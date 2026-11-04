#include <NeForce/core/container/flat_unordered_map.hpp>
#include <NeFlux/common.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include "platform/platform_backend.hpp"
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/sysinfo.hpp>
namespace neforce::neflux {

    namespace {


        struct process_sample {
            uint64_t cpu_ms = 0;
            uint64_t read_bytes = 0;
            uint64_t write_bytes = 0;
            uint64_t start_ms = 0;
            bool valid = false;
        };

        string system_host_name() { return sysinfo::instance().host_name(); }

        string system_os_name() {
            const auto& os = sysinfo::instance().os_version();
            return os.product_name.empty() ? os.version() : os.product_name;
        }

        string system_kernel_version() { return sysinfo::instance().os_version().version(); }

        uint64_t system_uptime_seconds() { return sysinfo::uptime_seconds(); }

        string system_cpu_brand() { return sysinfo::instance().get_cpu_info().brand; }

        uint64_t system_physical_cores() { return sysinfo::instance().get_cpu_info().cores; }

    } // namespace

    double rate(optional<uint64_t> previous, uint64_t current, double seconds) {
        if (!previous.has_value() || current < *previous || seconds <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(current - *previous) / seconds;
    }

    map<string, tuple<uint64_t, uint64_t, uint64_t>> parse_diskstats(string_view text) {
        map<string, tuple<uint64_t, uint64_t, uint64_t>> out;
        for (const auto& line: split_lines(text)) {
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 14 || starts_with(parts[2].view(), "loop") || starts_with(parts[2].view(), "ram")) {
                continue;
            }
            const auto read = parse_u64(parts[5].view());
            const auto write = parse_u64(parts[9].view());
            const auto busy = parse_u64(parts[12].view());
            if (!read.has_value() || !write.has_value() || !busy.has_value()) {
                continue;
            }
            out[parts[2]] = {*read * 512, *write * 512, *busy};
        }
        return out;
    }

    json cpu_breakdown(const vector<uint64_t>& previous, const vector<uint64_t>& current) {
        json out = json::object();
        if (previous.size() != current.size() || previous.empty()) {
            return out;
        }
        const size_t count = current.size();
        if (!(count == 3 || count == 4 || count == 8 || count == 10)) {
            return out;
        }
        vector<uint64_t> delta(count, 0);
        for (size_t i = 0; i < count; ++i) {
            if (current[i] < previous[i]) {
                return out;
            }
            delta[i] = current[i] - previous[i];
        }
        uint64_t total = 0;
        for (size_t i = 0; i < min(static_cast<size_t>(8), count); ++i) {
            total += delta[i];
        }
        if (count == 10) {
            delta[0] = delta[0] > delta[8] ? delta[0] - delta[8] : 0;
            delta[1] = delta[1] > delta[9] ? delta[1] - delta[9] : 0;
        }
        if (total == 0) {
            return out;
        }
        vector<const char*> labels;
        if (count == 3) {
            labels = {"user", "system", "idle"};
        } else if (count == 4) {
            labels = {"user", "nice", "system", "idle"};
        } else if (count == 10) {
            labels = {"user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal", "guest", "guest_nice"};
        } else {
            labels = {"user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal"};
        }
        for (size_t i = 0; i < count; ++i) {
            out.set(labels[i], json(static_cast<double>(delta[i]) / static_cast<double>(total) * 100.0));
        }
        return out;
    }

    string simd_extensions(const sysinfo::simd_info& simd) {
        string text;
        const auto add = [&text](const char* name) {
            if (!text.empty()) {
                text.append(", ");
            }
            text.append(name);
        };
        if (simd.supports(sysinfo::simd_instruction::sse2)) {
            add("SSE2");
        }
        if (simd.supports(sysinfo::simd_instruction::ssse3)) {
            add("SSSE3");
        }
        if (simd.supports(sysinfo::simd_instruction::sse4_1)) {
            add("SSE4.1");
        }
        if (simd.supports(sysinfo::simd_instruction::sse4_2)) {
            add("SSE4.2");
        }
        if (simd.supports(sysinfo::simd_instruction::avx)) {
            add("AVX");
        }
        if (simd.supports(sysinfo::simd_instruction::avx2)) {
            add("AVX2");
        }
        if (simd.supports(sysinfo::simd_instruction::fma)) {
            add("FMA3");
        }
        if (simd.supports(sysinfo::simd_instruction::avx512f)) {
            add("AVX-512");
        }
        if (simd.supports(sysinfo::simd_instruction::aes)) {
            add("AES");
        }
        if (simd.supports(sysinfo::simd_instruction::pclmulqdq)) {
            add("PCLMULQDQ");
        }
        if (simd.supports(sysinfo::simd_instruction::neon)) {
            add("NEON");
        }
        if (simd.supports(sysinfo::simd_instruction::sve)) {
            add("SVE");
        }
        return text;
    }

