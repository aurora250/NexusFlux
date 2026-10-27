#include <NeFlux/common.hpp>
#include <NeForce/core/string/string_view.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/container/map.hpp>
#include <NeForce/core/system/dynamic_library.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <sys/utsname.h>
#elif NEFLUX_PLATFORM_WINDOWS
#    include <windows.h>
#    include <dxgi.h>
#endif
namespace neforce::neflux {

    namespace {

        string gpu_identity(const gpu& item) {
            string name = lower_ascii(item.name_.view());
            name.trim();
            string vendor = lower_ascii(item.vendor_.view());
            vendor.trim();
            return format("{}|{}", vendor, name);
        }

        string device_name_of(const string& value) {
            const auto slash = value.rfind('/');
            const auto backslash = value.rfind('\\');
            const auto position = slash == string::npos
                                          ? backslash
                                          : (backslash == string::npos ? slash : neforce::max(slash, backslash));
            return position == string::npos ? value : value.substr(position + 1);
        }

        optional<uint64_t> read_number(const string& file_path) {
            const auto text = read_text_file(file_path);
            return text.has_value() ? parse_u64(trim(text->view()).view()) : none;
        }

        optional<uint32_t> read_dpm_clock(const string& file_path) {
            const auto text = read_text_file(file_path);
            if (!text.has_value()) {
                return none;
            }
            optional<uint32_t> active;
            optional<uint32_t> highest;
            for (const auto& line: split_lines(*text)) {
                const auto star = line.find('*');
                const auto mhz = line.find("Mhz");
                if (mhz == string::npos) {
                    continue;
                }
                size_t begin = mhz;
                while (begin > 0 && line[begin - 1] >= '0' && line[begin - 1] <= '9') {
                    --begin;
                }
                const auto value = begin < mhz ? parse_u64(line.view().substr(begin, mhz - begin)) : none;
                if (!value.has_value()) {
                    continue;
                }
                const auto clock = static_cast<uint32_t>(*value);
                if (!highest.has_value() || clock > *highest) {
                    highest = clock;
                }
                if (star != string::npos) {
                    active = clock;
                }
            }
            return active.has_value() ? active : highest;
        }

        string kernel_version() {
#if NEFLUX_PLATFORM_LINUX
            ::utsname info{};
            if (::uname(&info) == 0) {
                return info.release;
            }
#endif
            return {};
        }

        using nvml_return = int;
        using nvml_device = void*;

        struct nvml_utilization {
            unsigned int gpu;
            unsigned int memory;
        };

        struct nvml_memory {
            unsigned long long total;
            unsigned long long free;
            unsigned long long used;
        };

        struct nvml_process_info {
            unsigned int pid;
            unsigned long long used_gpu_memory;
            unsigned int gpu_instance_id;
            unsigned int compute_instance_id;
        };

        struct nvml_process_utilization {
            unsigned int pid;
            unsigned long long timestamp;
            unsigned int sm_util;
            unsigned int mem_util;
            unsigned int enc_util;
            unsigned int dec_util;
        };

        struct nvml_api {
#if NEFLUX_PLATFORM_WINDOWS
            dynamic_library library_{"nvml.dll", dynamic_library::load_mode::now};
#elif NEFLUX_PLATFORM_MACOS
            dynamic_library library_{"libnvidia-ml.dylib", dynamic_library::load_mode::now};
#else
            dynamic_library library_{"libnvidia-ml.so.1", dynamic_library::load_mode::now};
#endif
            dynamic_library* library_ptr_ = nullptr;
            nvml_return (*init)() = nullptr;
            nvml_return (*shutdown)() = nullptr;
            nvml_return (*device_count)(unsigned int*) = nullptr;
            nvml_return (*device_by_index)(unsigned int, nvml_device*) = nullptr;
            nvml_return (*device_name)(nvml_device, char*, unsigned int) = nullptr;
            nvml_return (*device_uuid)(nvml_device, char*, unsigned int) = nullptr;
            const char* (*error_string)(nvml_return) = nullptr;
            nvml_return (*system_driver_version)(char*, unsigned int) = nullptr;
            nvml_return (*system_nvml_version)(char*, unsigned int) = nullptr;
            nvml_return (*system_cuda_driver_version)(int*) = nullptr;
            nvml_return (*device_utilization)(nvml_device, nvml_utilization*) = nullptr;
            nvml_return (*device_memory)(nvml_device, nvml_memory*) = nullptr;
            nvml_return (*device_temperature)(nvml_device, int, unsigned int*) = nullptr;
            nvml_return (*device_fan_speed)(nvml_device, unsigned int, unsigned int*) = nullptr;
            nvml_return (*device_power_usage)(nvml_device, unsigned int*) = nullptr;
            nvml_return (*device_power_limit)(nvml_device, unsigned int*) = nullptr;
            nvml_return (*device_clock)(nvml_device, int, unsigned int*) = nullptr;
            nvml_return (*device_encoder)(nvml_device, unsigned int*, unsigned int*) = nullptr;
            nvml_return (*device_decoder)(nvml_device, unsigned int*, unsigned int*) = nullptr;
            nvml_return (*device_pcie_throughput)(nvml_device, int, unsigned int*) = nullptr;
            nvml_return (*device_pstate)(nvml_device, int*) = nullptr;
            nvml_return (*device_compute_processes)(nvml_device, unsigned int*, nvml_process_info*) = nullptr;
            nvml_return (*device_graphics_processes)(nvml_device, unsigned int*, nvml_process_info*) = nullptr;
            nvml_return (*device_process_utilization)(nvml_device, nvml_process_utilization*, unsigned int*,
                                                      unsigned long long) = nullptr;
            nvml_return (*device_vbios)(nvml_device, char*, unsigned int) = nullptr;
            nvml_return (*device_pcie_generation)(nvml_device, unsigned int*) = nullptr;
            nvml_return (*device_pcie_width)(nvml_device, unsigned int*) = nullptr;
            nvml_return (*device_max_clock)(nvml_device, int, unsigned int*) = nullptr;
            bool ready = false;
            string status;

