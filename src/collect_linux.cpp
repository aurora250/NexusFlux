#include <NeFlux/common.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/container/set.hpp>
#include <cstddef>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
namespace neforce::neflux {

    namespace {

        constexpr double sensor_refresh_seconds = 2.0;
        constexpr double storage_refresh_seconds = 10.0;

        string proc_path(const char* format_value) { return string("/proc/") + format_value; }

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

        struct cpu_line {
            uint64_t fields[10] = {};
            size_t count = 0;
        };

        vector<cpu_line> read_cpu_lines() {
            vector<cpu_line> lines;
            const auto text = read_file("/proc/stat");
            for (const auto& line: split_lines(text)) {
                if (!starts_with(line.view(), "cpu")) {
                    continue;
                }
                size_t begin = 3;
                while (begin < line.size() && line[begin] >= '0' && line[begin] <= '9') {
                    ++begin;
                }
                cpu_line entry;
                const auto parts = split_whitespace(line.view().substr(begin));
                for (size_t i = 0; i < parts.size() && i < 10; ++i) {
                    const auto value = parse_u64(parts[i].view());
                    entry.fields[i] = value.has_value() ? *value : 0;
                    entry.count = i + 1;
                }
                if (entry.count >= 4) {
                    lines.push_back(entry);
                }
            }
            return lines;
        }

        vector<uint64_t> aggregate_counters(const vector<cpu_line>& lines) {
            vector<uint64_t> out;
            if (lines.empty()) {
                return out;
            }
            for (size_t i = 0; i < lines[0].count; ++i) {
                out.push_back(lines[0].fields[i]);
            }
            return out;
        }

        vector<float> core_percentages(const vector<cpu_line>& previous, const vector<cpu_line>& current) {
            vector<float> out;
            if (previous.size() != current.size()) {
                return out;
            }
            out.reserve(current.size());
            for (size_t index = 1; index < current.size(); ++index) {
                uint64_t total_delta = 0;
                uint64_t idle_delta = 0;
                bool valid = true;
                for (size_t field = 0; field < 4; ++field) {
                    if (current[index].fields[field] < previous[index].fields[field]) {
                        valid = false;
                        break;
                    }
                    total_delta += current[index].fields[field] - previous[index].fields[field];
                    if (field == 3) {
                        idle_delta = current[index].fields[field] - previous[index].fields[field];
                    }
                }
                if (!valid || total_delta == 0) {
                    out.push_back(0.0F);
                    continue;
                }
                out.push_back(static_cast<float>(100.0 * static_cast<double>(total_delta - idle_delta) /
                                                 static_cast<double>(total_delta)));
            }
            return out;
        }

