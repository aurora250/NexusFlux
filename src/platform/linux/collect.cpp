#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#include <NeForce/core/container/flat_unordered_map.hpp>
#include <NeForce/core/container/set.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#include <sys/statvfs.h>

namespace neforce::neflux::platform {


        string read_file(const string& file_path, size_t limit = 0) {
            auto text = read_text_file(file_path);
            if (!text.has_value()) {
                return {};
            }
            if (limit != 0 && text->size() > limit) {
                text->resize(limit);
            }
            return *text;
        }
        uint64_t cpu_frequency_mhz() {
            uint64_t total = 0;
            uint64_t count = 0;
            for (uint32_t index = 0; index < 1024; ++index) {
                const string base = format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", index);
                const auto value = parse_u64(trim(read_file(base)).view());
                if (!value.has_value()) {
                    break;
                }
                total += *value / 1000;
                ++count;
            }
            if (count != 0) {
                return total / count;
            }
            const auto text = read_file("/proc/cpuinfo");
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "cpu MHz")) {
                    const auto value = parse_f64(trim(line.view().substr(line.find(':') + 1)).view());
                    if (value.has_value()) {
                        return static_cast<uint64_t>(*value);
                    }
                }
            }
            return 0;
        }
        vector<topology_entry> read_topology(size_t count) {
            vector<topology_entry> out;
            out.reserve(count);
            for (size_t index = 0; index < count; ++index) {
                topology_entry entry;
                const string base = format("/sys/devices/system/cpu/cpu{}/topology/", index);
                const auto package = parse_u64(trim(read_file(base + "physical_package_id")).view());
                const auto core = parse_u64(trim(read_file(base + "core_id")).view());
                if (package.has_value() && core.has_value()) {
                    entry.package = static_cast<uint32_t>(*package);
                    entry.core = static_cast<uint32_t>(*core);
                }
                out.push_back(entry);
            }
            return out;
        }
        vector<optional<float>> core_temperatures(const vector<topology_entry>& topology) {
            map<pair<uint32_t, uint32_t>, float> temperatures;
            for (uint32_t index = 0; index < 64; ++index) {
                const string base = format("/sys/class/hwmon/hwmon{}/", index);
                const auto name = trim(read_file(base + "name"));
                if (name != "coretemp") {
                    continue;
                }
                optional<uint32_t> package;
                flat_unordered_map<string, uint32_t> core_labels;
                flat_unordered_map<string, string> inputs;
                for (uint32_t sensor = 1; sensor < 64; ++sensor) {
                    const string label_name = format("temp{}_label", sensor);
                    const auto label = trim(read_file(base + label_name));
                    if (label.empty()) {
                        continue;
                    }
                    if (starts_with(label.view(), "Package id ")) {
                        const auto value = parse_u64(label.view().substr(11));
                        if (value.has_value()) {
                            package = static_cast<uint32_t>(*value);
                        }
                    } else if (starts_with(label.view(), "Core ")) {
                        const auto value = parse_u64(label.view().substr(5));
                        if (value.has_value()) {
                            core_labels[label_name] = static_cast<uint32_t>(*value);
                        }
                    }
                    inputs[label_name] = base + format("temp{}_input", sensor);
                }
                if (!package.has_value()) {
                    continue;
                }
                for (const auto& entry: core_labels) {
                    const auto raw = parse_f64(trim(read_file(inputs[entry.first])).view());
                    if (raw.has_value() && is_finite(*raw)) {
                        temperatures[{*package, entry.second}] = static_cast<float>(*raw / 1000.0);
                    }
                }
            }
            vector<optional<float>> out;
            out.reserve(topology.size());
            for (const auto& entry: topology) {
                if (entry.package.has_value() && entry.core.has_value()) {
                    const auto found = temperatures.find({*entry.package, *entry.core});
                    if (found != temperatures.end()) {
                        out.push_back(found->second);
                        continue;
                    }
                }
                out.push_back(none);
            }
            return out;
        }
        proc_meminfo read_meminfo() {
            proc_meminfo info;
            const auto text = read_file("/proc/meminfo");
            uint64_t cached = 0;
            bool cached_seen = false;
            for (const auto& line: split_lines(text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 2) {
                    continue;
                }
                const auto value = parse_u64(parts[1].view());
                if (!value.has_value()) {
                    continue;
                }
                const uint64_t bytes_value = *value * 1024;
                if (parts[0] == "MemTotal:") {
                    info.total = bytes_value;
                } else if (parts[0] == "MemAvailable:") {
                    info.available = bytes_value;
                } else if (parts[0] == "MemFree:") {
                    info.free = bytes_value;
                } else if (parts[0] == "Cached:" || parts[0] == "SReclaimable:") {
                    cached += bytes_value;
                    cached_seen = true;
                } else if (parts[0] == "SwapTotal:") {
                    info.swap_total = bytes_value;
                } else if (parts[0] == "SwapFree:") {
                    info.swap_free = bytes_value;
                }
            }
            if (cached_seen) {
                info.cached = cached;
            }
            return info;
        }
        vector<string> list_directory(const string& directory) {
            vector<string> out;
            const auto entries = path(directory).children();
            for (const auto& entry: entries) {
                out.push_back(entry.str());
            }
            return out;
        }
        string device_name(const string& source) {
            const auto slash = source.rfind('/');
            return slash == string::npos ? source : source.substr(slash + 1);
        }
        string block_parent(const string& device) {
            string parent = device;
            while (!parent.empty() && parent[parent.size() - 1] >= '0' && parent[parent.size() - 1] <= '9') {
                parent.resize(parent.size() - 1);
            }
            if (!parent.empty() && parent[parent.size() - 1] == 'p' && parent.size() > 1) {
                parent.resize(parent.size() - 1);
            }
            return parent.empty() ? device : parent;
        }
        string block_kind(const string& device) {
            const string parent = block_parent(device);
            const auto rotational = trim(read_file("/sys/class/block/" + parent + "/queue/rotational"));
            if (rotational == "0") {
                return "SSD";
            }
            if (rotational == "1") {
                return "HDD";
            }
            return "Unknown";
        }
        bool block_removable(const string& device) {
            const string parent = block_parent(device);
            return trim(read_file("/sys/class/block/" + parent + "/removable")) == "1";
        }
        json statvfs_fields(const string& mount) {
            json fields = json::object();
            struct ::statvfs info{};
            if (::statvfs(mount.data(), &info) == 0) {
                fields.set("inodes_total", json(info.f_files));
                fields.set("inodes_free", json(info.f_ffree));
                fields.set("block_size_bytes", json(info.f_frsize));
                fields.set("readonly", json((info.f_flag & ST_RDONLY) != 0));
            }
            return fields;
        }
        vector<disk> read_disks() {
            vector<disk> out;
            set<string> seen;
            for (const auto& mount_point: sysinfo::mount_points()) {
                if (!seen.insert(mount_point.target).second) {
                    continue;
                }
                const string& source = mount_point.source;
                const string& filesystem = mount_point.filesystem;
                const string source_name = device_name(source);
                if (!starts_with(source.view(), "/dev/") || starts_with(source_name.view(), "loop") ||
                    filesystem == "squashfs" || filesystem == "iso9660" || filesystem == "devtmpfs" ||
                    filesystem == "proc" || filesystem == "sysfs" || filesystem == "efivarfs") {
                    continue;
                }
                if (mount_point.total_bytes == 0) {
                    continue;
                }
                disk item;
                item.name_ = source;
                item.mount_ = mount_point.target;
                item.filesystem_ = filesystem;
                item.kind_ = block_kind(source_name);
                item.removable_ = block_removable(source_name);
                item.available_ = mount_point.available_bytes;
                item.total_ = mount_point.total_bytes;
                item.used_ = item.total_ > item.available_ ? item.total_ - item.available_ : 0;
                item.extra_ = statvfs_fields(item.mount_);
                out.push_back(move(item));
            }
            neforce::tim_sort(out.begin(), out.end(), [](const disk& a, const disk& b) { return a.mount_ < b.mount_; });
            return out;
        }
        vector<sensor> read_sensors() {
            vector<sensor> out;
            for (uint32_t index = 0; index < 64; ++index) {
                const string base = format("/sys/class/hwmon/hwmon{}/", index);
                const auto chip = trim(read_file(base + "name"));
                if (chip.empty()) {
                    continue;
                }
                for (uint32_t channel = 1; channel < 64; ++channel) {
                    const auto raw = parse_f64(trim(read_file(base + format("temp{}_input", channel))).view());
                    if (!raw.has_value() || !is_finite(*raw)) {
                        continue;
                    }
                    sensor item;
                    const auto label = trim(read_file(base + format("temp{}_label", channel)));
                    item.name_ = label.empty() ? format("{} temp{}", chip, channel) : label;
                    item.temperature_ = static_cast<float>(*raw / 1000.0);
                    const auto critical = parse_f64(trim(read_file(base + format("temp{}_crit", channel))).view());
                    if (critical.has_value()) {
                        item.critical_ = static_cast<float>(*critical / 1000.0);
                    }
                    out.push_back(move(item));
                }
            }
            return out;
        }
        vector<battery> read_batteries() {
            vector<battery> out;
            uint32_t index = 0;
            for (const auto& full: list_directory("/sys/class/power_supply")) {
                const auto slash = full.rfind('/');
                const string name = slash == string::npos ? full : full.substr(slash + 1);
                const string base = "/sys/class/power_supply/" + name + "/";
                if (trim(read_file(base + "type")) != "Battery") {
                    continue;
                }
                battery item;
                const auto model = trim(read_file(base + "model_name"));
                item.name_ = model.empty() ? format("Battery {}", index) : model;
                const auto capacity = parse_f64(trim(read_file(base + "capacity")).view());
                if (capacity.has_value()) {
                    item.percent_ = static_cast<float>(*capacity);
                }
                const auto status = trim(read_file(base + "status"));
                item.status_ = status.empty() ? "Unknown" : status;
                const auto power_now = parse_f64(trim(read_file(base + "power_now")).view());
                const auto current_now = parse_f64(trim(read_file(base + "current_now")).view());
                const auto voltage_now = parse_f64(trim(read_file(base + "voltage_now")).view());
                double watts = 0.0;
                bool has_watts = false;
                if (power_now.has_value()) {
                    watts = *power_now / 1'000'000.0;
                    has_watts = true;
                } else if (current_now.has_value() && voltage_now.has_value()) {
                    watts = *current_now * *voltage_now / 1'000'000'000'000.0;
                    has_watts = true;
                }
                if (has_watts) {
                    if (item.status_ == "Discharging") {
                        watts = -absolute(watts);
                    } else if (item.status_ == "Charging") {
                        watts = absolute(watts);
                    }
                    item.watts_ = static_cast<float>(watts);
                }
                const auto energy_now = parse_f64(trim(read_file(base + "energy_now")).view());
                const auto energy_full = parse_f64(trim(read_file(base + "energy_full")).view());
                const auto energy_design = parse_f64(trim(read_file(base + "energy_full_design")).view());
                const auto voltage = parse_f64(trim(read_file(base + "voltage_now")).view());
                const auto temperature = parse_f64(trim(read_file(base + "temp")).view());
                const auto cycle_count = parse_u64(trim(read_file(base + "cycle_count")).view());
                const auto health = parse_f64(trim(read_file(base + "capacity")).view());
                const auto vendor = trim(read_file(base + "manufacturer"));
                item.extra_ = json::object();
                if (energy_full.has_value() && energy_now.has_value() && *energy_full > 0) {
                    item.extra_.set("health_percent", json(*health));
                } else if (health.has_value()) {
                    item.extra_.set("health_percent", json(*health));
                }
                if (energy_now.has_value()) {
                    item.extra_.set("energy_wh", json(*energy_now / 1'000'000.0));
                }
                if (energy_full.has_value()) {
                    item.extra_.set("full_capacity_wh", json(*energy_full / 1'000'000.0));
                }
                if (energy_design.has_value()) {
                    item.extra_.set("design_capacity_wh", json(*energy_design / 1'000'000.0));
                }
                if (voltage.has_value()) {
                    item.extra_.set("voltage_v", json(*voltage / 1'000'000.0));
                }
                if (temperature.has_value()) {
                    item.extra_.set("temperature_celsius", json(*temperature / 10.0));
                }
                if (cycle_count.has_value()) {
                    item.extra_.set("cycle_count", json(*cycle_count));
                }
                item.extra_.set("vendor", json(vendor));
                if (const auto rate = parse_f64(trim(read_file(base + "power_now")).view());
                    rate.has_value() && *rate > 0 && energy_now.has_value()) {
                    if (item.status_ == "Charging" && energy_full.has_value()) {
                        const auto seconds = static_cast<uint64_t>((*energy_full - *energy_now) / *rate * 3600.0);
                        item.extra_.set("time_to_full_seconds", json(seconds));
                    } else if (item.status_ == "Discharging") {
                        const auto seconds = static_cast<uint64_t>(*energy_now / *rate * 3600.0);
                        item.extra_.set("time_to_empty_seconds", json(seconds));
                    }
                }
                out.push_back(move(item));
                ++index;
            }
            return out;
        }


    vector<uint64_t> core_frequencies_mhz(size_t count) {
        vector<uint64_t> values;
        values.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            const auto value = parse_u64(trim(read_file(
                    format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", index))).view());
            values.push_back(value.has_value() ? *value / 1000 : 0);
        }
        return values;
    }

    string process_state_text(const ::neforce::process::snapshot_entry& entry) {
        if (entry.state_code != 0) {
            switch (entry.state_code) {
                case 'R':
                    return "Runnable";
                case 'S':
                    return "Sleeping";
                case 'D':
                    return "UninterruptibleDiskSleep";
                case 'Z':
                    return "Zombie";
                case 'T':
                    return "Stopped";
                case 't':
                    return "Tracing";
                case 'X':
                case 'x':
                    return "Dead";
                case 'I':
                    return "Idle";
                case 'P':
                    return "Parked";
                default:
                    break;
            }
        }
        switch (entry.status) {
            case ::neforce::process::state::running:
                return "Running";
            case ::neforce::process::state::suspended:
                return "Suspended";
            case ::neforce::process::state::stopped:
                return "Stopped";
            case ::neforce::process::state::exited:
                return "Zombie";
            case ::neforce::process::state::unknown:
            default:
                return "Unknown";
        }
    }

    vector<pressure> read_pressure() {
        vector<pressure> pressure_values;
        for (const auto& info: sysinfo::pressure_status()) {
            if (!info.available) {
                continue;
            }
            pressure item;
            item.resource_ = info.resource;
            item.some_10_ = info.some_avg10;
            item.some_60_ = info.some_avg60;
            item.some_300_ = info.some_avg300;
            item.some_total_us_ = info.some_total_us;
            if (info.has_full) {
                item.full_10_ = info.full_avg10;
                item.full_60_ = info.full_avg60;
                item.full_300_ = info.full_avg300;
                item.full_total_us_ = info.full_total_us;
            }
            pressure_values.push_back(move(item));
        }
        return pressure_values;
    }

} // namespace neforce::neflux::platform