    string or_dash(const string& value) { return value.empty() ? string("—") : value; }

    string cache_label(const uint32_t kib, const uint32_t instances) {
        if (kib == 0) {
            return "—";
        }
        const string size = kib >= 1024 ? format("{} MiB", kib / 1024) : format("{} KiB", kib);
        return instances > 1 ? format("{} ×{}", size, instances) : size;
    }

    string architecture_name(const sysinfo::architecture architecture) {
        switch (architecture) {
            case sysinfo::architecture::x86:
                return "x86";
            case sysinfo::architecture::x64:
                return "x86_64";
            case sysinfo::architecture::arm:
                return "ARM";
            case sysinfo::architecture::arm64:
                return "ARM64";
            case sysinfo::architecture::riscv32:
                return "RISC-V 32";
            case sysinfo::architecture::riscv64:
                return "RISC-V 64";
            case sysinfo::architecture::loongarch32:
                return "LoongArch 32";
            case sysinfo::architecture::loongarch64:
                return "LoongArch 64";
            case sysinfo::architecture::unknown:
                break;
        }
        return "Unknown";
    }

    json cpu_info_json(const sysinfo::cpu_info& cpu, const sysinfo::architecture architecture) {
        json result = json::object();
        json details = json::array();
        const auto detail = [&result, &details](const char* label, const string& value) {
            const string shown = or_dash(value);
            result.set(label, json(shown));
            json item = json::object();
            item.set("Label", json(label));
            item.set("Value", json(shown));
            details.push(move(item));
        };

        result.set("Architecture", json(architecture_name(architecture)));
        result.set("Vendor", json(or_dash(cpu.vendor)));
        result.set("Brand", json(or_dash(cpu.brand)));
        result.set("Max clock", json(cpu.max_mhz != 0 ? format("{} MHz", cpu.max_mhz) : string("—")));
        result.set("Current clock", json(cpu.current_mhz != 0 ? format("{} MHz", cpu.current_mhz) : string("—")));
        result.set("Cores / Threads",
                   json(cpu.ecore_count != 0
                                ? format("{}P + {}E / {}T", cpu.pcore_count, cpu.ecore_count, cpu.logical_processors)
                                : format("{} / {}", cpu.cores, cpu.logical_processors)));
        result.set("Instructions", json(or_dash(cpu.features)));
        result.set("SIMD", json(or_dash(simd_extensions(cpu.simd))));
        result.set("L1", json(cache_label(cpu.l1_kib, cpu.l1_instances)));
        result.set("L1 data", json(cache_label(cpu.l1_data_kib, cpu.l1_data_instances)));
        result.set("L1 inst", json(cache_label(cpu.l1_instruction_kib, cpu.l1_instruction_instances)));
        result.set("L2", json(cache_label(cpu.l2_kib, cpu.l2_instances)));
        result.set("L3", json(cache_label(cpu.l3_kib, cpu.l3_instances)));

        switch (architecture) {
            case sysinfo::architecture::x86:
            case sysinfo::architecture::x64: {
                detail("Family", cpu.x86.family);
                detail("Model", cpu.x86.model);
                detail("Stepping", cpu.x86.stepping);
                detail("Ext family", cpu.x86.ext_family);
                detail("Ext model", cpu.x86.ext_model);
                detail("Revision", cpu.x86.revision);
                detail("Core VID", cpu.x86.core_vid);
                detail("Technology", cpu.x86.technology);
                detail("Socket", cpu.x86.socket);
                detail("Code name", cpu.x86.code_name);
                detail("Bus speed", cpu.x86.bus_speed_mhz != 0 ? format("{} MHz", cpu.x86.bus_speed_mhz) : string());
                detail("Multiplier", cpu.x86.bus_speed_mhz != 0 && cpu.current_mhz != 0
                                             ? format("{:.2f}", static_cast<double>(cpu.current_mhz) /
                                                                        static_cast<double>(cpu.x86.bus_speed_mhz))
                                             : string());
                detail("Max TDP", cpu.x86.max_tdp_watts != 0 ? format("{} W", cpu.x86.max_tdp_watts) : string());
                break;
            }
            case sysinfo::architecture::arm:
            case sysinfo::architecture::arm64: {
                detail("ARM arch", cpu.arm.architecture);
                detail("Implementer", cpu.arm.implementer);
                detail("Part", cpu.arm.part);
                detail("Variant", cpu.arm.variant);
                detail("Revision", cpu.arm.revision);
                detail("Core name", cpu.arm.core_name);
                break;
            }
            case sysinfo::architecture::riscv32:
            case sysinfo::architecture::riscv64: {
                detail("ISA", cpu.riscv.isa);
                detail("MMU", cpu.riscv.mmu);
                detail("Uarch", cpu.riscv.uarch);
                detail("MVendor ID", cpu.riscv.mvendorid);
                detail("March ID", cpu.riscv.marchid);
                detail("MImp ID", cpu.riscv.mimpid);
                break;
            }
            case sysinfo::architecture::loongarch32:
            case sysinfo::architecture::loongarch64: {
                detail("Family", cpu.loongarch.family);
                detail("Revision", cpu.loongarch.revision);
                detail("FPU revision", cpu.loongarch.fpu_revision);
                detail("ISA", cpu.loongarch.isa);
                detail("Address size", cpu.loongarch.address_sizes);
                detail("Machine", cpu.loongarch.machine);
                detail("System type", cpu.loongarch.system_type);
                detail("Watchpoint", cpu.loongarch.watchpoint);
                detail("TLB entries",
                       cpu.loongarch.tlb_entries != 0 ? format("{}", cpu.loongarch.tlb_entries) : string());
                break;
            }
            case sysinfo::architecture::unknown:
                break;
        }
        result.set("Details", move(details));
        return result;
    }

