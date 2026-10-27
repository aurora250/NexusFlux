#include <NeForce/core/container/flat_unordered_map.hpp>
#include <NeFlux/common.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <sys/statvfs.h>
#    include <sys/utsname.h>
#    include <unistd.h>
#    include <cstdio>
#endif
namespace neforce::neflux {

    namespace {
#if NEFLUX_PLATFORM_LINUX
        constexpr double sensor_refresh_seconds = 2.0;
        constexpr double storage_refresh_seconds = 10.0;

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
        string host_name() {
            const auto text = trim(read_file("/proc/sys/kernel/hostname"));
            return text.empty() ? "localhost" : text;
        }
        string kernel_version() {
            ::utsname info{};
            if (::uname(&info) == 0) {
                return info.release;
            }
            return {};
        }
        string os_name() {
            string name;
            string version;
            const auto text = read_file("/etc/os-release");
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "NAME=")) {
                    name = line.substr(5);
                } else if (starts_with(line.view(), "VERSION_ID=")) {
                    version = line.substr(11);
                }
                if (!name.empty() && !version.empty()) {
                    break;
                }
            }
            for (auto* value: {&name, &version}) {
                if (value->size() >= 2 && (*value)[0] == '"' && (*value)[value->size() - 1] == '"') {
                    *value = value->substr(1, value->size() - 2);
                }
            }
            if (name.empty()) {
                return kernel_version();
            }
            return version.empty() ? format("Linux ({})", name) : format("Linux ({} {})", name, version);
        }
        uint64_t uptime_seconds() {
            const auto text = read_file("/proc/uptime");
            const auto first = text.substr(0, text.find(' '));
            const auto value = parse_f64(first.view());
            return value.has_value() ? static_cast<uint64_t>(*value) : 0;
        }
        string cpu_brand() {
            const auto text = read_file("/proc/cpuinfo");
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "model name")) {
                    const auto colon = line.find(':');
                    if (colon != string::npos) {
                        return trim(line.view().substr(colon + 1));
                    }
                }
                if (starts_with(line.view(), "Hardware")) {
                    const auto colon = line.find(':');
                    if (colon != string::npos) {
                        return trim(line.view().substr(colon + 1));
                    }
                }
            }
            return {};
        }
        uint64_t physical_core_count() {
            const auto text = read_file("/proc/cpuinfo");
            set<string> seen;
            string package;
            string core;
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "physical id")) {
                    package = trim(line.view().substr(line.find(':') + 1));
                } else if (starts_with(line.view(), "core id")) {
                    core = trim(line.view().substr(line.find(':') + 1));
                } else if (line.empty()) {
                    if (!package.empty() || !core.empty()) {
                        seen.insert(package + ":" + core);
                    }
                    package.clear();
                    core.clear();
                }
            }
            if (!package.empty() || !core.empty()) {
                seen.insert(package + ":" + core);
            }
            return seen.empty() ? 0 : seen.size();
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
        struct topology_entry {
            optional<uint32_t> package;
            optional<uint32_t> core;
        };

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
        void read_load(float (&load)[3]) {
            const auto parts = split_whitespace(read_file("/proc/loadavg").view());
            for (size_t i = 0; i < 3 && i < parts.size(); ++i) {
                const auto value = parse_f64(parts[i].view());
                load[i] = value.has_value() ? static_cast<float>(*value) : 0.0F;
            }
        }
        struct proc_meminfo {
            optional<uint64_t> total;
            optional<uint64_t> available;
            optional<uint64_t> free;
            optional<uint64_t> cached;
            optional<uint64_t> swap_total;
            optional<uint64_t> swap_free;
        };

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
            stable_sort_by_index(out, [](const disk& a, const disk& b) { return a.mount_ < b.mount_; });
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
        vector<pressure> read_pressure() {
            vector<pressure> out;
            for (const char* name: {"cpu", "memory", "io"}) {
                const auto text = read_file(string("/proc/pressure/") + name);
                if (text.empty()) {
                    continue;
                }
                pressure item;
                item.resource_ = name;
                const auto value = [&text](const char* kind, const char* key) -> optional<double> {
                    for (const auto& line: split_lines(text)) {
                        if (starts_with(line.view(), kind)) {
                            const auto parts = split_whitespace(line.view());
                            for (const auto& part: parts) {
                                if (starts_with(part.view(), key)) {
                                    return parse_f64(part.view().substr(string_view(key).size()));
                                }
                            }
                        }
                    }
                    return none;
                };
                item.some_10_ = value("some ", "avg10=");
                item.some_60_ = value("some ", "avg60=");
                item.some_300_ = value("some ", "avg300=");
                if (const auto total = value("some ", "total="); total.has_value()) {
                    item.some_total_us_ = static_cast<uint64_t>(*total);
                }
                item.full_10_ = value("full ", "avg10=");
                item.full_60_ = value("full ", "avg60=");
                item.full_300_ = value("full ", "avg300=");
                if (const auto total = value("full ", "total="); total.has_value()) {
                    item.full_total_us_ = static_cast<uint64_t>(*total);
                }
                out.push_back(move(item));
            }
            return out;
        }