            nvml_api() {
                try {
                    library_ptr_ = &library_;
                    init = library_.to_symbol<nvml_return (*)()>("nvmlInit_v2");
                    shutdown = library_.to_symbol<nvml_return (*)()>("nvmlShutdown");
                    device_count = library_.to_symbol<nvml_return (*)(unsigned int*)>("nvmlDeviceGetCount_v2");
                    device_by_index = library_.to_symbol<nvml_return (*)(unsigned int, nvml_device*)>(
                            "nvmlDeviceGetHandleByIndex_v2");
                    device_name =
                            library_.to_symbol<nvml_return (*)(nvml_device, char*, unsigned int)>("nvmlDeviceGetName");
                    device_uuid =
                            library_.to_symbol<nvml_return (*)(nvml_device, char*, unsigned int)>("nvmlDeviceGetUUID");
                    error_string = library_.to_symbol<const char* (*) (nvml_return)>("nvmlErrorString");
                    system_driver_version =
                            library_.to_symbol<nvml_return (*)(char*, unsigned int)>("nvmlSystemGetDriverVersion");
                    system_nvml_version =
                            library_.to_symbol<nvml_return (*)(char*, unsigned int)>("nvmlSystemGetNVMLVersion");
                    system_cuda_driver_version =
                            library_.to_symbol<nvml_return (*)(int*)>("nvmlSystemGetCudaDriverVersion_v2");
                    device_utilization = library_.to_symbol<nvml_return (*)(nvml_device, nvml_utilization*)>(
                            "nvmlDeviceGetUtilizationRates");
                    device_memory =
                            library_.to_symbol<nvml_return (*)(nvml_device, nvml_memory*)>("nvmlDeviceGetMemoryInfo");
                    device_temperature = library_.to_symbol<nvml_return (*)(nvml_device, int, unsigned int*)>(
                            "nvmlDeviceGetTemperature");
                    device_fan_speed = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int, unsigned int*)>(
                            "nvmlDeviceGetFanSpeed");
                    device_power_usage =
                            library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*)>("nvmlDeviceGetPowerUsage");
                    device_power_limit = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*)>(
                            "nvmlDeviceGetEnforcedPowerLimit");
                    device_clock = library_.to_symbol<nvml_return (*)(nvml_device, int, unsigned int*)>(
                            "nvmlDeviceGetClockInfo");
                    device_encoder = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, unsigned int*)>(
                            "nvmlDeviceGetEncoderUtilization");
                    device_decoder = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, unsigned int*)>(
                            "nvmlDeviceGetDecoderUtilization");
                    device_pcie_throughput = library_.to_symbol<nvml_return (*)(nvml_device, int, unsigned int*)>(
                            "nvmlDeviceGetPcieThroughput");
                    device_pstate =
                            library_.to_symbol<nvml_return (*)(nvml_device, int*)>("nvmlDeviceGetPerformanceState");
                    if (library_.has_symbol("nvmlDeviceGetComputeRunningProcesses_v3")) {
                        device_compute_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetComputeRunningProcesses_v3");
                    } else if (library_.has_symbol("nvmlDeviceGetComputeRunningProcesses_v2")) {
                        device_compute_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetComputeRunningProcesses_v2");
                    } else {
                        device_compute_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetComputeRunningProcesses");
                    }
                    if (library_.has_symbol("nvmlDeviceGetGraphicsRunningProcesses_v3")) {
                        device_graphics_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetGraphicsRunningProcesses_v3");
                    } else if (library_.has_symbol("nvmlDeviceGetGraphicsRunningProcesses_v2")) {
                        device_graphics_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetGraphicsRunningProcesses_v2");
                    } else {
                        device_graphics_processes =
                                library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*, nvml_process_info*)>(
                                        "nvmlDeviceGetGraphicsRunningProcesses");
                    }
                    if (library_.has_symbol("nvmlDeviceGetProcessUtilization")) {
                        device_process_utilization = library_.to_symbol<nvml_return (*)(
                                nvml_device, nvml_process_utilization*, unsigned int*, unsigned long long)>(
                                "nvmlDeviceGetProcessUtilization");
                    }
                    if (library_.has_symbol("nvmlDeviceGetVbiosVersion")) {
                        device_vbios = library_.to_symbol<nvml_return (*)(nvml_device, char*, unsigned int)>(
                                "nvmlDeviceGetVbiosVersion");
                    }
                    if (library_.has_symbol("nvmlDeviceGetCurrPcieLinkGeneration")) {
                        device_pcie_generation = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*)>(
                                "nvmlDeviceGetCurrPcieLinkGeneration");
                    }
                    if (library_.has_symbol("nvmlDeviceGetCurrPcieLinkWidth")) {
                        device_pcie_width = library_.to_symbol<nvml_return (*)(nvml_device, unsigned int*)>(
                                "nvmlDeviceGetCurrPcieLinkWidth");
                    }
                    if (library_.has_symbol("nvmlDeviceGetMaxClockInfo")) {
                        device_max_clock = library_.to_symbol<nvml_return (*)(nvml_device, int, unsigned int*)>(
                                "nvmlDeviceGetMaxClockInfo");
                    }
                    if (init == nullptr || device_count == nullptr || device_by_index == nullptr) {
                        status = "NVML unavailable: required symbols missing";
                        return;
                    }
                    const nvml_return result = init();
                    if (result != 0) {
                        status = format("NVML unavailable: {}",
                                        error_string != nullptr ? error_string(result) : "init failed");
                        return;
                    }
                    ready = true;
                    status = "NVML ready";
                } catch (const exception& error) {
                    status = format("NVML unavailable: {}", error.what());
                }
            }

            ~nvml_api() {
                if (ready && shutdown != nullptr) {
                    shutdown();
                }
            }

            string device_string(nvml_device device, unsigned int field) const {
                char buffer[256] = {};
                if (field == 0 && device_name != nullptr && device_name(device, buffer, sizeof(buffer)) == 0) {
                    return buffer;
                }
                if (field == 1 && device_uuid != nullptr && device_uuid(device, buffer, sizeof(buffer)) == 0) {
                    return buffer;
                }
                return {};
            }
        };

        struct cached_gpu_processes {
            string uuid;
            vector<gpu_process> processes;
            vector<string> errors;
            uint64_t sampled_at = 0;
        };

        map<uint32_t, cached_gpu_processes>& process_cache() {
            static map<uint32_t, cached_gpu_processes> cache;
            return cache;
        }

        map<uint32_t, uint64_t>& last_seen() {
            static map<uint32_t, uint64_t> seen;
            return seen;
        }

        vector<gpu_process> read_processes(nvml_api& api, uint32_t id, nvml_device device, bool live,
                                           vector<string>& errors) {
            auto& cache = process_cache();
            const string uuid = api.device_string(device, 1);
            auto& entry = cache[id];
            if (!entry.uuid.empty() && entry.uuid != uuid) {
                entry = cached_gpu_processes{};
            }
            if (!live && !entry.uuid.empty()) {
                errors = entry.errors;
                return entry.processes;
            }
            map<uint32_t, gpu_process> by_pid;
            const auto merge = [&by_pid](const char* kind, nvml_return result, unsigned int count,
                                         const vector<nvml_process_info>& buffer) {
                if (result != 0) {
                    return;
                }
                for (unsigned int index = 0; index < count; ++index) {
                    const auto& raw = buffer[index];
                    auto& item = by_pid[raw.pid];
                    item.pid_ = raw.pid;
                    if (raw.used_gpu_memory != 0xFFFFFFFFFFFFFFFFULL) {
                        item.memory_ = item.memory_.has_value()
                                               ? neforce::max(*item.memory_, static_cast<uint64_t>(raw.used_gpu_memory))
                                               : static_cast<uint64_t>(raw.used_gpu_memory);
                    }
                    item.kind_.append(kind);
                }
            };
            vector<string> process_errors;
            if (api.device_compute_processes != nullptr) {
                unsigned int count = 256;
                vector<nvml_process_info> buffer(count);
                const nvml_return result = api.device_compute_processes(device, &count, buffer.data());
                if (result == 0) {
                    merge("C", result, count, buffer);
                } else {
                    process_errors.push_back(
                            format("C: {}", api.error_string != nullptr ? api.error_string(result) : "failed"));
                }
            }
            if (api.device_graphics_processes != nullptr) {
                unsigned int count = 256;
                vector<nvml_process_info> buffer(count);
                const nvml_return result = api.device_graphics_processes(device, &count, buffer.data());
                if (result == 0) {
                    merge("G", result, count, buffer);
                } else {
                    process_errors.push_back(
                            format("G: {}", api.error_string != nullptr ? api.error_string(result) : "failed"));
                }
            }
            if (api.device_process_utilization != nullptr) {
                unsigned int count = 256;
                vector<nvml_process_utilization> samples(count);
                const unsigned long long since = last_seen()[id];
                const nvml_return result = api.device_process_utilization(device, samples.data(), &count, since);
                if (result == 0) {
                    uint64_t newest = since;
                    map<uint32_t, nvml_process_utilization> latest;
                    for (unsigned int index = 0; index < count; ++index) {
                        const auto& sample = samples[index];
                        if (sample.sm_util > 100 || sample.mem_util > 100 || sample.enc_util > 100 ||
                            sample.dec_util > 100 || sample.timestamp <= since) {
                            continue;
                        }
                        newest = neforce::max(newest, static_cast<uint64_t>(sample.timestamp));
                        auto& target = latest[sample.pid];
                        if (sample.timestamp >= target.timestamp) {
                            target = sample;
                        }
                    }
                    if (count != 0) {
                        last_seen()[id] = newest;
                    }
                    for (const auto& item: latest) {
                        const auto found = by_pid.find(item.first);
                        if (found == by_pid.end()) {
                            continue;
                        }
                        found->second.utilization_ = item.second.sm_util;
                        found->second.memory_utilization_ = item.second.mem_util;
                        found->second.encoder_ = item.second.enc_util;
                        found->second.decoder_ = item.second.dec_util;
                    }
                }
            }
            vector<gpu_process> processes;
            for (auto& item: by_pid) {
                processes.push_back(move(item.second));
            }
            stable_sort_by_index(processes, [](const gpu_process& a, const gpu_process& b) {
                return a.memory_.has_value() ? (!b.memory_.has_value() || *a.memory_ > *b.memory_) : false;
            });
            entry.uuid = uuid;
            entry.processes = processes;
            entry.errors = process_errors;
            entry.sampled_at = unix_time();
            errors = move(process_errors);
            return processes;
        }

        vector<gpu> read_nvml(nvml_api& api, bool live_processes, vector<string>& issues) {
            vector<gpu> out;
            char driver[80] = {};
            char nvml_version[80] = {};
            if (api.system_driver_version != nullptr) {
                api.system_driver_version(driver, sizeof(driver));
            }
            if (api.system_nvml_version != nullptr) {
                api.system_nvml_version(nvml_version, sizeof(nvml_version));
            }
            string cuda;
            if (api.system_cuda_driver_version != nullptr) {
                int version = 0;
                if (api.system_cuda_driver_version(&version) == 0) {
                    cuda = format("{}.{}", version / 1000, (version % 1000) / 10);
                }
            }
            unsigned int count = 0;
            const nvml_return result = api.device_count(&count);
            if (result != 0) {
                issues.push_back(format("NVML device discovery: {}",
                                        api.error_string != nullptr ? api.error_string(result) : "failed"));
                return out;
            }
            for (unsigned int id = 0; id < count; ++id) {
                nvml_device device = nullptr;
                if (api.device_by_index(id, &device) != 0) {
                    issues.push_back(format("GPU {}: unreadable", id));
                    continue;
                }
                gpu item;
                item.id_ = id;
                item.name_ = api.device_string(device, 0);
                if (item.name_.empty()) {
                    item.name_ = format("NVIDIA GPU {}", id);
                }
                item.vendor_ = "NVIDIA";
                item.backend_ = "NVML";
                item.uuid_ = api.device_string(device, 1);
                item.driver_ = driver;
                if (!cuda.empty()) {
                    item.cuda_driver_version_ = cuda;
                }
                if (nvml_version[0] != '\0') {
                    item.nvml_version_ = nvml_version;
                }
                if (api.device_utilization != nullptr) {
                    nvml_utilization utilization{};
                    if (api.device_utilization(device, &utilization) == 0) {
                        item.utilization_ = utilization.gpu;
                        item.memory_utilization_ = utilization.memory;
                    }
                }
                if (api.device_memory != nullptr) {
                    nvml_memory memory_value{};
                    if (api.device_memory(device, &memory_value) == 0) {
                        item.memory_used_ = memory_value.used;
                        item.memory_total_ = memory_value.total;
                    }
                }
                if (api.device_temperature != nullptr) {
                    unsigned int temperature = 0;
                    if (api.device_temperature(device, 0, &temperature) == 0) {
                        item.temperature_ = temperature;
                    }
                }
                if (api.device_fan_speed != nullptr) {
                    unsigned int fan = 0;
                    if (api.device_fan_speed(device, 0, &fan) == 0) {
                        item.fan_ = fan;
                    }
                }
                if (api.device_power_usage != nullptr) {
                    unsigned int power = 0;
                    if (api.device_power_usage(device, &power) == 0) {
                        item.power_watts_ = static_cast<float>(power) / 1000.0F;
                    }
                }
                if (api.device_power_limit != nullptr) {
                    unsigned int limit = 0;
                    if (api.device_power_limit(device, &limit) == 0) {
                        item.power_limit_watts_ = static_cast<float>(limit) / 1000.0F;
                    }
                }
                if (api.device_clock != nullptr) {
                    unsigned int clock = 0;
                    if (api.device_clock(device, 0, &clock) == 0) {
                        item.clock_mhz_ = clock;
                    }
                    if (api.device_clock(device, 2, &clock) == 0) {
                        item.memory_clock_mhz_ = clock;
                    }
                }
                if (api.device_encoder != nullptr) {
                    unsigned int utilization = 0;
                    unsigned int period = 0;
                    if (api.device_encoder(device, &utilization, &period) == 0) {
                        item.encoder_ = utilization;
                    }
                }
                if (api.device_decoder != nullptr) {
                    unsigned int utilization = 0;
                    unsigned int period = 0;
                    if (api.device_decoder(device, &utilization, &period) == 0) {
                        item.decoder_ = utilization;
                    }
                }
                if (api.device_pcie_throughput != nullptr) {
                    unsigned int value = 0;
                    if (api.device_pcie_throughput(device, 0, &value) == 0) {
                        item.pcie_rx_kib_ = value;
                    }
                    if (api.device_pcie_throughput(device, 1, &value) == 0) {
                        item.pcie_tx_kib_ = value;
                    }
                }
                if (api.device_pstate != nullptr) {
                    int state = -1;
                    if (api.device_pstate(device, &state) == 0 && state >= 0 && state < 16) {
                        item.performance_state_ = format("P{}", state);
                    }
                }
                item.extra_ = json::object();
                if (api.device_vbios != nullptr) {
                    char vbios[80] = {};
                    if (api.device_vbios(device, vbios, sizeof(vbios)) == 0) {
                        item.extra_.set("vbios_version", json(vbios));
                    }
                }
                if (api.device_pcie_generation != nullptr) {
                    unsigned int generation = 0;
                    if (api.device_pcie_generation(device, &generation) == 0) {
                        item.extra_.set("pcie_link_generation", json(generation));
                    }
                }
                if (api.device_pcie_width != nullptr) {
                    unsigned int width = 0;
                    if (api.device_pcie_width(device, &width) == 0) {
                        item.extra_.set("pcie_link_width", json(width));
                    }
                }
                if (api.device_max_clock != nullptr) {
                    unsigned int maximum = 0;
                    if (api.device_max_clock(device, 0, &maximum) == 0) {
                        item.extra_.set("max_clock_mhz", json(maximum));
                    }
                }
                vector<string> process_errors;
                item.processes_ = read_processes(api, id, device, live_processes, process_errors);
                const auto cached = process_cache().find(id);
                if (cached != process_cache().end()) {
                    item.extra_.set("process_sampled_at", json(cached->second.sampled_at));
                }
                if (!live_processes && process_errors.empty() && process_cache().find(id) == process_cache().end()) {
                    item.process_status_ = "Not sampled · open Processes or GPU";
                } else if (process_errors.empty()) {
                    item.process_status_ = "compute + graphics";
                } else {
                    string joined;
                    for (const auto& error: process_errors) {
                        if (!joined.empty()) {
                            joined.append("; ");
                        }
                        joined.append(error);
                    }
                    item.process_status_ = joined;
                }
                out.push_back(move(item));
            }
            return out;
        }

        struct drm_client_totals {
            uint64_t engine_ns = 0;
            uint64_t memory_bytes = 0;
        };

        struct drm_engine_state {
            uint64_t engine_ns = 0;
            uint64_t memory_bytes = 0;
            double sampled_at = 0.0;
            bool valid = false;
            uint32_t utilization = 0;
        };

        map<string, drm_engine_state>& drm_states() {
            static map<string, drm_engine_state> states;
            return states;
        }

        optional<drm_client_totals> read_drm_clients(const string& pci_id) {
            map<string, drm_client_totals> clients;
            for (const auto& proc_entry: ::neforce::path("/proc").children()) {
                const string entry = proc_entry.str();
                const auto slash = entry.rfind('/');
                const string name = slash == string::npos ? entry : entry.substr(slash + 1);
                if (!parse_u64(name.view()).has_value()) {
                    continue;
                }
                for (const auto& fd_entry: ::neforce::path(entry + "/fdinfo").children()) {
                    const string fd_path = fd_entry.str();
                    const auto text = read_text_file(fd_path);
                    if (!text.has_value() || text->find("drm-pdev:") == string::npos) {
                        continue;
                    }
                    string client;
                    uint64_t engine_ns = 0;
                    uint64_t memory_bytes = 0;
                    bool matches = false;
                    for (const auto& line: split_lines(*text)) {
                        if (starts_with(line.view(), "drm-pdev:")) {
                            matches = trim(line.view().substr(9)) == pci_id;
                        } else if (starts_with(line.view(), "drm-client-id:")) {
                            client = trim(line.view().substr(14));
                        } else if (starts_with(line.view(), "drm-total-") && line.find(" KiB") != string::npos) {
                            const auto colon = line.find(':');
                            const auto value = colon == string::npos
                                                       ? none
                                                       : parse_u64(trim(line.view().substr(colon + 1)).view());
                            if (value.has_value()) {
                                memory_bytes += *value * 1024;
                            }
                        } else if (starts_with(line.view(), "drm-engine-") && ends_with(line.view(), " ns")) {
                            const auto colon = line.find(':');
                            const auto value = colon == string::npos
                                                       ? none
                                                       : parse_u64(trim(line.view().substr(colon + 1)).view());
                            if (value.has_value()) {
                                engine_ns += *value;
                            }
                        }
                    }
                    if (!matches || client.empty()) {
                        continue;
                    }
                    auto& total = clients[client];
                    total.engine_ns = neforce::max(total.engine_ns, engine_ns);
                    total.memory_bytes = neforce::max(total.memory_bytes, memory_bytes);
                }
            }
            if (clients.empty()) {
                return none;
            }
            drm_client_totals total;
            for (const auto& client: clients) {
                total.engine_ns += client.second.engine_ns;
                total.memory_bytes += client.second.memory_bytes;
            }
            return total;
        }

        void update_drm_state(gpu& item, const string& pci_id) {
            auto& state = drm_states()[pci_id];
            const auto current = read_drm_clients(pci_id);
            const double now = static_cast<double>(monotonic_ms());
            if (!current.has_value()) {
                return;
            }
            if (state.valid) {
                const double elapsed = neforce::max(1.0, now - state.sampled_at);
                if (current->engine_ns > state.engine_ns) {
                    const double busy =
                            static_cast<double>(current->engine_ns - state.engine_ns) / (elapsed * 1'000'000.0) * 100.0;
                    state.utilization = static_cast<uint32_t>(neforce::clamp(busy, 0.0, 100.0));
                } else {
                    state.utilization = 0;
                }
            }
            state.engine_ns = current->engine_ns;
            state.memory_bytes = current->memory_bytes;
            state.sampled_at = now;
            state.valid = true;
            if (!item.utilization_.has_value() || *item.utilization_ == 0) {
                item.utilization_ = state.utilization;
            }
            if (!item.memory_used_.has_value() && current->memory_bytes > 0) {
                item.memory_used_ = current->memory_bytes;
            }
            item.extra_.set("drm_engine_ns", json(current->engine_ns));
            if (current->memory_bytes > 0) {
                item.extra_.set("drm_client_memory_bytes", json(current->memory_bytes));
            }
        }

        vector<gpu> read_sysfs(uint32_t first_id) {
#if NEFLUX_PLATFORM_LINUX
            vector<gpu> out;
            vector<string> cards;
            for (const auto& full: ::neforce::path("/sys/class/drm").children()) {
                const auto slash = full.str().rfind('/');
                const string name = slash == string::npos ? full.str() : full.str().substr(slash + 1);
                if (starts_with(name.view(), "card") && name.size() > 4) {
                    bool numeric = true;
                    for (size_t i = 4; i < name.size(); ++i) {
                        if (name[i] < '0' || name[i] > '9') {
                            numeric = false;
                            break;
                        }
                    }
                    if (numeric) {
                        cards.push_back(full.str());
                    }
                }
            }
            stable_sort_by_index(cards, [](const string& a, const string& b) { return a < b; });
            set<string> known_pci;
            for (const auto& card: cards) {
                const auto canonical = ::neforce::path(card + "/device").canonical().str();
                if (!canonical.empty()) {
                    known_pci.insert(device_name_of(canonical));
                }
            }
            for (const auto& card: cards) {
                const string device = card + "/device";
                const string vendor =
                        trim(read_text_file(device + "/vendor").has_value() ? *read_text_file(device + "/vendor")
                                                                            : string());
                if (vendor.empty()) {
                    continue;
                }
                const auto driver_path = ::neforce::path(device + "/driver").canonical().str();
                if (driver_path.find("simple-framebuffer") != string::npos) {
                    continue;
                }
                if (vendor == "0x10de" && first_id > 0) {
                    continue;
                }
                string vendor_name = "DRM";
                if (vendor == "0x1002") {
                    vendor_name = "AMD";
                } else if (vendor == "0x8086") {
                    vendor_name = "Intel";
                } else if (vendor == "0x10de") {
                    vendor_name = "NVIDIA";
                } else if (vendor == "0x5143") {
                    vendor_name = "Qualcomm";
                } else if (vendor == "0x14e4") {
                    vendor_name = "Broadcom";
                } else if (vendor == "0x13b5") {
                    vendor_name = "Arm";
                }
                gpu item;
                item.id_ = first_id + static_cast<uint32_t>(out.size());
                string model = trim(read_text_file(device + "/product_name").has_value()
                                            ? *read_text_file(device + "/product_name")
                                            : string());
                if (model.empty()) {
                    const auto id_text = read_text_file(device + "/device");
                    if (vendor_name == "Intel") {
                        model = format("Intel Graphics ({})",
                                       id_text.has_value() ? trim(id_text->view()) : string("integrated"));
                    } else {
                        model = format("{} {}", vendor_name, id_text.has_value() ? trim(id_text->view()) : string());
                    }
                }
                item.name_ = model;
                item.vendor_ = vendor_name;
                item.backend_ = "DRM sysfs";
                const auto canonical = ::neforce::path(device).canonical().str();
                item.uuid_ = canonical.empty() ? device : device_name_of(canonical);
                const auto driver = move(driver_path);
                if (!driver.empty()) {
                    item.driver_ = format("{} / kernel {}", device_name_of(driver), kernel_version());
                }
                item.extra_ = json::object();
                item.extra_.set("pci_bus_id", json(device_name_of(canonical)));
                if (const auto shared_used = read_number(device + "/mem_info_gtt_used"); shared_used.has_value()) {
                    item.extra_.set("shared_memory_used_bytes", json(*shared_used));
                }
                if (const auto shared_total = read_number(device + "/mem_info_gtt_total"); shared_total.has_value()) {
                    item.extra_.set("shared_memory_total_bytes", json(*shared_total));
                }
                if (const auto speed = read_text_file(device + "/current_link_speed"); speed.has_value()) {
                    item.extra_.set("pcie_link_speed", json(trim(speed->view())));
                }
                if (const auto width = read_number(device + "/current_link_width"); width.has_value()) {
                    item.extra_.set("pcie_link_width", json(*width));
                }
                if (const auto busy = read_number(device + "/gpu_busy_percent"); busy.has_value()) {
                    item.utilization_ = static_cast<uint32_t>(*busy);
                }
                if (const auto used = read_number(device + "/mem_info_vram_used"); used.has_value()) {
                    item.memory_used_ = *used;
                }
                if (const auto total = read_number(device + "/mem_info_vram_total"); total.has_value()) {
                    item.memory_total_ = *total;
                }
                for (uint32_t hwmon = 0; hwmon < 8; ++hwmon) {
                    const string base = device + format("/hwmon/hwmon{}", hwmon);
                    if (const auto temperature = read_number(base + "/temp1_input"); temperature.has_value()) {
                        item.temperature_ = static_cast<uint32_t>(*temperature / 1000);
                    }
                    if (const auto power = read_number(base + "/power1_average"); power.has_value()) {
                        item.power_watts_ = static_cast<float>(*power) / 1'000'000.0F;
                    }
                }
                if (const auto clock = read_number(card + "/gt_cur_freq_mhz"); clock.has_value()) {
                    item.clock_mhz_ = static_cast<uint32_t>(*clock);
                }
                if (vendor_name == "AMD") {
                    if (const auto clock = read_dpm_clock(device + "/pp_dpm_sclk"); clock.has_value()) {
                        item.clock_mhz_ = clock;
                    }
                    if (const auto clock = read_dpm_clock(device + "/pp_dpm_mclk"); clock.has_value()) {
                        item.memory_clock_mhz_ = clock;
                    }
                    const auto level = read_text_file(device + "/power_dpm_force_performance_level");
                    if (level.has_value() && !trim(level->view()).empty()) {
                        item.performance_state_ = trim(level->view());
                    }
                }
                for (uint32_t hwmon = 0; hwmon < 8; ++hwmon) {
                    const string base = device + format("/hwmon/hwmon{}", hwmon);
                    if (const auto cap = read_number(base + "/power1_cap"); cap.has_value()) {
                        item.power_limit_watts_ = static_cast<float>(*cap) / 1'000'000.0F;
                    }
                    if (const auto fan = read_number(base + "/fan1_input"); fan.has_value()) {
                        item.extra_.set("fan_rpm", json(*fan));
                    }
                }
                update_drm_state(item, device_name_of(canonical));
                if (vendor_name == "Intel") {
                    item.memory_used_ = item.memory_used_.value_or(0);
                    if (!item.memory_total_.has_value()) {
                        const auto meminfo = read_text_file("/proc/meminfo");
                        if (meminfo.has_value()) {
                            for (const auto& line: split_lines(*meminfo)) {
                                if (!starts_with(line.view(), "MemTotal:")) {
                                    continue;
                                }
                                const auto colon = line.find(':');
                                const auto parts = colon == string::npos
                                                           ? vector<string>{}
                                                           : split_whitespace(line.view().substr(colon + 1));
                                const auto value = parts.empty() ? none : parse_u64(parts[0].view());
                                if (value.has_value()) {
                                    item.memory_total_ = *value * 1024;
                                    item.extra_.set("memory_kind", json("shared"));
                                }
                                break;
                            }
                        }
                    }
                }
                item.process_status_ = "DRM fdinfo engine utilization · two-sample average";
                out.push_back(move(item));
            }
            for (const auto& entry: ::neforce::path("/sys/bus/pci/devices").children()) {
                const string device = entry.str();
                const string pci = device_name_of(device);
                if (known_pci.count(pci) != 0) {
                    continue;
                }
                const auto vendor_text = read_text_file(device + "/vendor");
                const auto class_text = read_text_file(device + "/class");
                if (!vendor_text.has_value() || !class_text.has_value()) {
                    continue;
                }
                const string vendor = trim(vendor_text->view());
                const string device_class = trim(class_text->view());
                if (!starts_with(device_class.view(), "0x03")) {
                    continue;
                }
                string vendor_name;
                if (vendor == "0x10de") {
                    vendor_name = "NVIDIA";
                } else if (vendor == "0x1002") {
                    vendor_name = "AMD";
                } else if (vendor == "0x8086") {
                    vendor_name = "Intel";
                } else {
                    continue;
                }
                const auto id_text = read_text_file(device + "/device");
                const string id = id_text.has_value() ? trim(id_text->view()) : string("unknown");
                gpu item;
                item.id_ = first_id + static_cast<uint32_t>(out.size());
                item.name_ = format("{} GPU ({})", vendor_name, id);
                item.vendor_ = vendor_name;
                item.backend_ = "PCI sysfs";
                item.uuid_ = pci;
                item.driver_ = "driver not loaded";
                item.extra_ = json::object();
                item.extra_.set("pci_bus_id", json(pci));
                item.extra_.set("pci_id", json(format("{}:{}", vendor, id)));
                item.process_status_ = "Driver not loaded; telemetry unavailable";
                out.push_back(move(item));
            }
            return out;
#else
            static_cast<void>(first_id);
            return {};
#endif
        }

