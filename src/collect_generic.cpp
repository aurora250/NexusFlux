#include <NeFlux/common.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#if NEFLUX_PLATFORM_WINDOWS
#    include <windows.h>
#else
#    include <unistd.h>
#endif
namespace neforce::neflux {

    namespace {

        string host_name() {
#if NEFLUX_PLATFORM_WINDOWS
            char buffer[256] = {};
            DWORD size = sizeof(buffer);
            if (GetComputerNameA(buffer, &size) != 0) {
                return buffer;
            }
            return environment::get("COMPUTERNAME");
#else
            char buffer[256] = {};
            if (gethostname(buffer, sizeof(buffer)) == 0) {
                return buffer;
            }
            return environment::get("HOSTNAME");
#endif
        }

        string run_query(const string& program, const vector<string>& args) {
            try {
                return ::neforce::process::capture(program, args, 3000);
            } catch (...) {
                return {};
            }
        }

        vector<process> read_processes() {
            vector<process> out;
#if NEFLUX_PLATFORM_WINDOWS
            const string script =
                    R"PS(Get-CimInstance Win32_Process | ForEach-Object { "$($_.ProcessId)`t$($_.ParentProcessId)`t$($_.Name)`t$($_.WorkingSetSize)`t$($_.UserModeTime)`t$($_.KernelModeTime)`t$($_.ThreadCount)`t$($_.CommandLine)" })PS";
            const string text = run_query("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", script});
            for (const auto& line: split_lines(text)) {
                const auto fields = split(line.view(), '\t', true);
                if (fields.size() < 7) {
                    continue;
                }
                const auto pid = parse_u64(fields[0].view());
                if (!pid.has_value()) {
                    continue;
                }
                process item;
                item.pid_ = static_cast<uint32_t>(*pid);
                if (const auto parent = parse_u64(fields[1].view()); parent.has_value()) {
                    item.parent_ = static_cast<uint32_t>(*parent);
                }
                item.name_ = fields[2];
                item.memory_ = parse_u64(fields[3].view()).value_or(0);
                const uint64_t user_time = parse_u64(fields[4].view()).value_or(0);
                const uint64_t kernel_time = parse_u64(fields[5].view()).value_or(0);
                item.cpu_time_ms_ = (user_time + kernel_time) / 10000;
                const uint64_t threads = parse_u64(fields[6].view()).value_or(0);
                item.command_.clear();
                for (size_t index = 7; index < fields.size(); ++index) {
                    if (index != 7) {
                        item.command_.push_back('\t');
                    }
                    item.command_.append(fields[index]);
                }
                if (item.command_.empty()) {
                    item.command_ = item.name_;
                }
                item.user_ = environment::get("USERNAME");
                item.state_ = "Unknown";
                item.extra_ = json::object();
                item.extra_.set("threads", json(threads));
                out.push_back(move(item));
            }
#elif NEFLUX_PLATFORM_MACOS
            const string text = run_query("ps", {"-axo", "pid=,ppid=,user=,stat=,rss=,etime=,comm=,args="});
            for (const auto& line: split_lines(text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 7) {
                    continue;
                }
                const auto pid = parse_u64(parts[0].view());
                const auto parent = parse_u64(parts[1].view());
                const auto rss = parse_u64(parts[4].view());
                if (!pid.has_value()) {
                    continue;
                }
                process item;
                item.pid_ = static_cast<uint32_t>(*pid);
                if (parent.has_value()) {
                    item.parent_ = static_cast<uint32_t>(*parent);
                }
                item.user_ = parts[2];
                item.state_ = parts[3];
                item.memory_ = rss.has_value() ? *rss * 1024 : 0;
                item.name_ = parts[6];
                for (size_t index = 6; index < parts.size(); ++index) {
                    if (!item.command_.empty()) {
                        item.command_.push_back(' ');
                    }
                    item.command_.append(parts[index]);
                }
                item.extra_ = json::object();
                out.push_back(move(item));
            }
#endif
            return out;
        }

    } // namespace

    double rate(optional<uint64_t> previous, uint64_t current, double seconds) {
        if (!previous.has_value() || current < *previous || seconds <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(current - *previous) / seconds;
    }

    map<string, tuple<uint64_t, uint64_t, uint64_t>> parse_diskstats(string_view text) {
        map<string, tuple<uint64_t, uint64_t, uint64_t>> out;
        static_cast<void>(text);
        return out;
    }

    json cpu_breakdown(const vector<uint64_t>& previous, const vector<uint64_t>& current) {
        static_cast<void>(previous);
        static_cast<void>(current);
        return json::object();
    }

    snapshot initial_snapshot() {
        const auto& info = sysinfo::instance();
        const auto& system = info.get_system_info();
        const auto& memory_info = info.get_memory_info();
        const auto& cpu = info.get_CPU_info();
        snapshot result;
        result.hostname_ = host_name();
        result.os_ = info.get_os_version_info().product_name;
        result.kernel_ = info.get_os_version_info().version();
        result.uptime_ = sysinfo::uptime_seconds();
        result.cpu_name_ = cpu.brand;
        result.cores_.resize(cpu.logical_processors);
        result.memory_.total_ = memory_info.total_physical;
        result.memory_.available_ = memory_info.available_physical;
        result.memory_.used_ = memory_info.total_physical > memory_info.available_physical
                                       ? memory_info.total_physical - memory_info.available_physical
                                       : 0;
        result.memory_.free_ = memory_info.available_physical;
        result.memory_.swap_total_ = memory_info.total_page_file;
        result.memory_.swap_used_ = memory_info.total_page_file > memory_info.available_page_file
                                            ? memory_info.total_page_file - memory_info.available_page_file
                                            : 0;
        static_cast<void>(system);
        return result;
    }

    struct collector::impl {
        bool gpu_enabled = true;
        advanced advanced_telemetry;
        map<uint32_t, uint64_t> previous_process_cpu;
        connection_collector connections;
        int64_t process_at = 0;
        double started_ms = static_cast<double>(monotonic_ms());
        double previous_at = static_cast<double>(monotonic_ms());
        gpu_result gpu_cache;
    };

    collector::collector(bool gpu_enabled) :
    impl_(make_unique<impl>()) {
        impl_->gpu_enabled = gpu_enabled;
    }

    collector::~collector() = default;

    snapshot collector::collect(const config& cfg) { return collect_with_processes(cfg, true, true, true); }

    snapshot collector::collect_with_processes(const config& cfg, bool processes_enabled, bool live_gpu_processes,
                                               bool connections_enabled) {
        const auto& info = sysinfo::instance();
        const auto& cpu = info.get_CPU_info();
        const auto& memory_info = info.get_memory_info();
        snapshot result;
        result.timestamp_ = unix_time();
        result.elapsed_ = (static_cast<double>(monotonic_ms()) - impl_->started_ms) / 1000.0;
        result.hostname_ = host_name();
        result.os_ = info.get_os_version_info().product_name;
        result.kernel_ = info.get_os_version_info().version();
        result.uptime_ = sysinfo::uptime_seconds();
        result.cpu_name_ = cpu.brand;
        result.cpu_percent_ = info.cpu_usage();
        result.cpu_mhz_ = cpu.current_MHz;
        result.cores_.resize(cpu.logical_processors);
        for (auto& value: result.cores_) {
            value = result.cpu_percent_;
        }
        result.memory_.total_ = memory_info.total_physical;
        result.memory_.available_ = memory_info.available_physical;
        result.memory_.used_ = memory_info.total_physical > memory_info.available_physical
                                       ? memory_info.total_physical - memory_info.available_physical
                                       : 0;
        result.memory_.free_ = memory_info.available_physical;
        result.memory_.swap_total_ = memory_info.total_page_file;
        result.memory_.swap_used_ = memory_info.total_page_file > memory_info.available_page_file
                                            ? memory_info.total_page_file - memory_info.available_page_file
                                            : 0;
        if (connections_enabled) {
            result.connections_ = impl_->connections.collect(true);
        }
        if (processes_enabled) {
            result.processes_ = read_processes();
            const int64_t now = monotonic_ms();
            const double elapsed = impl_->process_at > 0 ? static_cast<double>(now - impl_->process_at) / 1000.0 : 0.0;
            for (auto& item: result.processes_) {
                const auto previous = impl_->previous_process_cpu.find(item.pid_);
                if (previous != impl_->previous_process_cpu.end() && elapsed > 0.0 &&
                    item.cpu_time_ms_ >= previous->second) {
                    item.cpu_ = static_cast<float>(static_cast<double>(item.cpu_time_ms_ - previous->second) /
                                                   (elapsed * 1000.0) * 100.0);
                }
                impl_->previous_process_cpu[item.pid_] = item.cpu_time_ms_;
            }
            impl_->process_at = now;
        }
        if (connections_enabled && !result.processes_.empty()) {
            for (auto& row: result.connections_.connections_) {
                for (const uint32_t pid: row.pids_) {
                    for (const auto& item: result.processes_) {
                        if (item.pid_ == pid) {
                            row.process_names_.push_back(item.name_);
                            break;
                        }
                    }
                }
            }
            result.connections_.owner_scan_complete_ = true;
            result.connections_.owners_sampled_at_ = unix_time();
        }
        for (const auto& item: sysinfo::network_interfaces()) {
            network value;
            value.name_ = item.name;
            value.mac_ = item.mac;
            value.address_ = item.address;
            value.state_ = item.is_up ? "up" : "down";
            result.networks_.push_back(move(value));
        }
        const auto disk_value = sysinfo::get_disk_info();
        disk storage;
        storage.name_ = disk_value.path;
        storage.mount_ = disk_value.path;
        storage.total_ = disk_value.total_bytes;
        storage.used_ = disk_value.used_bytes;
        storage.available_ = disk_value.free_bytes;
        storage.extra_ = json::object();
        result.disks_.push_back(move(storage));
        const auto gpu_values = collect_gpus(impl_->gpu_enabled, live_gpu_processes);
        result.gpus_ = gpu_values.devices_;
        result.gpu_status_ = gpu_values.status_;
        result.extra_ = json::object();
        json state = json::object();
        state.set("processes", json(processes_enabled));
        state.set("gpu_processes", json(live_gpu_processes));
        result.extra_.set("Collection state", move(state));
        impl_->advanced_telemetry.collect(result, cfg);
        if (result.cpu_percent_ >= cfg.cpu_warning_) {
            result.alerts_.push_back(format("CPU {:.0f}%", result.cpu_percent_));
        }
        result.collection_ms_ = 0.0;
        return result;
    }

    void collector::set_gpu_enabled(bool enabled) noexcept { impl_->gpu_enabled = enabled; }

    bool collector::gpu_enabled() const noexcept { return impl_->gpu_enabled; }

} // namespace neforce::neflux