    json hardware_snapshot(uint64_t memory_total) {
        const auto& info = sysinfo::instance();
        const auto& cpu = info.get_cpu_info();
        const auto& hardware = info.get_hardware_info();
        const auto architecture = info.get_architecture();
        json root = json::object();
        root.set("CPU", cpu_info_json(cpu, architecture));

        json bios = json::object();
        bios.set("Brand", or_dash(hardware.bios.brand));
        bios.set("Version", or_dash(hardware.bios.version));
        bios.set("Date", or_dash(hardware.bios.date));
        bios.set("Microcode", or_dash(hardware.bios.microcode));
        root.set("BIOS", move(bios));

        json board = json::object();
        board.set("Vendor", or_dash(hardware.board.vendor));
        board.set("Model", or_dash(hardware.board.model));
        board.set("Bus", or_dash(hardware.board.bus));
        board.set("Chipset", or_dash(hardware.board.chipset));
        board.set("Southbridge", or_dash(hardware.board.southbridge));
        root.set("Board", move(board));

        json memory = json::object();
        memory.set("Type", or_dash(hardware.memory.type));
        memory.set("Channel", or_dash(hardware.memory.channel));
        memory.set("Size", memory_total != 0 ? bytes(static_cast<double>(memory_total)) : string("—"));
        memory.set("Controller freq",
                   hardware.memory.configured_mts != 0
                           ? format("{} MT/s (rated {})", hardware.memory.configured_mts, hardware.memory.rated_mts)
                           : string("—"));
        const double dram_mhz = static_cast<double>(hardware.memory.configured_mts) / 2.0;
        memory.set("DRAM freq", dram_mhz > 0.0 ? format("{:.1f} MHz", dram_mhz) : string("—"));
        const bool x86_architecture =
                architecture == sysinfo::architecture::x86 || architecture == sysinfo::architecture::x64;
        memory.set("FSB:DRAM", x86_architecture && dram_mhz > 0.0 && cpu.x86.bus_speed_mhz != 0
                                       ? format("1:{}", static_cast<uint64_t>(neforce::round(
                                                                dram_mhz / static_cast<double>(cpu.x86.bus_speed_mhz))))
                                       : string("—"));
        memory.set("LLC / Ring", or_dash(hardware.memory.llc_ring));
        memory.set("Timings", or_dash(hardware.memory.timings));
        root.set("Memory", move(memory));
        return root;
    }