#if NEFLUX_PLATFORM_WINDOWS
        vector<gpu> read_dxgi(uint32_t first_id) {
            vector<gpu> out;
            IDXGIFactory* factory = nullptr;
            if (CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)) != S_OK ||
                factory == nullptr) {
                return out;
            }
            IDXGIAdapter* adapter = nullptr;
            set<string> seen;
            for (UINT index = 0; factory->EnumAdapters(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
                if (adapter == nullptr) {
                    continue;
                }
                DXGI_ADAPTER_DESC description{};
                if (adapter->GetDesc(&description) == S_OK) {
                    const string name = wcharacter::to_string(wstring_view(description.Description));
                    if (name.empty() || name.contains("Microsoft Basic Render Driver") ||
                        description.DedicatedVideoMemory == 0) {
                        adapter->Release();
                        adapter = nullptr;
                        continue;
                    }
                    gpu item;
                    item.id_ = first_id + static_cast<uint32_t>(out.size());
                    item.name_ = name;
                    item.vendor_ = description.VendorId == 0x1002
                                           ? "AMD"
                                           : (description.VendorId == 0x10DE
                                                      ? "NVIDIA"
                                                      : (description.VendorId == 0x8086 ? "Intel" : "DRM"));
                    item.backend_ = "DXGI";
                    item.memory_total_ = description.DedicatedVideoMemory;
                    const string pci_id = format("{:04x}:{:04x}:{:08x}:{:02x}", description.VendorId,
                                                 description.DeviceId, description.SubSysId, description.Revision);
                    item.uuid_ = pci_id;
                    item.extra_ = json::object();
                    item.extra_.set("pci_id", json(pci_id));
                    item.extra_.set("shared_memory_bytes", json(static_cast<uint64_t>(description.SharedSystemMemory)));
                    item.process_status_ = "DXGI adapter · per-process counters unavailable";
                    const string identity = gpu_identity(item);
                    if (!seen.insert(identity).second) {
                        adapter->Release();
                        adapter = nullptr;
                        continue;
                    }
                    out.push_back(move(item));
                }
                adapter->Release();
                adapter = nullptr;
            }
            factory->Release();
            return out;
        }