        float global_cpu_percent(const vector<cpu_line>& previous, const vector<cpu_line>& current) {
            if (previous.empty() || current.empty()) {
                return 0.0F;
            }
            uint64_t total_delta = 0;
            uint64_t idle_delta = 0;
            for (size_t field = 0; field < 4; ++field) {
                if (current[0].fields[field] < previous[0].fields[field]) {
                    return 0.0F;
                }
                total_delta += current[0].fields[field] - previous[0].fields[field];
                if (field == 3) {
                    idle_delta = current[0].fields[field] - previous[0].fields[field];
                }
            }
            if (total_delta == 0) {
                return 0.0F;
            }
            return static_cast<float>(100.0 * static_cast<double>(total_delta - idle_delta) /
                                      static_cast<double>(total_delta));
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
                map<string, uint32_t> core_labels;
                map<string, string> inputs;
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

        map<uint32_t, string> read_users() {
            map<uint32_t, string> users;
            const auto text = read_file("/etc/passwd");
            for (const auto& line: split_lines(text)) {
                const auto fields = split(line.view(), ':', true);
                if (fields.size() < 3) {
                    continue;
                }
                const auto uid = parse_u64(fields[2].view());
                if (uid.has_value()) {
                    users[static_cast<uint32_t>(*uid)] = fields[0];
                }
            }
            return users;
        }

        struct stat_fields {
            string name;
            vector<string> fields;
            
            NEFORCE_NODISCARD bool valid() const { return fields.size() >= 22; }
            
            NEFORCE_NODISCARD uint64_t number(size_t index) const {
                if (index >= fields.size()) {
                    return 0;
                }
                const auto value = parse_u64(fields[index].view());
                return value.has_value() ? *value : 0;
            }
        };

        stat_fields parse_stat(const string& text) {
            stat_fields result;
            const auto begin = text.find('(');
            const auto end = text.rfind(')');
            if (begin == string::npos || end == string::npos || end <= begin) {
                return result;
            }
            result.name = text.substr(begin + 1, end - begin - 1);
            result.fields = split_whitespace(text.view().substr(end + 1));
            return result;
        }

        pair<uint64_t, uint64_t> parse_io(const string& text) {
            uint64_t read_bytes = 0;
            uint64_t write_bytes = 0;
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "read_bytes:")) {
                    const auto value = parse_u64(trim(line.view().substr(11)).view());
                    read_bytes = value.has_value() ? *value : 0;
                } else if (starts_with(line.view(), "write_bytes:")) {
                    const auto value = parse_u64(trim(line.view().substr(12)).view());
                    write_bytes = value.has_value() ? *value : 0;
                }
            }
            return {read_bytes, write_bytes};
        }

        optional<uint32_t> parse_uid(const string& status) {
            for (const auto& line: split_lines(status)) {
                if (!starts_with(line.view(), "Uid:")) {
                    continue;
                }
                const auto parts = split_whitespace(line.view().substr(4));
                if (!parts.empty()) {
                    const auto value = parse_u64(parts[0].view());
                    if (value.has_value()) {
                        return static_cast<uint32_t>(*value);
                    }
                }
            }
            return none;
        }

        string process_state(const string& code) {
            if (code == "R") {
                return "Runnable";
            }
            if (code == "S") {
                return "Sleeping";
            }
            if (code == "D") {
                return "UninterruptibleDiskSleep";
            }
            if (code == "Z") {
                return "Zombie";
            }
            if (code == "T") {
                return "Stopped";
            }
            if (code == "t") {
                return "Tracing";
            }
            if (code == "X" || code == "x") {
                return "Dead";
            }
            if (code == "I") {
                return "Idle";
            }
            if (code == "P") {
                return "Parked";
            }
            return "Unknown";
        }

        struct cached_process {
            uint64_t start = 0;
            uint64_t cpu = 0;
            uint64_t read = 0;
            uint64_t written = 0;
            string name;
            string command;
            optional<uint32_t> uid;
            bool valid = false;
        };

        string command_line(const string& directory) {
            const auto text = read_file(directory + "/cmdline", 8 * 1024 * 1024);
            string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                const auto c = static_cast<unsigned char>(text[i]);
                if (c == 0) {
                    if (i + 1 < text.size() && !out.empty() && out[out.size() - 1] != ' ') {
                        out.push_back(' ');
                    }
                    continue;
                }
                if (c < 0x20 || c == 0x7F) {
                    continue;
                }
                out.push_back(static_cast<char>(c));
            }
            while (!out.empty() && out[out.size() - 1] == ' ') {
                out.resize(out.size() - 1);
            }
            return out;
        }

        uint64_t boot_time() {
            const auto text = read_file("/proc/stat");
            for (const auto& line: split_lines(text)) {
                if (starts_with(line.view(), "btime ")) {
                    const auto value = parse_u64(trim(line.view().substr(6)).view());
                    if (value.has_value()) {
                        return *value;
                    }
                }
            }
            return 0;
        }

        string interface_address(const string& name) {
            string out;
            ::ifaddrs* list = nullptr;
            if (::getifaddrs(&list) != 0 || list == nullptr) {
                return out;
            }
            for (const ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
                if (item->ifa_addr == nullptr || name != item->ifa_name) {
                    continue;
                }
                char buffer[INET6_ADDRSTRLEN] = {};
                const int family = item->ifa_addr->sa_family;
                if (family == AF_INET) {
                    const auto* address = reinterpret_cast<::sockaddr_in*>(item->ifa_addr);
                    if (::inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer)) != nullptr) {
                        if (!out.empty()) {
                            out.append(", ");
                        }
                        out.append(buffer);
                    }
                } else if (family == AF_INET6) {
                    const auto* address = reinterpret_cast<::sockaddr_in6*>(item->ifa_addr);
                    if (::inet_ntop(AF_INET6, &address->sin6_addr, buffer, sizeof(buffer)) != nullptr) {
                        if (!out.empty()) {
                            out.append(", ");
                        }
                        out.append(buffer);
                    }
                }
            }
            ::freeifaddrs(list);
            return out;
        }

        struct net_counters {
            uint64_t rx = 0;
            uint64_t tx = 0;
            uint64_t rx_packets = 0;
            uint64_t tx_packets = 0;
        };

        net_counters read_net_counters(const string& name) {
            net_counters counters;
            const string base = "/sys/class/net/" + name + "/statistics/";
            if (const auto value = parse_u64(trim(read_file(base + "rx_bytes")).view()); value.has_value()) {
                counters.rx = *value;
            }
            if (const auto value = parse_u64(trim(read_file(base + "tx_bytes")).view()); value.has_value()) {
                counters.tx = *value;
            }
            if (const auto value = parse_u64(trim(read_file(base + "rx_packets")).view()); value.has_value()) {
                counters.rx_packets = *value;
            }
            if (const auto value = parse_u64(trim(read_file(base + "tx_packets")).view()); value.has_value()) {
                counters.tx_packets = *value;
            }
            return counters;
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
            struct ::statvfs info {};
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
            const auto text = read_file("/proc/mounts");
            for (const auto& line: split_lines(text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 4) {
                    continue;
                }
                string mount = parts[1];
                string unescaped;
                for (size_t i = 0; i < mount.size(); ++i) {
                    if (mount[i] == '\\' && i + 3 < mount.size() && mount[i + 1] == '0' && mount[i + 2] == '4' &&
                        mount[i + 3] == '0') {
                        unescaped.push_back(' ');
                        i += 3;
                    } else {
                        unescaped.push_back(mount[i]);
                    }
                }
                mount = unescaped;
                if (!seen.insert(mount).second) {
                    continue;
                }
                const string& source = parts[0];
                const string& filesystem = parts[2];
                const string source_name = device_name(source);
                if (!starts_with(source.view(), "/dev/") || starts_with(source_name.view(), "loop") ||
                    filesystem == "squashfs" || filesystem == "iso9660" || filesystem == "devtmpfs" ||
                    filesystem == "proc" || filesystem == "sysfs" || filesystem == "efivarfs") {
                    continue;
                }
                struct statvfs info {};
                if (statvfs(mount.data(), &info) != 0 || info.f_blocks == 0) {
                    continue;
                }
                const uint64_t total = info.f_blocks * info.f_frsize;
                if (total == 0) {
                    continue;
                }
                disk item;
                item.name_ = source;
                item.mount_ = mount;
                item.filesystem_ = filesystem;
                item.kind_ = block_kind(source_name);
                item.removable_ = block_removable(source_name);
                item.available_ = info.f_bavail * info.f_frsize;
                item.total_ = total;
                item.used_ = total > item.available_ ? total - item.available_ : 0;
                item.extra_ = statvfs_fields(mount);
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

        string container_name_from_cgroup(const string& text) {
            for (const auto& line: split_lines(text)) {
                if (line.contains("docker") || line.contains("podman")) {
                    return line;
                }
            }
            return {};
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

    struct collector::impl {
        bool gpu_enabled = true;
        vector<cpu_line> previous_cpu;
        vector<uint64_t> previous_aggregate;
        map<string, net_counters> previous_networks;
        map<string, tuple<uint64_t, uint64_t, uint64_t>> previous_disks;
        map<uint32_t, cached_process> processes;
        map<uint32_t, string> users;
        vector<topology_entry> topology;
        vector<sensor> sensors;
        vector<battery> batteries;
        vector<disk> disks;
        double sensors_at = -1.0;
        double disks_at = -1.0;
        uint64_t cycles = 0;
        uint64_t boot = 0;
        uint64_t ticks_per_second = 100;
        uint64_t page_size = 4096;
        uint64_t started_ms = 0;
        double previous_at = 0.0;
        uint64_t uptime = 0;
        string hostname;
        string os;
        string kernel;
        string cpu_name;
        uint64_t physical_cores = 0;
        gpu_result cached_gpu;
        connection_collector connections;
        advanced advanced_telemetry;

        impl() :
        boot(boot_time()),
        started_ms(static_cast<uint64_t>(monotonic_ms())),
        previous_at(static_cast<double>(monotonic_ms())) {
            ticks_per_second = static_cast<uint64_t>(::sysconf(_SC_CLK_TCK));
            if (ticks_per_second == 0) {
                ticks_per_second = 100;
            }
            page_size = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
            if (page_size == 0) {
                page_size = 4096;
            }
            hostname = host_name();
            os = os_name();
            kernel = kernel_version();
            cpu_name = cpu_brand();
            physical_cores = physical_core_count();
            users = read_users();
            topology = read_topology(read_cpu_lines().size() > 1 ? read_cpu_lines().size() - 1 : 0);
            previous_cpu = read_cpu_lines();
            previous_aggregate = aggregate_counters(previous_cpu);
            previous_networks.clear();
            for (const auto& full: list_directory("/sys/class/net")) {
                const auto slash = full.rfind('/');
                const string name = slash == string::npos ? full : full.substr(slash + 1);
                previous_networks[name] = read_net_counters(name);
            }
            previous_disks = parse_diskstats(read_file("/proc/diskstats").view());
            disks = read_disks();
            sensors = read_sensors();
            batteries = read_batteries();
            disks_at = static_cast<double>(monotonic_ms());
            sensors_at = disks_at;
        }

        // TODO: neforce::sysinfo have system info collections

        snapshot sample_exact(const config& cfg, bool processes_enabled, bool live_gpu_processes,
                              bool connections_enabled) {
            const auto began = static_cast<double>(monotonic_ms());
            const double elapsed = max(0.001, (began - previous_at) / 1000.0);
            previous_at = began;
            const double collection_started = began;

            vector<cpu_line> current_cpu = read_cpu_lines();
            const auto current_aggregate = aggregate_counters(current_cpu);
            vector<float> cores = core_percentages(previous_cpu, current_cpu);
            const float cpu_percent = global_cpu_percent(previous_cpu, current_cpu);
            json breakdown = cpu_breakdown(previous_aggregate, current_aggregate);
            previous_cpu = current_cpu;
            previous_aggregate = current_aggregate;

            if (cycles % 5 == 0) {
            }
            ++cycles;
            if (topology.size() != (current_cpu.empty() ? 0 : current_cpu.size() - 1)) {
                topology = read_topology(current_cpu.empty() ? 0 : current_cpu.size() - 1);
            }

            uptime = uptime_seconds();
            float load[3] = {0.0F, 0.0F, 0.0F};
            read_load(load);

            const auto meminfo = read_meminfo();
            memory memory_value;
            memory_value.total_ = meminfo.total.has_value() ? *meminfo.total : 0;
            memory_value.available_ = meminfo.available.has_value() ? *meminfo.available : 0;
            memory_value.free_ = meminfo.free;
            memory_value.cached_ = meminfo.cached;
            memory_value.swap_total_ = meminfo.swap_total.has_value() ? *meminfo.swap_total : 0;
            const uint64_t swap_free = meminfo.swap_free.has_value() ? *meminfo.swap_free : 0;
            memory_value.swap_used_ = memory_value.swap_total_ > swap_free ? memory_value.swap_total_ - swap_free : 0;
            memory_value.used_ =
                    memory_value.total_ > memory_value.available_ ? memory_value.total_ - memory_value.available_ : 0;

            vector<process> processes;
            if (processes_enabled) {
                processes = collect_processes(elapsed, cfg.show_threads_);
            }

            vector<network> networks = collect_networks(elapsed);
            vector<disk> current_disks = disks;
            if (began - disks_at >= storage_refresh_seconds * 1000.0) {
                current_disks = read_disks();
                disks = current_disks;
                disks_at = began;
            }
            if (began - sensors_at >= sensor_refresh_seconds * 1000.0) {
                sensors = read_sensors();
                batteries = read_batteries();
                sensors_at = began;
            }
            vector<disk_io> disk_io_values = collect_disk_io(elapsed);
            const auto core_temperatures_value = core_temperatures(topology);
            const auto pressure_values = read_pressure();
            const auto gpu_values = collect_gpus(gpu_enabled, live_gpu_processes);
            cached_gpu = gpu_values;

            map<uint32_t, tuple<optional<uint64_t>, optional<uint32_t>, vector<uint32_t>>> gpu_processes;
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

            uint64_t cpu_mhz = cpu_frequency_mhz();

            snapshot result;
            result.timestamp_ = unix_time();
            result.elapsed_ = (static_cast<double>(monotonic_ms()) - static_cast<double>(started_ms)) / 1000.0;
            result.collection_ms_ = 0.0;
            result.hostname_ = hostname;
            result.os_ = os;
            result.kernel_ = kernel;
            result.uptime_ = uptime;
            result.cpu_name_ = cpu_name;
            result.cpu_percent_ = cpu_percent;
            result.cpu_mhz_ = cpu_mhz;
            result.cpu_breakdown_ = move(breakdown);
            result.cores_ = move(cores);
            result.core_temperatures_ = core_temperatures_value;
            for (size_t i = 0; i < 3; ++i) {
                result.load_[i] = load[i];
            }
            result.memory_ = memory_value;
            result.processes_ = move(processes);
            result.networks_ = move(networks);
            result.disks_ = move(current_disks);
            result.disk_io_ = move(disk_io_values);
            result.sensors_ = sensors;
            result.batteries_ = batteries;
            result.gpus_ = gpu_values.devices_;
            result.gpu_status_ = gpu_values.status_;
            result.pressure_ = pressure_values;
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
            advanced_telemetry.collect(result, cfg);
            json collection_state = json::object();
            collection_state.set("processes", json(processes_enabled));
            collection_state.set("gpu_processes", json(live_gpu_processes));
            result.extra_.set("Collection state", move(collection_state));

            json topology_json = json::object();
            topology_json.set("architecture", json("x86_64"));
            topology_json.set("logical_cores", json(result.cores_.size()));
            topology_json.set("physical_cores", json(physical_cores));
            json frequencies = json::array();
            for (uint32_t index = 0; index < result.cores_.size(); ++index) {
                const auto value = parse_u64(
                        trim(read_file(format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", index)))
                                .view());
                frequencies.push(json(value.has_value() ? *value / 1000 : 0));
            }
            topology_json.set("per_core_frequency_mhz", move(frequencies));
            result.extra_.set("CPU topology", move(topology_json));

            map<string, uint64_t> states;
            optional<uint64_t> threads;
            for (const auto& item: result.processes_) {
                if (item.thread_) {
                    continue;
                }
                states[item.state_] += 1;
                const json* stat = item.extra_.get("stat");
                if (stat != nullptr && stat->is_object()) {
                    const json* value = stat->get("threads");
                    if (value != nullptr && (value->is_integer() || value->is_number())) {
                        const uint64_t count = static_cast<uint64_t>(value->as_integer());
                        threads = (threads.has_value() ? *threads : 0) + count;
                    }
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

            result.collection_ms_ = (static_cast<double>(monotonic_ms()) - collection_started);
            return result;
        }

        vector<process> collect_processes(double seconds, bool include_threads);

        vector<network> collect_networks(double seconds);

        vector<disk_io> collect_disk_io(double seconds);
    };

    vector<process> collector::impl::collect_processes(double seconds, bool include_threads) {
        vector<process> out;
        set<uint32_t> seen;
        for (const auto& full: list_directory("/proc")) {
            const auto slash = full.rfind('/');
            const string name = slash == string::npos ? full : full.substr(slash + 1);
            const auto pid_value = parse_u64(name.view());
            if (!pid_value.has_value()) {
                continue;
            }
            const auto pid = static_cast<uint32_t>(*pid_value);
            const string directory = "/proc/" + name;
            const auto stat_text = read_file(directory + "/stat", 8192);
            if (stat_text.empty()) {
                continue;
            }
            const auto stat = parse_stat(stat_text);
            if (!stat.valid()) {
                continue;
            }
            const uint64_t start = stat.number(19);
            auto& cached = processes[pid];
            const bool fresh = !cached.valid || cached.start != start;
            if (fresh) {
                cached.start = start;
                cached.name = stat.name;
                cached.command = command_line(directory);
                cached.uid = parse_uid(read_file(directory + "/status"));
                cached.cpu = 0;
                cached.read = 0;
                cached.written = 0;
                cached.valid = true;
            }
            const uint64_t total_cpu = stat.number(11) + stat.number(12);
            process item;
            item.pid_ = pid;
            item.parent_ = stat.fields.size() > 1 && !stat.fields[1].empty()
                                   ? optional<uint32_t>(static_cast<uint32_t>(stat.number(1)))
                                   : none;
            item.start_time_ = boot + start / ticks_per_second;
            item.name_ = cached.name;
            item.command_ = cached.command;
            item.user_ = cached.uid.has_value()
                                 ? (users.count(*cached.uid) != 0 ? users[*cached.uid] : format("{}", *cached.uid))
                                 : string("—");
            item.state_ = process_state(stat.fields[0]);
            item.cpu_ =
                    fresh ? 0.0F
                          : static_cast<float>(static_cast<double>(total_cpu - cached.cpu) * 100.0 /
                                               static_cast<double>(ticks_per_second) / max(0.001, seconds));
            const auto statm = split_whitespace(read_file(directory + "/statm").view());
            if (statm.size() >= 2) {
                const auto virtual_pages = parse_u64(statm[0].view());
                const auto resident_pages = parse_u64(statm[1].view());
                item.virtual_memory_ = virtual_pages.has_value() ? *virtual_pages * page_size : stat.number(20);
                item.memory_ = resident_pages.has_value() ? *resident_pages * page_size : stat.number(21) * page_size;
            } else {
                item.virtual_memory_ = stat.number(20);
                item.memory_ = stat.number(21) * page_size;
            }
            const auto io = parse_io(read_file(directory + "/io"));
            item.read_total_ = io.first;
            item.write_total_ = io.second;
            item.read_per_sec_ =
                    fresh ? 0.0 : static_cast<double>(io.first - cached.read) / max(0.001, seconds);
            item.write_per_sec_ =
                    fresh ? 0.0 : static_cast<double>(io.second - cached.written) / max(0.001, seconds);
            item.cpu_time_ms_ = total_cpu * 1000 / ticks_per_second;
            if (stat.fields.size() > 16) {
                const auto nice = parse_i64(stat.fields[16].view());
                if (nice.has_value()) {
                    item.nice_ = static_cast<int32_t>(*nice);
                }
            }
            item.kernel_thread_ = (stat.number(6) & 0x200000ULL) != 0;
            item.runtime_ = unix_time() > item.start_time_ ? unix_time() - item.start_time_ : 0;
            item.extra_ = json::object();
            cached.cpu = total_cpu;
            cached.read = io.first;
            cached.written = io.second;
            seen.insert(pid);
            out.push_back(move(item));

            if (include_threads) {
                for (const auto& task_full: list_directory(directory + "/task")) {
                    const auto task_slash = task_full.rfind('/');
                    const string task_name = task_slash == string::npos ? task_full : task_full.substr(task_slash + 1);
                    const auto tid_value = parse_u64(task_name.view());
                    if (!tid_value.has_value() || static_cast<uint32_t>(*tid_value) == pid) {
                        continue;
                    }
                    const auto task_stat_text = read_file(task_full + "/stat", 8192);
                    const auto task_stat = parse_stat(task_stat_text);
                    if (!task_stat.valid()) {
                        continue;
                    }
                    process thread_item = item;
                    thread_item.pid_ = static_cast<uint32_t>(*tid_value);
                    thread_item.thread_ = true;
                    thread_item.name_ = task_stat.name;
                    thread_item.start_time_ = boot + task_stat.number(19) / ticks_per_second;
                    thread_item.cpu_time_ms_ = (task_stat.number(11) + task_stat.number(12)) * 1000 / ticks_per_second;
                    thread_item.cpu_ = 0.0F;
                    thread_item.read_per_sec_ = 0.0;
                    thread_item.write_per_sec_ = 0.0;
                    thread_item.command_ = item.command_;
                    out.push_back(move(thread_item));
                }
            }
        }
        vector<uint32_t> stale;
        for (const auto& entry: processes) {
            if (seen.count(entry.first) == 0) {
                stale.push_back(entry.first);
            }
        }
        for (const uint32_t pid: stale) {
            processes.erase(pid);
        }
        return out;
    }

    vector<network> collector::impl::collect_networks(double seconds) {
        vector<network> out;
        map<string, net_counters> current;
        for (const auto& full: list_directory("/sys/class/net")) {
            const auto slash = full.rfind('/');
            const string name = slash == string::npos ? full : full.substr(slash + 1);
            const auto counters = read_net_counters(name);
            const string base = "/sys/class/net/" + name + "/";
            network item;
            item.name_ = name;
            item.mac_ = trim(read_file(base + "address"));
            if (const auto mtu = parse_u64(trim(read_file(base + "mtu")).view()); mtu.has_value()) {
                item.mtu_ = *mtu;
            }
            if (const auto speed = parse_u64(trim(read_file(base + "speed")).view()); speed.has_value() && *speed > 0) {
                item.speed_mbps_ = *speed;
            }
            item.rx_packets_ = counters.rx_packets;
            item.tx_packets_ = counters.tx_packets;
            const auto previous = previous_networks.find(name);
            if (previous != previous_networks.end()) {
                if (counters.rx_packets >= previous->second.rx_packets) {
                    item.rx_packets_per_sec_ =
                            static_cast<double>(counters.rx_packets - previous->second.rx_packets) / seconds;
                }
                if (counters.tx_packets >= previous->second.tx_packets) {
                    item.tx_packets_per_sec_ =
                            static_cast<double>(counters.tx_packets - previous->second.tx_packets) / seconds;
                }
                item.rx_per_sec_ = rate(previous->second.rx, counters.rx, seconds);
                item.tx_per_sec_ = rate(previous->second.tx, counters.tx, seconds);
            }
            const auto rx_errors = parse_u64(trim(read_file(base + "statistics/rx_errors")).view());
            const auto tx_errors = parse_u64(trim(read_file(base + "statistics/tx_errors")).view());
            item.rx_errors_ = rx_errors.has_value() ? *rx_errors : 0;
            item.tx_errors_ = tx_errors.has_value() ? *tx_errors : 0;
            item.errors_ = item.rx_errors_ + item.tx_errors_;
            const auto rx_dropped = parse_u64(trim(read_file(base + "statistics/rx_dropped")).view());
            const auto tx_dropped = parse_u64(trim(read_file(base + "statistics/tx_dropped")).view());
            item.drops_ = (rx_dropped.has_value() ? *rx_dropped : 0) + (tx_dropped.has_value() ? *tx_dropped : 0);
            item.rx_total_ = counters.rx;
            item.tx_total_ = counters.tx;
            const auto state = trim(read_file(base + "operstate"));
            item.state_ = state.empty() ? "unknown" : state;
            item.address_ = interface_address(name);
            current[name] = counters;
            out.push_back(move(item));
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

    vector<disk_io> collector::impl::collect_disk_io(double seconds) {
        const auto counters = parse_diskstats(read_file("/proc/diskstats").view());
        vector<disk_io> out;
        for (const auto& entry: counters) {
            disk_io item;
            item.name_ = entry.first;
            const auto previous = previous_disks.find(entry.first);
            if (previous != previous_disks.end()) {
                item.read_per_sec_ = rate(get<0>(previous->second), get<0>(entry.second), seconds);
                item.write_per_sec_ = rate(get<1>(previous->second), get<1>(entry.second), seconds);
                const double busy = rate(get<2>(previous->second), get<2>(entry.second), seconds) / 10.0;
                item.busy_percent_ = busy < 0.0 ? 0.0 : (busy > 100.0 ? 100.0 : busy);
            } else {
                item.busy_percent_ = 0.0;
            }
            const string device = "/sys/class/block/" + entry.first;
            item.aggregate_ =
                    path(device + "/device").exists() && !path(device + "/partition").exists();
            out.push_back(move(item));
        }
        previous_disks = counters;
        stable_sort_by_index(out, [](const disk_io& a, const disk_io& b) { return a.name_ < b.name_; });
        return out;
    }

    snapshot initial_snapshot() {
        snapshot result;
        result.hostname_ = host_name();
        result.os_ = os_name();
        result.kernel_ = kernel_version();
        result.uptime_ = uptime_seconds();
        result.cpu_name_ = cpu_brand();
        const auto cpu_lines = read_cpu_lines();
        result.cores_.assign(cpu_lines.size() > 1 ? cpu_lines.size() - 1 : 0, 0.0F);
        read_load(result.load_);
        const auto meminfo = read_meminfo();
        result.memory_.total_ = meminfo.total.has_value() ? *meminfo.total : 0;
        result.memory_.available_ = meminfo.available.has_value() ? *meminfo.available : 0;
        result.memory_.free_ = meminfo.free;
        result.memory_.cached_ = meminfo.cached;
        result.memory_.swap_total_ = meminfo.swap_total.has_value() ? *meminfo.swap_total : 0;
        result.memory_.swap_used_ =
                result.memory_.swap_total_ > (meminfo.swap_free.has_value() ? *meminfo.swap_free : 0)
                        ? result.memory_.swap_total_ - (meminfo.swap_free.has_value() ? *meminfo.swap_free : 0)
                        : 0;
        result.memory_.used_ = result.memory_.total_ > result.memory_.available_
                                       ? result.memory_.total_ - result.memory_.available_
                                       : 0;
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