    struct collector::impl {
        bool gpu_enabled = true;
        flat_unordered_map<uint32_t, process_sample> previous_processes;
        flat_unordered_map<string, tuple<uint64_t, uint64_t, uint64_t, uint64_t>> previous_networks;
        connection_collector connections;
        advanced advanced_telemetry;
        int64_t process_at = 0;
        double started_ms = static_cast<double>(monotonic_ms());
        double previous_at = static_cast<double>(monotonic_ms());
        gpu_result cached_gpu;
        vector<platform::topology_entry> topology;
        vector<sensor> sensors;
        vector<battery> batteries;
        vector<disk> disks;
        double sensors_at = -1.0;
        double disks_at = -1.0;

        impl() {
            topology = platform::read_topology(sysinfo::instance().get_cpu_info().logical_processors);
            sensors = platform::read_sensors();
            batteries = platform::read_batteries();
            disks = platform::read_disks();
            const double now = static_cast<double>(monotonic_ms());
            sensors_at = now;
            disks_at = now;
            static_cast<void>(sysinfo::cpu_usage());
            static_cast<void>(sysinfo::cpu_usage_per_core());
            static_cast<void>(sysinfo::cpu_usage_breakdown());
            static_cast<void>(sysinfo::disk_io_rates());
        }

        snapshot sample_exact(const config& cfg, bool processes_enabled, bool live_gpu_processes,
                              bool connections_enabled) {
            const auto began = static_cast<double>(monotonic_ms());
            const double elapsed = max(0.001, (began - previous_at) / 1000.0);
            previous_at = began;
            const double collection_started = began;

            auto& info = sysinfo::instance();
            const auto& cpu = info.get_cpu_info();
            float cpu_percent = static_cast<float>(sysinfo::cpu_usage());
            vector<float> cores;
            const auto core_usage = sysinfo::cpu_usage_per_core();
            cores.reserve(core_usage.size());
            for (const auto value: core_usage) {
                cores.push_back(static_cast<float>(value));
            }
            if (cores.empty()) {
                cores.assign(cpu.logical_processors, cpu_percent);
            } else if (cpu_percent <= 0.0F) {
                double total = 0.0;
                for (const float value: cores) {
                    total += value;
                }
                cpu_percent = static_cast<float>(total / static_cast<double>(cores.size()));
            }

            const auto breakdown_value = sysinfo::cpu_usage_breakdown();
            json breakdown = json::object();
            breakdown.set("user", json(breakdown_value.user));
            breakdown.set("nice", json(breakdown_value.nice));
            breakdown.set("system", json(breakdown_value.system));
            breakdown.set("idle", json(breakdown_value.idle));
            breakdown.set("iowait", json(breakdown_value.iowait));
            breakdown.set("irq", json(breakdown_value.irq));
            breakdown.set("softirq", json(breakdown_value.softirq));
            breakdown.set("steal", json(breakdown_value.steal));
            breakdown.set("guest", json(breakdown_value.guest));
            breakdown.set("guest_nice", json(breakdown_value.guest_nice));

            uint64_t cpu_mhz = cpu.current_mhz;
            if (const uint64_t platform_mhz = platform::cpu_frequency_mhz(); platform_mhz != 0) {
                cpu_mhz = platform_mhz;
            }

            float load[3] = {0.0F, 0.0F, 0.0F};
            platform::read_load(load);

            const auto memory_status_value = sysinfo::memory_status();
            memory memory_value;
            memory_value.total_ = memory_status_value.total_physical;
            memory_value.available_ = memory_status_value.available_physical;
            memory_value.free_ = memory_status_value.available_physical;
            const uint64_t swap_free = memory_status_value.available_page_file;
            memory_value.swap_total_ = memory_status_value.total_page_file;
            memory_value.swap_used_ = memory_value.swap_total_ > swap_free ? memory_value.swap_total_ - swap_free : 0;
            if (memory_status_value.cached_physical > 0) {
                memory_value.cached_ = memory_status_value.cached_physical;
            }
            const auto meminfo = platform::read_meminfo();
            if (meminfo.total.has_value()) {
                memory_value.total_ = *meminfo.total;
            }
            if (meminfo.available.has_value()) {
                memory_value.available_ = *meminfo.available;
            }
            if (meminfo.free.has_value()) {
                memory_value.free_ = *meminfo.free;
            }
            if (meminfo.cached.has_value()) {
                memory_value.cached_ = *meminfo.cached;
            }
            if (meminfo.swap_total.has_value()) {
                memory_value.swap_total_ = *meminfo.swap_total;
            }
            const uint64_t swap_free_value = meminfo.swap_free.value_or(0);
            memory_value.swap_used_ =
                    memory_value.swap_total_ > swap_free_value ? memory_value.swap_total_ - swap_free_value : 0;
            memory_value.used_ =
                    memory_value.total_ > memory_value.available_ ? memory_value.total_ - memory_value.available_ : 0;

            vector<process> processes;
            if (processes_enabled) {
                processes = collect_processes(elapsed, cfg.collection_.show_threads_);
            }

            vector<network> networks = collect_networks(elapsed);

            vector<disk> current_disks = disks;
            if (began - disks_at >= 10.0 * 1000.0) {
                current_disks = platform::read_disks();
                disks = current_disks;
                disks_at = began;
            }

            if (began - sensors_at >= 2.0 * 1000.0) {
                sensors = platform::read_sensors();
                batteries = platform::read_batteries();
                sensors_at = began;
            }
            const vector<sensor>& sensors_value = sensors;
            const vector<battery>& batteries_value = batteries;

            vector<disk_io> disk_io_values;
            for (const auto& item: sysinfo::disk_io_rates()) {
                disk_io value;
                value.name_ = item.name;
                const auto sane_rate = [](const double rate) {
                    return neforce::is_finite(rate) && rate >= 0.0 && rate < 1.0e18 ? rate : 0.0;
                };
                value.read_per_sec_ = sane_rate(item.read_bytes_per_second);
                value.write_per_sec_ = sane_rate(item.write_bytes_per_second);
                value.busy_percent_ = item.busy_percent;
                value.aggregate_ = item.aggregate;
                disk_io_values.push_back(move(value));
            }
            const auto core_temperatures_value = platform::core_temperatures(topology);
            auto pressure_values = platform::read_pressure();

            const auto gpu_values = collect_gpus(gpu_enabled, live_gpu_processes);
            cached_gpu = gpu_values;

            flat_unordered_map<uint32_t, tuple<optional<uint64_t>, optional<uint32_t>, vector<uint32_t>>> gpu_processes;
            for (const auto& device: gpu_values.devices_) {
                if (const json* mig = device.extra_.get("mig_children");
                    mig != nullptr && mig->is_integer() && mig->as_integer() > 0) {
                    continue;
                }
                for (const auto& item: device.processes_) {
                    auto& entry = gpu_processes[item.pid_];
                    if (item.memory_.has_value()) {
                        get<0>(entry) = get<0>(entry).has_value() ? *get<0>(entry) + *item.memory_ : *item.memory_;
                    }
                    if (item.utilization_.has_value()) {
                        get<1>(entry) =
                                get<1>(entry).has_value() ? *get<1>(entry) + *item.utilization_ : *item.utilization_;
                    }
                    get<2>(entry).push_back(device.id_);
                }
            }
            for (auto& item: processes) {
                const auto found = gpu_processes.find(item.pid_);
                if (found == gpu_processes.end()) {
                    continue;
                }
                item.gpu_memory_ = get<0>(found->second);
                item.gpu_percent_ = get<1>(found->second);
                item.gpu_ids_ = get<2>(found->second);
            }
            neforce::tim_sort(processes.begin(), processes.end(), [](const process& a, const process& b) {
                if (a.cpu_ != b.cpu_) {
                    return a.cpu_ > b.cpu_;
                }
                return a.pid_ < b.pid_;
            });

            snapshot result;
            result.timestamp_ = unix_time();
            result.elapsed_ = (static_cast<double>(monotonic_ms()) - started_ms) / 1000.0;
            result.collection_ms_ = 0.0;
            result.hostname_ = system_host_name();
            result.os_ = system_os_name();
            result.kernel_ = system_kernel_version();
            result.uptime_ = system_uptime_seconds();
            result.cpu_name_ = system_cpu_brand();
            result.cpu_percent_ = cpu_percent;
            result.cpu_mhz_ = cpu_mhz;
            result.cpu_breakdown_ = move(breakdown);
            result.cores_ = move(cores);
            result.pcore_count_ = cpu.pcore_count;
            result.ecore_count_ = cpu.ecore_count;
            result.core_temperatures_ = move(core_temperatures_value);
            for (size_t i = 0; i < 3; ++i) {
                result.load_[i] = load[i];
            }
            result.memory_ = memory_value;
            result.processes_ = move(processes);
            result.networks_ = move(networks);
            result.disks_ = move(current_disks);
            result.disk_io_ = move(disk_io_values);
            result.sensors_ = move(sensors_value);
            result.batteries_ = move(batteries_value);
            result.gpus_ = gpu_values.devices_;
            result.gpu_status_ = gpu_values.status_;
            result.pressure_ = move(pressure_values);
            if (connections_enabled) {
                result.connections_ = connections.collect(true);
            }
            for (auto& row: result.connections_.connections_) {
                if (!row.process_names_.empty()) {
                    continue;
                }
                for (const uint32_t pid: row.pids_) {
                    for (const auto& item: result.processes_) {
                        if (item.pid_ == pid) {
                            row.process_names_.push_back(item.name_);
                            break;
                        }
                    }
                }
            }

            if (result.cpu_percent_ >= cfg.cpu_warning_) {
                result.alerts_.push_back(format("CPU {:.0f}%", result.cpu_percent_));
            }
            const double memory_percent = percent(result.memory_.used_, result.memory_.total_);
            if (memory_percent >= cfg.memory_warning_) {
                result.alerts_.push_back(format("Memory {:.0f}%", memory_percent));
            }
            for (const auto& disk_item: result.disks_) {
                if (percent(disk_item.used_, disk_item.total_) >= 90.0) {
                    result.alerts_.push_back(format("Disk {} ≥90%", disk_item.mount_));
                }
            }
            for (const auto& device: result.gpus_) {
                if (device.temperature_.has_value() && *device.temperature_ >= cfg.gpu_temperature_warning_) {
                    result.alerts_.push_back(format("GPU {} hot: {}°C", device.id_, *device.temperature_));
                }
            }
            for (const auto& item: result.pressure_) {
                if (item.some_10_.has_value() && *item.some_10_ > 20.0) {
                    result.alerts_.push_back(format("{} pressure {:.0f}%", item.resource_, *item.some_10_));
                }
            }

            result.extra_ = json::object();
            result.extra_.set("Hardware", hardware_snapshot(result.memory_.total_));
            advanced_telemetry.collect(result, cfg);
            json collection_state = json::object();
            collection_state.set("processes", json(processes_enabled));
            collection_state.set("gpu_processes", json(live_gpu_processes));
            result.extra_.set("Collection state", move(collection_state));

            json topology_json = json::object();
            topology_json.set("logical_cores", json(result.cores_.size()));
            topology_json.set("physical_cores", json(system_physical_cores()));
            topology_json.set("architecture", json(architecture_name(sysinfo::instance().get_architecture())));
            if (const auto frequencies = platform::core_frequencies_mhz(result.cores_.size()); !frequencies.empty()) {
                json values = json::array();
                for (const uint64_t value: frequencies) {
                    values.push(json(static_cast<int64_t>(value)));
                }
                topology_json.set("per_core_frequency_mhz", move(values));
            }
            result.extra_.set("CPU topology", move(topology_json));

            map<string, uint64_t> states;
            optional<uint64_t> threads;
            for (const auto& item: result.processes_) {
                if (item.thread_) {
                    continue;
                }
                states[item.state_] += 1;
                const json* value = item.extra_.get("threads");
                if (value != nullptr && (value->is_integer() || value->is_number())) {
                    threads = (threads.has_value() ? *threads : 0) + static_cast<uint64_t>(value->as_integer());
                }
            }
            json counts = json::object();
            json state_json = json::object();
            for (const auto& entry: states) {
                state_json.set(entry.first, json(entry.second));
            }
            counts.set("states", move(state_json));
            counts.set("threads", threads.has_value() ? json(*threads) : json());
            counts.set("sampled_rows", json(result.processes_.size()));
            result.extra_.set("Process counts", move(counts));

            result.collection_ms_ = static_cast<double>(monotonic_ms()) - collection_started;
            return result;
        }