#endif

    } // namespace

    gpu_result collect_gpus(bool enabled, bool live_processes) {
        gpu_result result;
        if (!enabled) {
            result.status_ = "GPU collection disabled (--no-gpu)";
            return result;
        }
        vector<string> issues;
        try {
            static nvml_api api;
            if (api.ready) {
                result.devices_ = read_nvml(api, live_processes, issues);
            } else {
                issues.push_back(api.status);
            }
        } catch (const exception& error) {
            issues.push_back(format("NVML unavailable: {}", error.what()));
        }
#if NEFLUX_PLATFORM_WINDOWS
        auto platform_devices = read_dxgi(static_cast<uint32_t>(result.devices_.size()));
#else
        auto platform_devices = read_sysfs(static_cast<uint32_t>(result.devices_.size()));
#endif
        set<string> known_devices;
        for (const auto& device: result.devices_) {
            known_devices.insert(gpu_identity(device));
        }
        for (auto& device: platform_devices) {
            const string identity = gpu_identity(device);
            if (!known_devices.insert(identity).second) {
                continue;
            }
            result.devices_.push_back(move(device));
        }
        for (size_t index = 0; index < result.devices_.size(); ++index) {
            result.devices_[index].id_ = static_cast<uint32_t>(index);
        }
        if (result.devices_.empty()) {
            if (issues.empty()) {
                result.status_ = "No readable GPU telemetry.";
            } else {
                result.status_ = issues[0];
                for (size_t i = 1; i < issues.size(); ++i) {
                    result.status_.append("; ");
                    result.status_.append(issues[i]);
                }
            }
        } else {
            result.status_ = format("{} device(s) · DRM/PCI telemetry", result.devices_.size());
            if (!issues.empty()) {
                result.status_.append(" · ");
                result.status_.append(issues[0]);
            }
        }
        return result;
    }

    vector<gpu> select_gpus(const vector<gpu>& devices, const gpu_selection& selection) {
        vector<gpu> candidates;
        for (const auto& device: devices) {
            if (!device.memory_total_.has_value() || !device.memory_used_.has_value()) {
                continue;
            }
            if (device.memory_limit_.has_value() && *device.memory_limit_ == 0) {
                continue;
            }
            const uint64_t free_bytes = *device.memory_total_ - *device.memory_used_;
            if (free_bytes < selection.min_free_bytes_) {
                continue;
            }
            if (*device.memory_total_ < selection.min_total_bytes_) {
                continue;
            }
            if (selection.max_gpu_utilization_.has_value() &&
                (!device.utilization_.has_value() || *device.utilization_ > *selection.max_gpu_utilization_)) {
                continue;
            }
            if (selection.max_memory_utilization_.has_value() &&
                (!device.memory_utilization_.has_value() ||
                 *device.memory_utilization_ > *selection.max_memory_utilization_)) {
                continue;
            }
            candidates.push_back(device);
        }
        stable_sort_by_index(candidates, [](const gpu& a, const gpu& b) {
            const uint32_t left = a.utilization_.has_value() ? *a.utilization_ : 101;
            const uint32_t right = b.utilization_.has_value() ? *b.utilization_ : 101;
            if (left != right) {
                return left < right;
            }
            const uint32_t left_mem = a.memory_utilization_.has_value() ? *a.memory_utilization_ : 101;
            const uint32_t right_mem = b.memory_utilization_.has_value() ? *b.memory_utilization_ : 101;
            return left_mem < right_mem;
        });
        return candidates;
    }

} // namespace neforce::neflux