#endif

        struct process_sample {
            uint64_t cpu_ms = 0;
            uint64_t read_bytes = 0;
            uint64_t write_bytes = 0;
            uint64_t start_ms = 0;
            bool valid = false;
        };

        string process_state_text(const ::neforce::process::snapshot_entry& entry) {
#if NEFLUX_PLATFORM_LINUX
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
#endif
            switch (entry.status) {
                case ::neforce::process::state::running:
                    return "Running";
                case ::neforce::process::state::suspended:
                    return "Suspended";
                case ::neforce::process::state::stopped:
                    return "Stopped";
                case ::neforce::process::state::exited:
                    return "Exited";
                default:
                    return "Unknown";
            }
        }

        string system_host_name() {
#if NEFLUX_PLATFORM_LINUX
            return host_name();
#else
            return sysinfo::instance().host_name();
#endif
        }

        string system_os_name() {
#if NEFLUX_PLATFORM_LINUX
            return os_name();
#else
            return sysinfo::instance().os_version().product_name;
#endif
        }

        string system_kernel_version() {
#if NEFLUX_PLATFORM_LINUX
            return kernel_version();
#else
            return sysinfo::instance().os_version().version();
#endif
        }

        uint64_t system_uptime_seconds() {
#if NEFLUX_PLATFORM_LINUX
            return uptime_seconds();
#else
            return sysinfo::uptime_seconds();
#endif
        }

        string system_cpu_brand() {
#if NEFLUX_PLATFORM_LINUX
            return cpu_brand();
#else
            return sysinfo::instance().get_cpu_info().brand;
#endif
        }

        uint64_t system_physical_cores() {
#if NEFLUX_PLATFORM_LINUX
            return physical_core_count();
#else
            return sysinfo::instance().get_cpu_info().cores;
#endif
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

    json hardware_snapshot(uint64_t memory_total) {
        const auto& info = sysinfo::instance();
        const auto& cpu = info.get_cpu_info();
        const auto& hardware = info.get_hardware_info();
        json root = json::object();
        json cpu_json = json::object();
        cpu_json.set("Family", or_dash(cpu.family));
        cpu_json.set("Model", or_dash(cpu.model));
        cpu_json.set("Stepping", or_dash(cpu.stepping));
        cpu_json.set("Ext family", or_dash(cpu.ext_family));
        cpu_json.set("Ext model", or_dash(cpu.ext_model));
        cpu_json.set("Revision", or_dash(cpu.revision));
        cpu_json.set("Core VID", or_dash(cpu.core_vid));
        cpu_json.set("Technology", or_dash(cpu.technology));
        cpu_json.set("Package", or_dash(cpu.package));
        cpu_json.set("Core name", or_dash(cpu.brand));
        cpu_json.set("Max TDP", cpu.max_tdp_watts != 0 ? format("{} W", cpu.max_tdp_watts) : string("—"));
        cpu_json.set("Clocks (P-core)",
                     cpu.max_mhz != 0 ? format("{} / {} MHz", cpu.max_mhz, cpu.current_mhz) : string("—"));
        cpu_json.set("Cores / Threads", format("{} / {}", cpu.cores, cpu.logical_processors));
        cpu_json.set("Instructions", or_dash(cpu.features));
        cpu_json.set("SIMD", or_dash(simd_extensions(cpu.simd)));
        cpu_json.set("L1", cache_label(cpu.l1_kib, cpu.l1_instances));
        cpu_json.set("L2", cache_label(cpu.l2_kib, cpu.l2_instances));
        cpu_json.set("L3", cache_label(cpu.l3_kib, cpu.l3_instances));
        root.set("CPU", move(cpu_json));

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
#if NEFLUX_PLATFORM_LINUX
        vector<topology_entry> topology;
        vector<sensor> sensors;
        vector<battery> batteries;
        vector<disk> disks;
        double sensors_at = -1.0;
        double disks_at = -1.0;
#endif

        impl() {
#if NEFLUX_PLATFORM_LINUX
            topology = read_topology(sysinfo::instance().get_cpu_info().logical_processors);
            sensors = read_sensors();
            batteries = read_batteries();
            disks = read_disks();
            const double now = static_cast<double>(monotonic_ms());
            sensors_at = now;
            disks_at = now;
#endif
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
            const float cpu_percent = static_cast<float>(sysinfo::cpu_usage());
            vector<float> cores;
            const auto core_usage = sysinfo::cpu_usage_per_core();
            cores.reserve(core_usage.size());
            for (const auto value: core_usage) {
                cores.push_back(static_cast<float>(value));
            }
            if (cores.empty()) {
                cores.assign(cpu.logical_processors, cpu_percent);
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
#if NEFLUX_PLATFORM_LINUX
            cpu_mhz = cpu_frequency_mhz();
#endif

            float load[3] = {0.0F, 0.0F, 0.0F};
#if NEFLUX_PLATFORM_LINUX
            read_load(load);
#endif

            const auto memory_status_value = sysinfo::memory_status();
            memory memory_value;
            memory_value.total_ = memory_status_value.total_physical;
            memory_value.available_ = memory_status_value.available_physical;
            memory_value.free_ = memory_status_value.available_physical;
            const uint64_t swap_free = memory_status_value.available_page_file;
            memory_value.swap_total_ = memory_status_value.total_page_file;
            memory_value.swap_used_ = memory_value.swap_total_ > swap_free ? memory_value.swap_total_ - swap_free : 0;
#if NEFLUX_PLATFORM_LINUX
            const auto meminfo = read_meminfo();
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
#endif
            memory_value.used_ =
                    memory_value.total_ > memory_value.available_ ? memory_value.total_ - memory_value.available_ : 0;

            vector<process> processes;
            if (processes_enabled) {
                processes = collect_processes(elapsed, cfg.show_threads_);
            }

            vector<network> networks = collect_networks(elapsed);

            vector<disk> current_disks = collect_disks();
#if NEFLUX_PLATFORM_LINUX
            current_disks = disks;
            if (began - disks_at >= storage_refresh_seconds * 1000.0) {
                current_disks = read_disks();
                disks = current_disks;
                disks_at = began;
            }
#else
            current_disks = collect_disks();
#endif

            vector<sensor> sensors_value;
            vector<battery> batteries_value = collect_batteries();
#if NEFLUX_PLATFORM_LINUX
            if (began - sensors_at >= sensor_refresh_seconds * 1000.0) {
                sensors = read_sensors();
                batteries = read_batteries();
                sensors_at = began;
            }
            sensors_value = sensors;
            batteries_value = batteries;
#endif

            vector<disk_io> disk_io_values;
            for (const auto& item: sysinfo::disk_io_rates()) {
                disk_io value;
                value.name_ = item.name;
                value.read_per_sec_ = item.read_bytes_per_second;
                value.write_per_sec_ = item.write_bytes_per_second;
                value.busy_percent_ = item.busy_percent;
                value.aggregate_ = item.aggregate;
                disk_io_values.push_back(move(value));
            }
            vector<optional<float>> core_temperatures_value;
            vector<pressure> pressure_values;
#if NEFLUX_PLATFORM_LINUX
            core_temperatures_value = core_temperatures(topology);
            pressure_values = read_pressure();
#endif

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
            stable_sort_by_index(processes, [](const process& a, const process& b) {
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
#if NEFLUX_PLATFORM_LINUX
            topology_json.set("architecture", json("x86_64"));
            json frequencies = json::array();
            for (uint32_t index = 0; index < result.cores_.size(); ++index) {
                const auto value = parse_u64(
                        trim(read_file(format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", index)))
                                .view());
                frequencies.push(json(value.has_value() ? *value / 1000 : 0));
            }
            topology_json.set("per_core_frequency_mhz", move(frequencies));
#endif
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
                process item;
                item.pid_ = entry.pid;
                if (entry.parent_pid != 0) {
                    item.parent_ = entry.parent_pid;
                }
                item.start_time_ = entry.start_time_ms / 1000;
                item.name_ = entry.name;
                item.command_ = entry.command.empty() ? entry.name : entry.command;
                item.user_ = entry.user.empty() ? string("—") : entry.user;
                item.state_ = process_state_text(entry);
                item.memory_ = entry.rss_bytes;
                item.virtual_memory_ = entry.virtual_bytes;
                item.cpu_time_ms_ = entry.user_time_ms + entry.system_time_ms;
                item.read_total_ = entry.io.read_bytes;
                item.write_total_ = entry.io.write_bytes;
                item.kernel_thread_ = entry.kernel_thread;
#if NEFLUX_PLATFORM_LINUX
                item.nice_ = entry.nice;
#endif
                item.runtime_ = unix_time() > item.start_time_ ? unix_time() - item.start_time_ : 0;
                item.extra_ = json::object();
                item.extra_.set("threads", json(static_cast<uint64_t>(entry.threads)));

                auto& previous = previous_processes[entry.pid];
                if (previous.valid && previous.start_ms == entry.start_time_ms && seconds > 0.0) {
                    item.cpu_ = static_cast<float>(static_cast<double>(item.cpu_time_ms_ - previous.cpu_ms) * 100.0 /
                                                   (seconds * 1000.0));
                    item.read_per_sec_ =
                            static_cast<double>(item.read_total_ - previous.read_bytes) / max(0.001, seconds);
                    item.write_per_sec_ =
                            static_cast<double>(item.write_total_ - previous.write_bytes) / max(0.001, seconds);
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
            stable_sort_by_index(out, [](const network& a, const network& b) {
                if (a.rx_per_sec_ != b.rx_per_sec_) {
                    return a.rx_per_sec_ > b.rx_per_sec_;
                }
                return a.name_ < b.name_;
            });
            return out;
        }

        static vector<disk> collect_disks() {
#if NEFLUX_PLATFORM_LINUX
            return read_disks();
#else
            vector<disk> out;
            for (const auto& mount: sysinfo::mount_points()) {
                disk item;
                item.name_ = mount.source.empty() ? mount.target : mount.source;
                item.mount_ = mount.target;
                item.filesystem_ = mount.filesystem;
                item.available_ = mount.available_bytes;
                item.total_ = mount.total_bytes;
                item.used_ = item.total_ > item.available_ ? item.total_ - item.available_ : 0;
                item.extra_ = json::object();
                out.push_back(move(item));
            }
            stable_sort_by_index(out, [](const disk& a, const disk& b) { return a.mount_ < b.mount_; });
            return out;
#endif
        }

        static vector<battery> collect_batteries() {
#if NEFLUX_PLATFORM_LINUX
            return read_batteries();
#else
            vector<battery> out;
            const auto info = sysinfo::battery_status();
            if (!info.present) {
                return out;
            }
            battery item;
            item.name_ = "Battery";
            item.status_ = info.status;
            if (info.charge_percent >= 0.0) {
                item.percent_ = static_cast<float>(info.charge_percent);
            }
            if (info.rate_mw >= 0.0) {
                item.watts_ = static_cast<float>(info.rate_mw / 1000.0);
            }
            item.extra_ = json::object();
            if (info.capacity_mwh >= 0.0) {
                item.extra_.set("energy_wh", json(info.capacity_mwh / 1000.0));
            }
            if (info.full_capacity_mwh >= 0.0) {
                item.extra_.set("full_capacity_wh", json(info.full_capacity_mwh / 1000.0));
            }
            if (info.remaining_seconds >= 0) {
                if (info.charging) {
                    item.extra_.set("time_to_full_seconds", json(static_cast<uint64_t>(info.remaining_seconds)));
                } else {
                    item.extra_.set("time_to_empty_seconds", json(static_cast<uint64_t>(info.remaining_seconds)));
                }
            }
            out.push_back(move(item));
            return out;
#endif
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
        result.memory_.used_ = memory.total_physical > memory.available_physical
                                       ? memory.total_physical - memory.available_physical
                                       : 0;
        result.memory_.swap_total_ = memory.total_page_file;
        result.memory_.swap_used_ = memory.total_page_file > memory.available_page_file
                                            ? memory.total_page_file - memory.available_page_file
                                            : 0;
        result.cores_.assign(sysinfo::instance().get_cpu_info().logical_processors, 0.0F);
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
