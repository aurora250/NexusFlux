#include <NeFlux/common.hpp>
#include <NeForce/core/string/string_view.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include "platform/platform_backend.hpp"
#include <NeFlux/util.hpp>
#include <NeForce/core/container/map.hpp>
#include <NeForce/core/system/dynamic_library.hpp>
namespace neforce::neflux {

    namespace {

        string gpu_identity(const gpu& item) {
            string name = lower_ascii(item.name_.view());
            name.trim();
            string vendor = lower_ascii(item.vendor_.view());
            vendor.trim();
            return format("{}|{}", vendor, name);
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
            dynamic_library library_{platform::nvml_library_name(), dynamic_library::load_mode::now};
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
            neforce::tim_sort(processes.begin(), processes.end(), [](const gpu_process& a, const gpu_process& b) {
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
        auto platform_devices = platform::read_platform_gpus(static_cast<uint32_t>(result.devices_.size()));
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
        neforce::tim_sort(candidates.begin(), candidates.end(), [](const gpu& a, const gpu& b) {
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
