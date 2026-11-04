#include "../platform_backend.hpp"
#include <NeFlux/gpu.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/container/map.hpp>
#include <NeForce/core/container/set.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/format.hpp>
#include <sys/utsname.h>

namespace neforce::neflux::platform {

    string nvml_library_name() { return "libnvidia-ml.so.1"; }

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

        struct drm_process_state {
            uint64_t engine_ns = 0;
            uint64_t memory_bytes = 0;
            double sampled_at = 0.0;
            bool valid = false;
        };

        map<string, map<uint32_t, drm_process_state>>& drm_process_states() {
            static map<string, map<uint32_t, drm_process_state>> states;
            return states;
        }

        optional<map<uint32_t, drm_client_totals>> read_drm_clients(const string& pci_id) {
            map<string, drm_client_totals> clients;
            map<string, uint32_t> client_pids;
            for (const auto& proc_entry: ::neforce::path("/proc").children()) {
                const string entry = proc_entry.str();
                const auto slash = entry.rfind('/');
                const string name = slash == string::npos ? entry : entry.substr(slash + 1);
                const auto pid = parse_u64(name.view());
                if (!pid.has_value()) {
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
                    const string key = format("{}|{}", *pid, client);
                    auto& total = clients[key];
                    total.engine_ns = neforce::max(total.engine_ns, engine_ns);
                    total.memory_bytes = neforce::max(total.memory_bytes, memory_bytes);
                    client_pids[key] = static_cast<uint32_t>(*pid);
                }
            }
            if (clients.empty()) {
                return none;
            }
            map<uint32_t, drm_client_totals> by_pid;
            for (const auto& client: clients) {
                auto& total = by_pid[client_pids[client.first]];
                total.engine_ns += client.second.engine_ns;
                total.memory_bytes += client.second.memory_bytes;
            }
            return by_pid;
        }

        void append_drm_processes(gpu& item, const string& pci_id, const map<uint32_t, drm_client_totals>& clients) {
            auto& states = drm_process_states()[pci_id];
            const auto now = static_cast<double>(monotonic_ms());
            set<uint32_t> present;
            for (const auto& client: clients) {
                const uint32_t pid = client.first;
                present.insert(pid);
                auto& state = states[pid];
                gpu_process process;
                process.pid_ = pid;
                process.kind_ = "G";
                if (client.second.memory_bytes > 0) {
                    process.memory_ = client.second.memory_bytes;
                }
                if (state.valid) {
                    const double elapsed = neforce::max(1.0, now - state.sampled_at);
                    if (client.second.engine_ns > state.engine_ns) {
                        const double busy = static_cast<double>(client.second.engine_ns - state.engine_ns) /
                                            (elapsed * 1'000'000.0) * 100.0;
                        process.utilization_ = static_cast<uint32_t>(neforce::clamp(busy, 0.0, 100.0));
                    } else {
                        process.utilization_ = 0;
                    }
                }
                state.engine_ns = client.second.engine_ns;
                state.memory_bytes = client.second.memory_bytes;
                state.sampled_at = now;
                state.valid = true;
                item.processes_.push_back(move(process));
            }
            for (auto iterator = states.begin(); iterator != states.end();) {
                if (present.count(iterator->first) == 0) {
                    auto next = iterator;
                    ++next;
                    states.erase(iterator);
                    iterator = next;
                } else {
                    ++iterator;
                }
            }
            neforce::tim_sort(item.processes_.begin(), item.processes_.end(),
                              [](const gpu_process& left, const gpu_process& right) {
                                  return left.memory_.value_or(0) > right.memory_.value_or(0);
                              });
        }

        optional<map<uint32_t, drm_client_totals>> update_drm_state(gpu& item, const string& pci_id) {
            auto& state = drm_states()[pci_id];
            const auto clients = read_drm_clients(pci_id);
            const double now = static_cast<double>(monotonic_ms());
            if (!clients.has_value()) {
                return none;
            }
            drm_client_totals current;
            for (const auto& client: *clients) {
                current.engine_ns += client.second.engine_ns;
                current.memory_bytes += client.second.memory_bytes;
            }
            if (state.valid) {
                const double elapsed = neforce::max(1.0, now - state.sampled_at);
                if (current.engine_ns > state.engine_ns) {
                    const double busy =
                            static_cast<double>(current.engine_ns - state.engine_ns) / (elapsed * 1'000'000.0) * 100.0;
                    state.utilization = static_cast<uint32_t>(neforce::clamp(busy, 0.0, 100.0));
                } else {
                    state.utilization = 0;
                }
            }
            state.engine_ns = current.engine_ns;
            state.memory_bytes = current.memory_bytes;
            state.sampled_at = now;
            state.valid = true;
            if (!item.utilization_.has_value() || *item.utilization_ == 0) {
                item.utilization_ = state.utilization;
            }
            if (!item.memory_used_.has_value() && current.memory_bytes > 0) {
                item.memory_used_ = current.memory_bytes;
            }
            item.extra_.set("drm_engine_ns", json(current.engine_ns));
            if (current.memory_bytes > 0) {
                item.extra_.set("drm_client_memory_bytes", json(current.memory_bytes));
            }
            return clients;
        }

        vector<gpu> read_platform_gpus(uint32_t first_id) {
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
            neforce::tim_sort(cards.begin(), cards.end(), [](const string& a, const string& b) { return a < b; });
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
                const auto drm_clients = update_drm_state(item, device_name_of(canonical));
                if (drm_clients.has_value()) {
                    append_drm_processes(item, device_name_of(canonical), *drm_clients);
                }
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
                                    item.unified_memory_ = true;
                                    item.extra_.set("memory_kind", json("shared"));
                                }
                                break;
                            }
                        }
                    }
                }
                item.process_status_ = "DRM fdinfo per-process engine/memory · two-sample average";
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
        }

} // namespace neforce::neflux::platform