        vector<process> collect_processes(double seconds, bool include_threads) {
            vector<process> out;
            set<uint32_t> seen;
            for (const auto& entry: ::neforce::process::snapshot()) {
                if (!seen.insert(entry.pid).second) {
                    continue;
                }
                process item;
                item.pid_ = entry.pid;
                if (entry.parent_pid != 0) {
                    item.parent_ = entry.parent_pid;
                }
                item.start_time_ = entry.start_time_ms / 1000;
                item.name_ = entry.name;
                item.command_ = entry.command.empty() ? entry.name : entry.command;
                item.user_ = entry.user.empty() ? string("—") : entry.user;
                item.state_ = platform::process_state_text(entry);
                item.memory_ = entry.rss_bytes;
                item.virtual_memory_ = entry.virtual_bytes;
                item.cpu_time_ms_ = entry.user_time_ms + entry.system_time_ms;
                item.read_total_ = entry.io.read_bytes;
                item.write_total_ = entry.io.write_bytes;
                item.kernel_thread_ = entry.kernel_thread;
                item.nice_ = entry.nice;
                item.runtime_ = unix_time() > item.start_time_ ? unix_time() - item.start_time_ : 0;
                item.extra_ = json::object();
                item.extra_.set("threads", json(static_cast<uint64_t>(entry.threads)));

                auto& previous = previous_processes[entry.pid];
                if (previous.valid && previous.start_ms == entry.start_time_ms && seconds > 0.0) {
                    if (item.cpu_time_ms_ >= previous.cpu_ms) {
                        item.cpu_ = static_cast<float>(
                                static_cast<double>(item.cpu_time_ms_ - previous.cpu_ms) * 100.0 / (seconds * 1000.0));
                    }
                    if (item.read_total_ >= previous.read_bytes) {
                        item.read_per_sec_ =
                                static_cast<double>(item.read_total_ - previous.read_bytes) / max(0.001, seconds);
                    }
                    if (item.write_total_ >= previous.write_bytes) {
                        item.write_per_sec_ =
                                static_cast<double>(item.write_total_ - previous.write_bytes) / max(0.001, seconds);
                    }
                }
                previous.cpu_ms = item.cpu_time_ms_;
                previous.read_bytes = item.read_total_;
                previous.write_bytes = item.write_total_;
                previous.start_ms = entry.start_time_ms;
                previous.valid = true;

                seen.insert(entry.pid);
                out.push_back(move(item));

                if (include_threads) {
                    for (const auto& thread: ::neforce::process::threads(entry.pid)) {
                        if (thread.tid == entry.pid) {
                            continue;
                        }
                        process thread_item = out.back();
                        thread_item.pid_ = thread.tid;
                        thread_item.thread_ = true;
                        thread_item.name_ = thread.name.empty() ? thread_item.name_ : thread.name;
                        thread_item.start_time_ = thread.start_time_ms / 1000;
                        thread_item.cpu_time_ms_ = thread.user_time_ms + thread.system_time_ms;
                        thread_item.cpu_ = 0.0F;
                        thread_item.read_per_sec_ = 0.0;
                        thread_item.write_per_sec_ = 0.0;
                        thread_item.memory_ = 0;
                        thread_item.virtual_memory_ = 0;
                        out.push_back(move(thread_item));
                    }
                }
            }
            vector<uint32_t> stale;
            for (const auto& entry: previous_processes) {
                if (seen.count(entry.first) == 0) {
                    stale.push_back(entry.first);
                }
            }
            for (const uint32_t pid: stale) {
                previous_processes.erase(pid);
            }
            return out;
        }

        vector<network> collect_networks(double seconds) {
            vector<network> out;
            flat_unordered_map<string, tuple<uint64_t, uint64_t, uint64_t, uint64_t>> current;
            for (const auto& item: sysinfo::network_interfaces()) {
                network value;
                value.name_ = item.name;
                value.mac_ = item.mac;
                value.address_ = item.address;
                value.mtu_ = item.mtu;
                value.state_ = item.operstate.empty() ? (item.is_up ? "up" : "down") : item.operstate;
                if (item.speed_bps > 0) {
                    value.speed_mbps_ = item.speed_bps / 1000000ULL;
                }
                value.rx_packets_ = item.rx_packets;
                value.tx_packets_ = item.tx_packets;
                value.rx_errors_ = item.rx_errors;
                value.tx_errors_ = item.tx_errors;
                value.errors_ = item.rx_errors + item.tx_errors;
                value.drops_ = item.rx_dropped + item.tx_dropped;
                value.rx_total_ = item.rx_bytes;
                value.tx_total_ = item.tx_bytes;
                const auto previous = previous_networks.find(item.name);
                if (previous != previous_networks.end() && seconds > 0.0) {
                    if (item.rx_bytes >= get<0>(previous->second)) {
                        value.rx_per_sec_ = static_cast<double>(item.rx_bytes - get<0>(previous->second)) / seconds;
                    }
                    if (item.tx_bytes >= get<1>(previous->second)) {
                        value.tx_per_sec_ = static_cast<double>(item.tx_bytes - get<1>(previous->second)) / seconds;
                    }
                    if (item.rx_packets >= get<2>(previous->second)) {
                        value.rx_packets_per_sec_ =
                                static_cast<double>(item.rx_packets - get<2>(previous->second)) / seconds;
                    }
                    if (item.tx_packets >= get<3>(previous->second)) {
                        value.tx_packets_per_sec_ =
                                static_cast<double>(item.tx_packets - get<3>(previous->second)) / seconds;
                    }
                }
                current[item.name] = {item.rx_bytes, item.tx_bytes, item.rx_packets, item.tx_packets};
                out.push_back(move(value));
            }
            previous_networks = move(current);
            neforce::tim_sort(out.begin(), out.end(), [](const network& a, const network& b) {
                if (a.rx_per_sec_ != b.rx_per_sec_) {
                    return a.rx_per_sec_ > b.rx_per_sec_;
                }
                return a.name_ < b.name_;
            });
            return out;
        }

    };

    snapshot initial_snapshot() {
        snapshot result;
        const auto memory = sysinfo::memory_status();
        result.hostname_ = system_host_name();
        result.os_ = system_os_name();
        result.kernel_ = system_kernel_version();
        result.uptime_ = system_uptime_seconds();
        result.cpu_name_ = system_cpu_brand();
        result.memory_.total_ = memory.total_physical;
        result.memory_.available_ = memory.available_physical;
        result.memory_.free_ = memory.available_physical;
        if (memory.cached_physical > 0) {
            result.memory_.cached_ = memory.cached_physical;
        }
        result.memory_.used_ = memory.total_physical > memory.available_physical
                                       ? memory.total_physical - memory.available_physical
                                       : 0;
        result.memory_.swap_total_ = memory.total_page_file;
        result.memory_.swap_used_ = memory.total_page_file > memory.available_page_file
                                            ? memory.total_page_file - memory.available_page_file
                                            : 0;
        const auto& initial_cpu = sysinfo::instance().get_cpu_info();
        result.cpu_name_ = initial_cpu.brand;
        result.cpu_mhz_ = initial_cpu.current_mhz;
        result.cores_.assign(initial_cpu.logical_processors, 0.0F);
        result.pcore_count_ = initial_cpu.pcore_count;
        result.ecore_count_ = initial_cpu.ecore_count;
        return result;
    }

    collector::collector(bool gpu_enabled) :
    impl_(make_unique<impl>()) {
        impl_->gpu_enabled = gpu_enabled;
    }

    collector::~collector() = default;

    snapshot collector::collect(const config& cfg) { return collect_with_processes(cfg, true, true, true); }

    snapshot collector::collect_with_processes(const config& cfg, bool processes_enabled, bool live_gpu_processes,
                                               bool connections_enabled) {
        return impl_->sample_exact(cfg, processes_enabled, live_gpu_processes, connections_enabled);
    }

    void collector::set_gpu_enabled(bool enabled) noexcept { impl_->gpu_enabled = enabled; }

    bool collector::gpu_enabled() const noexcept { return impl_->gpu_enabled; }

} // namespace neforce::neflux
