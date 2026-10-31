#include <NeFlux/advanced.hpp>
#include <NeFlux/common.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/taskstats.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#include <NeForce/core/system/process.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <unistd.h>
#endif
namespace neforce::neflux {

    namespace {

        bool any_column(const config& cfg, const vector<const char*>& names) {
            for (const auto& column: cfg.columns_) {
                for (const char* name: names) {
                    if (column == name) {
                        return true;
                    }
                }
            }
            return false;
        }

        json parse_key_value(const string& text) {
            json out = json::object();
            for (const auto& line: split_lines(text)) {
                const auto colon = line.find(':');
                if (colon == string::npos) {
                    continue;
                }
                const string key = trim(line.view().substr(0, colon));
                const string raw = trim(line.view().substr(colon + 1));
                const auto parts = split_whitespace(raw.view());
                if (!parts.empty() && parts.size() >= 2 && parts[1] == "kB") {
                    const auto value = parse_u64(parts[0].view());
                    if (value.has_value()) {
                        out.set(key, json(*value * 1024));
                        continue;
                    }
                }
                if (key.size() > 3 && starts_with(key.view(), "Vm") || key == "RssAnon" || key == "RssFile" ||
                    key == "RssShmem") {
                    out.set(key, json(raw));
                    continue;
                }
                const auto number = parse_u64(raw.view());
                if (number.has_value()) {
                    out.set(key, json(*number));
                } else {
                    out.set(key, json(raw));
                }
            }
            return out;
        }

        json parse_process_stat(const string& text) {
            json out = json::object();
            const auto close = text.rfind(')');
            if (close == string::npos) {
                return out;
            }
            const auto fields = split_whitespace(text.view().substr(close + 1));

            static constexpr struct {
                string_view name;
                size_t index;
            } mapping[] = {
                    {"pgrp", 2},
                    {"session", 3},
                    {"tty", 4},
                    {"tpgid", 5},
                    {"minor_faults", 7},
                    {"minor_faults_children", 8},
                    {"major_faults", 9},
                    {"major_faults_children", 10},
                    {"utime_ticks", 11},
                    {"stime_ticks", 12},
                    {"children_utime_ticks", 13},
                    {"children_stime_ticks", 14},
                    {"priority", 15},
                    {"threads", 17},
                    {"processor", 36},
                    {"rt_priority", 37},
                    {"scheduling", 38},
            };

            for (const auto& item: mapping) {
                if (item.index >= fields.size()) {
                    continue;
                }
                const auto value = parse_i64(fields[item.index].view());
                if (value.has_value()) {
                    out.set(item.name, json(*value));
                }
            }
            return out;
        }

        string read_field(const string& file_path) {
            const auto text = read_text_file(file_path);
            return text.has_value() ? trim(text->view()) : string();
        }

    } // namespace

    struct advanced::impl {
#if NEFLUX_PLATFORM_LINUX
        struct counter_entry {
            uint64_t value = 0;
            int64_t at = 0;
        };
        struct process_fields_entry {
            int64_t at = 0;
            json values = json::object();
        };
        map<string, counter_entry> host_counters;
        map<string, counter_entry> energy;
        map<string, vector<uint64_t>> disk;
        int64_t disk_at = 0;
        int64_t host_at = 0;
        map<string, process_fields_entry> process_fields;
        struct engine_previous {
            uint64_t ns = 0;
            int64_t at = 0;
        };
        map<string, engine_previous> engines;
        taskstats_collector delays;
        uint64_t page_size = 4096;
        uint64_t ticks = 100;

        impl() {
            const long page = ::sysconf(_SC_PAGESIZE);
            page_size = page > 0 ? static_cast<uint64_t>(page) : 4096;
            const long clock = ::sysconf(_SC_CLK_TCK);
            ticks = clock > 0 ? static_cast<uint64_t>(clock) : 100;
            host_at = monotonic_ms();
            disk_at = host_at;
        }
#endif
    };

    advanced::advanced() :
    impl_(make_unique<impl>()) {}

    advanced::~advanced() = default;

    void advanced::collect(snapshot& value, const config& cfg) {
#if NEFLUX_PLATFORM_LINUX
        const int64_t now = monotonic_ms();
        const auto stat_text = read_text_file("/proc/stat").value_or(string());
        json scheduler = json::object();
        map<string, uint64_t> counters;
        static constexpr struct {
            string_view key;
            string_view label;
        } host_fields[] = {
                {"ctxt", "context_switches"},        {"intr", "interrupts"},
                {"softirq", "soft_interrupts"},      {"processes", "processes_created"},
                {"procs_running", "runnable_tasks"}, {"procs_blocked", "blocked_tasks"},
        };
        for (const auto& item: host_fields) {
            for (const auto& line: split_lines(stat_text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 2 || parts[0] != item.key) {
                    continue;
                }
                const auto current = parse_u64(parts[1].view());
                if (!current.has_value()) {
                    continue;
                }
                scheduler.set(format("{}_total", item.label), json(*current));
                const auto found = impl_->host_counters.find(item.key);
                if (found != impl_->host_counters.end() && now > found->second.at && *current >= found->second.value &&
                    !starts_with(string_view(item.key), "procs_")) {
                    scheduler.set(format("{}_per_second", item.label),
                                  json(static_cast<double>(*current - found->second.value) * 1000.0 /
                                       static_cast<double>(now - found->second.at)));
                }
                counters[item.key] = *current;
                break;
            }
        }
        const auto vmstat = parse_key_value(read_text_file("/proc/vmstat").value_or(string()));
        for (const auto& item: vector<pair<const char*, const char*>>{{"pswpin", "swap_in"}, {"pswpout", "swap_out"}}) {
            const json* pages = vmstat.get(item.first);
            if (pages == nullptr || !(pages->is_integer() || pages->is_number())) {
                continue;
            }
            const uint64_t bytes = static_cast<uint64_t>(pages->as_integer()) * impl_->page_size;
            scheduler.set(format("{}_bytes", item.second), json(bytes));
            const auto found = impl_->host_counters.find(item.first);
            if (found != impl_->host_counters.end() && now > found->second.at && bytes >= found->second.value) {
                scheduler.set(format("{}_bytes_per_second", item.second),
                              json(static_cast<double>(bytes - found->second.value) * 1000.0 /
                                   static_cast<double>(now - found->second.at)));
            }
            counters[item.first] = bytes;
        }
        impl_->host_counters.clear();
        for (const auto& item: counters) {
            impl_->host_counters[item.first] = impl::counter_entry{item.second, now};
        }
        impl_->host_at = now;
        value.extra_.set("CPU scheduler / swap IO", move(scheduler));

        json power = json::array();
        for (const auto& entry: path("/sys/class/powercap").children()) {
            const auto energy = read_field(entry.str() + "/energy_uj");
            const auto energy_value = parse_u64(energy.view());
            if (!energy_value.has_value()) {
                continue;
            }
            optional<double> watts;
            const auto found = impl_->energy.find(entry.str());
            if (found != impl_->energy.end() && now > found->second.at) {
                uint64_t delta = 0;
                if (*energy_value >= found->second.value) {
                    delta = *energy_value - found->second.value;
                } else {
                    const auto maximum = parse_u64(read_field(entry.str() + "/max_energy_range_uj").view());
                    if (maximum.has_value()) {
                        delta = *maximum - found->second.value + *energy_value;
                    }
                }
                watts = static_cast<double>(delta) / 1e6 * 1000.0 / static_cast<double>(now - found->second.at);
            }
            impl_->energy[entry.str()] = impl::counter_entry{*energy_value, now};
            json item = json::object();
            item.set("name", json(read_field(entry.str() + "/name")));
            item.set("watts", watts.has_value() ? json(*watts) : json());
            item.set("energy_uj", json(*energy_value));
            power.push(move(item));
        }
        if (!power.empty()) {
            value.extra_.set("CPU power (RAPL)", move(power));
        }

        const auto disk_text = read_text_file("/proc/diskstats").value_or(string());
        map<string, vector<uint64_t>> disk_counters;
        for (const auto& line: split_lines(disk_text)) {
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 14) {
                continue;
            }
            vector<uint64_t> values;
            bool valid = true;
            for (size_t index = 3; index < parts.size(); ++index) {
                const auto number = parse_u64(parts[index].view());
                if (!number.has_value()) {
                    valid = false;
                    break;
                }
                values.push_back(*number);
            }
            if (valid) {
                disk_counters[parts[2]] = move(values);
            }
        }
        if (impl_->disk_at > 0 && now > impl_->disk_at) {
            const double seconds = static_cast<double>(now - impl_->disk_at) / 1000.0;
            for (auto& item: value.disk_io_) {
                const auto current = disk_counters.find(item.name_);
                const auto previous = impl_->disk.find(item.name_);
                if (current == disk_counters.end() || previous == impl_->disk.end() || current->second.size() < 11 ||
                    previous->second.size() < 11) {
                    continue;
                }
                const auto delta = [&](size_t index) -> optional<uint64_t> {
                    return current->second[index] >= previous->second[index]
                                   ? optional<uint64_t>(current->second[index] - previous->second[index])
                                   : none;
                };
                const auto read_ops = delta(0);
                const auto write_ops = delta(4);
                if (read_ops.has_value()) {
                    item.read_iops_ = static_cast<double>(*read_ops) / seconds;
                }
                if (write_ops.has_value()) {
                    item.write_iops_ = static_cast<double>(*write_ops) / seconds;
                }
                if (read_ops.has_value() && write_ops.has_value() && *read_ops + *write_ops > 0 &&
                    delta(3).has_value() && delta(7).has_value()) {
                    item.await_ms_ =
                            static_cast<double>(*delta(3) + *delta(7)) / static_cast<double>(*read_ops + *write_ops);
                }
                if (delta(10).has_value()) {
                    item.queue_depth_ = static_cast<double>(*delta(10)) / 1000.0 / seconds;
                }
            }
        }
        impl_->disk = move(disk_counters);
        impl_->disk_at = now;

        const bool delay_requested = any_column(cfg, {"cpu_delay", "io_delay", "swap_delay"});
        if (delay_requested) {
            set<pair<uint32_t, uint64_t>> identities;
            for (const auto& item: value.processes_) {
                identities.insert({item.pid_, item.start_time_});
            }
            impl_->delays.retain(identities);
            const int64_t delay_began = monotonic_ms();
            for (auto& item: value.processes_) {
                if (monotonic_ms() - delay_began >= 100) {
                    break;
                }
                try {
                    const json fields = impl_->delays.collect(item.pid_, item.start_time_, item.thread_);
                    for (const auto& field: fields.as_object()) {
                        item.extra_.set(field.first, field.second);
                    }
                } catch (const exception& error) {
                    item.extra_.set("delay_accounting_status", json(error.what()));
                }
            }
        }

        // DRM fdinfo gives AMD/Intel per-process VRAM and engine counters.
        map<string, uint32_t> cards;
        for (const auto& item: value.gpus_) {
            if (contains_ignore_case(item.backend_.view(), "sysfs")) {
                cards[item.uuid_] = item.id_;
            }
        }
        if (!cards.empty()) {
            map<string, impl::engine_previous> next_engines;
            for (auto& process_item: value.processes_) {
                set<string> seen_clients;
                map<string, double> engines;
                map<uint32_t, uint64_t> gpu_memory;
                map<uint32_t, double> gpu_utilization;
                json clients = json::array();
                set<uint32_t> gpu_ids;
                for (const auto& fd_entry: path(format("/proc/{}/fd", process_item.pid_)).children()) {
                    char target[512] = {};
                    const auto length = ::readlink(fd_entry.str().data(), target, sizeof(target) - 1);
                    if (length <= 0 || !starts_with(string_view(target, static_cast<size_t>(length)), "/dev/dri")) {
                        continue;
                    }
                    const auto slash = fd_entry.str().rfind('/');
                    const string fd = slash == string::npos ? fd_entry.str() : fd_entry.str().substr(slash + 1);
                    const auto info_text = read_text_file(format("/proc/{}/fdinfo/{}", process_item.pid_, fd));
                    if (!info_text.has_value()) {
                        continue;
                    }
                    map<string, string> fields;
                    for (const auto& line: split_lines(*info_text)) {
                        const auto colon = line.find(':');
                        if (colon != string::npos) {
                            fields[trim(line.view().substr(0, colon))] = trim(line.view().substr(colon + 1));
                        }
                    }
                    const auto client = fields.find("drm-client-id");
                    const auto pci = fields.find("drm-pdev");
                    if (client == fields.end() || pci == fields.end()) {
                        continue;
                    }
                    const auto card = cards.find(pci->second);
                    if (card == cards.end()) {
                        continue;
                    }
                    const string key = pci->second + ":" + client->second;
                    if (!seen_clients.insert(key).second) {
                        continue;
                    }
                    gpu_ids.insert(card->second);
                    json client_json = json::object();
                    client_json.set("gpu_id", json(card->second));
                    json field_json = json::object();
                    for (const auto& field: fields) {
                        field_json.set(field.first, json(field.second));
                    }
                    client_json.set("fields", move(field_json));
                    clients.push(move(client_json));
                    for (const auto& field: fields) {
                        if (!starts_with(field.first.view(), "drm-engine-") ||
                            starts_with(field.first.view(), "drm-engine-capacity-")) {
                            continue;
                        }
                        const auto nanoseconds = parse_u64(split_whitespace(field.second.view()).empty()
                                                                   ? string_view()
                                                                   : split_whitespace(field.second.view())[0].view());
                        if (!nanoseconds.has_value()) {
                            continue;
                        }
                        const string identity = format("{}:{}:{}:{}:{}", process_item.pid_, process_item.start_time_,
                                                       pci->second, client->second, field.first);
                        const auto previous = impl_->engines.find(identity);
                        if (previous != impl_->engines.end()) {
                            double utilization = 0.0;
                            if (now > previous->second.at && *nanoseconds >= previous->second.ns) {
                                utilization = static_cast<double>(*nanoseconds - previous->second.ns) * 1000.0 /
                                              static_cast<double>(now - previous->second.at) / 1e7;
                            }
                            const auto capacity_field = fields.find("drm-engine-capacity-" + field.first.substr(12));
                            double capacity = 1.0;
                            if (capacity_field != fields.end()) {
                                const auto ivalue = parse_f64(capacity_field->second.view());
                                if (ivalue.has_value()) {
                                    capacity = max(1.0, *ivalue);
                                }
                            }
                            utilization = clamp(utilization / capacity, 0.0, 100.0);
                            const string name = format("GPU{} {}", card->second, field.first.substr(11));
                            engines[name] += utilization;
                        }
                        next_engines[identity] = impl::engine_previous{*nanoseconds, now};
                        const json* existing = process_item.extra_.get("drm_engine_time_ns");
                        const uint64_t total = existing != nullptr ? static_cast<uint64_t>(existing->as_integer()) : 0;
                        process_item.extra_.set("drm_engine_time_ns", json(total + *nanoseconds));
                    }
                    for (const char* key_name: {"drm-resident-vram", "drm-memory-vram"}) {
                        const auto memory = fields.find(key_name);
                        if (memory == fields.end()) {
                            continue;
                        }
                        const auto bytes = parse_size(memory->second.view());
                        if (bytes.has_value()) {
                            gpu_memory[card->second] += *bytes;
                        }
                    }
                }
                for (const auto& entry: gpu_memory) {
                    process_item.gpu_memory_ =
                            (process_item.gpu_memory_.has_value() ? *process_item.gpu_memory_ : 0) + entry.second;
                    process_item.gpu_ids_.push_back(entry.first);
                }
                for (const auto& entry: engines) {
                    const auto space = entry.first.find(' ');
                    if (space == string::npos) {
                        continue;
                    }
                    const auto id = parse_u64(entry.first.view().substr(3, space - 3));
                    if (id.has_value()) {
                        gpu_utilization[static_cast<uint32_t>(*id)] =
                                max(gpu_utilization[static_cast<uint32_t>(*id)], min(entry.second, 100.0));
                    }
                }
                if (!engines.empty()) {
                    json engine_json = json::object();
                    double total = 0.0;
                    for (const auto& entry: engines) {
                        engine_json.set(entry.first, json(entry.second));
                        total += entry.second;
                    }
                    process_item.extra_.set("DRM engines (%)", move(engine_json));
                    process_item.gpu_percent_ = static_cast<uint32_t>(min(
                            100.0, (process_item.gpu_percent_.has_value() ? *process_item.gpu_percent_ : 0) + total));
                }
                if (!clients.empty()) {
                    process_item.extra_.set("DRM clients (VRAM and shared memory counters)", move(clients));
                }
                for (const uint32_t id: gpu_ids) {
                    for (auto& device: value.gpus_) {
                        if (device.id_ != id) {
                            continue;
                        }
                        gpu_process entry;
                        entry.pid_ = process_item.pid_;
                        if (gpu_memory.count(id) != 0) {
                            entry.memory_ = gpu_memory[id];
                        }
                        if (gpu_utilization.count(id) != 0) {
                            entry.utilization_ = static_cast<uint32_t>(gpu_utilization[id]);
                        }
                        entry.kind_ = "DRM";
                        device.processes_.push_back(move(entry));
                        device.process_status_ = "DRM fdinfo (readable processes)";
                        break;
                    }
                }
            }
            impl_->engines = move(next_engines);
        }

        const bool need_status =
                any_column(cfg, {"uid", "tgid", "data", "code", "lib", "swap", "context_switches", "uid_effective",
                                 "uid_saved", "gid", "gid_effective", "nspid", "seccomp", "capabilities",
                                 "voluntary_context_switches", "nonvoluntary_context_switches"});
        const bool need_io = any_column(cfg, {"rchar", "wchar", "syscr", "syscw", "cancelled_write"});
        const bool need_smaps = any_column(cfg, {"pss", "pss_swap", "epss", "uss"});
        const bool need_statm = any_column(cfg, {"shared", "private"});
        const bool need_stat =
                any_column(cfg, {"priority", "processor", "threads", "pgrp", "session", "tty", "minor_faults",
                                 "major_faults", "utime", "stime", "cutime", "cstime", "tpgid", "minor_faults_children",
                                 "major_faults_children", "rt_priority", "scheduling", "cpu_time_children"});
        const bool need_extra =
                any_column(cfg, {"oom", "io_priority", "cgroup", "autogroup", "autogroup_nice", "comm", "exe", "cwd",
                                 "security", "wchan", "exe_deleted", "fd_count", "autogroup_id"});
        set<string> identities;
        for (const auto& item: value.processes_) {
            identities.insert(format("{}:{}", item.pid_, item.start_time_));
        }
        vector<string> stale_fields;
        for (const auto& entry: impl_->process_fields) {
            if (identities.count(entry.first) == 0) {
                stale_fields.push_back(entry.first);
            }
        }
        for (const auto& key: stale_fields) {
            impl_->process_fields.erase(key);
        }
        for (auto& item: value.processes_) {
            item.extra_.set("clock_ticks_per_second", json(impl_->ticks));
            const string identity = format("{}:{}", item.pid_, item.start_time_);
            const auto found = impl_->process_fields.find(identity);
            const bool refresh = found == impl_->process_fields.end() || now - found->second.at > 5000;
            if ((need_status || need_io || need_smaps || need_statm || need_stat || need_extra) && refresh) {
                json values = json::object();
                if (need_status) {
                    values.set(
                            "status",
                            parse_key_value(read_text_file(format("/proc/{}/status", item.pid_)).value_or(string())));
                }
                if (need_io) {
                    values.set("io",
                               parse_key_value(read_text_file(format("/proc/{}/io", item.pid_)).value_or(string())));
                }
                if (need_smaps) {
                    values.set("smaps_rollup",
                               parse_key_value(
                                       read_text_file(format("/proc/{}/smaps_rollup", item.pid_)).value_or(string())));
                }
                if (need_statm) {
                    const auto statm = split_whitespace(
                            read_text_file(format("/proc/{}/statm", item.pid_)).value_or(string()).view());
                    if (statm.size() >= 3) {
                        const auto shared = parse_u64(statm[2].view());
                        if (shared.has_value()) {
                            values.set("shared_bytes", json(*shared * impl_->page_size));
                        }
                    }
                }
                if (need_stat) {
                    values.set("stat", parse_process_stat(
                                               read_text_file(format("/proc/{}/stat", item.pid_)).value_or(string())));
                }
                if (any_column(cfg, {"oom"})) {
                    values.set("oom", json(read_field(format("/proc/{}/oom_score", item.pid_))));
                }
                if (any_column(cfg, {"cgroup"})) {
                    values.set("cgroup", json(read_field(format("/proc/{}/cgroup", item.pid_))));
                }
                if (any_column(cfg, {"autogroup", "autogroup_nice", "autogroup_id"})) {
                    values.set("autogroup", json(read_field(format("/proc/{}/autogroup", item.pid_))));
                }
                if (any_column(cfg, {"comm"})) {
                    values.set("comm", json(read_field(format("/proc/{}/comm", item.pid_))));
                }
                if (any_column(cfg, {"security"})) {
                    values.set("security", json(read_field(format("/proc/{}/attr/current", item.pid_))));
                }
                if (any_column(cfg, {"wchan"})) {
                    values.set("wchan", json(read_field(format("/proc/{}/wchan", item.pid_))));
                }
                if (any_column(cfg, {"exe", "exe_deleted", "cwd"})) {
                    const string exe = ::neforce::process::get_executable_path(item.pid_);
                    if (!exe.empty()) {
                        values.set("exe", json(exe));
                        values.set("exe_deleted", json(ends_with(exe.view(), " (deleted)")));
                    }
                    char target[4096] = {};
                    const auto cwd_length =
                            ::readlink(format("/proc/{}/cwd", item.pid_).data(), target, sizeof(target) - 1);
                    if (cwd_length > 0) {
                        target[cwd_length] = '\0';
                        values.set("cwd", json(target));
                    }
                }
                if (any_column(cfg, {"fd_count"})) {
                    values.set("fd_count", json(path(format("/proc/{}/fd", item.pid_)).children().size()));
                }
                values.set("details_sampled_at", json(unix_time()));
                impl_->process_fields[identity] = impl::process_fields_entry{now, move(values)};
            }
            if (impl_->process_fields.count(identity) != 0) {
                for (const auto& field: impl_->process_fields[identity].values.as_object()) {
                    item.extra_.set(field.first, field.second);
                }
            }
        }
#else
        static_cast<void>(value);
        static_cast<void>(cfg);
#endif
    }

    json discover_platform_extras(const config& cfg) {
        json extra = json::object();
#if NEFLUX_PLATFORM_LINUX
        try {
            json sensors = json::array();
            for (const auto& hwmon: path("/sys/class/hwmon").children()) {
                const string device = read_field(hwmon.str() + "/name");
                for (const auto& file: hwmon.children(true)) {
                    const auto name = file.filename();
                    if (!name.ends_with("_input") || !file.is_file()) {
                        continue;
                    }
                    const auto prefix_end = name.find('_');
                    if (prefix_end == string_view::npos) {
                        continue;
                    }
                    const string prefix(name.data(), prefix_end);
                    const auto value = parse_f64(read_field(file.str()).view());
                    if (!value.has_value() || !is_finite(*value)) {
                        continue;
                    }
                    double divisor = 1.0;
                    string unit;
                    if (starts_with(prefix.view(), "fan")) {
                        unit = "RPM";
                    } else if (starts_with(prefix.view(), "in")) {
                        divisor = 1000.0;
                        unit = "V";
                    } else if (starts_with(prefix.view(), "power")) {
                        divisor = 1e6;
                        unit = "W";
                    } else if (starts_with(prefix.view(), "curr")) {
                        divisor = 1000.0;
                        unit = "A";
                    } else {
                        continue;
                    }
                    const auto label =
                            read_field(hwmon.str() + "/" + string(name.substr(0, name.size() - 6)) + "_label");
                    json item = json::object();
                    item.set("device", json(device));
                    item.set("label", json(label.empty() ? string(name) : label));
                    item.set("value", json(*value / divisor));
                    item.set("unit", json(unit));
                    sensors.push(move(item));
                }
            }
            if (!sensors.empty()) {
                extra.set("Hardware sensors", move(sensors));
            }
        } catch (const exception& e) {
            NEFORCE_REPORT_EXCEPTION(e);
            // hwmon may be partially unreadable; keep the rest of the extras.
        }
        if (const auto meminfo = read_text_file("/proc/meminfo"); meminfo.has_value()) {
            extra.set("Memory details", parse_key_value(*meminfo));
        }
        json numa = json::array();
        for (const auto& node: sysinfo::instance().get_numa_info()) {
            const string name = format("node{}", node.node_id);
            string cpulist;
            uint32_t previous_core = 0;
            bool range = false;
            for (size_t index = 0; index < node.core_list.size(); ++index) {
                const uint32_t core = node.core_list[index];
                if (index == 0) {
                    cpulist.append(format("{}", core));
                } else if (core == previous_core + 1) {
                    range = true;
                } else {
                    if (range) {
                        cpulist.append(format("-{}", previous_core));
                    }
                    cpulist.append(format(",{}", core));
                    range = false;
                }
                if (index + 1 < node.core_list.size()) {
                    previous_core = core;
                } else if (range) {
                    cpulist.append(format("-{}", core));
                }
            }
            json item = json::object();
            item.set("node", json(name));
            item.set("cpulist", json(cpulist));
            const auto node_meminfo =
                    read_text_file(format("/sys/devices/system/node/{}/meminfo", name)).value_or(string());
            optional<uint64_t> node_total;
            for (const auto& line: split_lines(node_meminfo)) {
                const auto colon = line.find(':');
                if (colon == string::npos || !line.contains("MemTotal")) {
                    continue;
                }
                const auto parts = split_whitespace(line.view().substr(colon + 1));
                const auto value = parts.empty() ? none : parse_u64(parts[0].view());
                if (value.has_value()) {
                    node_total = *value * 1024;
                }
                break;
            }
            item.set("memory_total_bytes", node_total.has_value() ? json(*node_total) : json());
            numa.push(move(item));
        }
        if (!numa.empty()) {
            extra.set("NUMA", move(numa));
        }
        if (const auto interrupts = read_text_file("/proc/interrupts"); interrupts.has_value()) {
            json items = json::array();
            uint64_t total = 0;
            bool first = true;
            for (const auto& line: split_lines(*interrupts)) {
                if (first) {
                    first = false;
                    continue;
                }
                const auto colon = line.find(':');
                if (colon == string::npos) {
                    continue;
                }
                const auto parts = split_whitespace(line.view().substr(colon + 1));
                uint64_t sum = 0;
                for (const auto& part: parts) {
                    const auto value = parse_u64(part.view());
                    if (value.has_value() && !part.empty() && part[0] >= '0' && part[0] <= '9') {
                        sum += *value;
                    }
                }
                if (sum == 0) {
                    continue;
                }
                total += sum;
                json item = json::object();
                item.set("irq", json(trim(line.view().substr(0, colon))));
                item.set("count", json(sum));
                items.push(move(item));
            }
            json summary = json::object();
            summary.set("total", json(total));
            summary.set("items", move(items));
            extra.set("Interrupts", move(summary));
        }
        if (const auto softirqs = read_text_file("/proc/softirqs"); softirqs.has_value()) {
            json items = json::array();
            uint64_t total = 0;
            for (const auto& line: split_lines(*softirqs)) {
                const auto colon = line.find(':');
                if (colon == string::npos) {
                    continue;
                }
                uint64_t sum = 0;
                for (const auto& part: split_whitespace(line.view().substr(colon + 1))) {
                    const auto value = parse_u64(part.view());
                    if (value.has_value()) {
                        sum += *value;
                    }
                }
                total += sum;
                json item = json::object();
                item.set("type", json(trim(line.view().substr(0, colon))));
                item.set("count", json(sum));
                items.push(move(item));
            }
            json summary = json::object();
            summary.set("total", json(total));
            summary.set("items", move(items));
            extra.set("Soft IRQ", move(summary));
        }
        if (const auto vmstat = read_text_file("/proc/vmstat"); vmstat.has_value()) {
            json counters = json::object();
            for (const auto& line: split_lines(*vmstat)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 2) {
                    continue;
                }
                const auto value = parse_u64(parts[1].view());
                if (value.has_value()) {
                    counters.set(parts[0], json(*value));
                }
            }
            extra.set("VM counters", move(counters));
        }
        if (const auto raid = read_text_file("/proc/mdstat"); raid.has_value() && !trim(raid->view()).empty()) {
            extra.set("Software RAID", json(*raid));
        }
        if (path("/dev/kvm").exists()) {
            json virtual_machines = json::object();
            virtual_machines.set("kvm", json(true));
            extra.set("Local virtual machines", move(virtual_machines));
        }
        if (!cfg.monitored_folders_.empty()) {
            json folders = json::array();
            for (const auto& folder: cfg.monitored_folders_) {
                json item = json::object();
                item.set("path", json(folder));
                uint64_t bytes_total = 0;
                uint64_t files = 0;
                uint64_t directories = 0;
                bool complete = true;
                vector<string> pending{folder};
                constexpr size_t limit = 20000;
                while (!pending.empty()) {
                    if (files + directories >= limit) {
                        complete = false;
                        break;
                    }
                    const string current = pending.back();
                    pending.pop_back();
                    try {
                        for (const auto& child: path(current).children(true)) {
                            if (child.is_directory()) {
                                ++directories;
                                pending.push_back(child.str());
                            } else if (child.is_file()) {
                                ++files;
                                bytes_total += filesystem::size(child).bytes();
                            }
                        }
                    } catch (const exception& error) {
                        complete = false;
                        item.set("error", json(error.what()));
                        break;
                    }
                }
                item.set("bytes", json(bytes_total));
                item.set("files", json(files));
                item.set("directories", json(directories));
                item.set("complete", json(complete));
                folders.push(move(item));
            }
            extra.set("Monitored folders", move(folders));
        }
        if (cfg.config_exec_ && !cfg.probes_.empty()) {
            json probes = json::object();
            for (const auto& probe: cfg.probes_) {
                try {
                    const string output = run_command(probe.command_[0],
                                                      vector<string>(probe.command_.begin() + 1, probe.command_.end()),
                                                      static_cast<int>(probe.timeout_seconds_ * 1000));
                    json item = json::object();
                    item.set("output", json(output));
                    item.set("timeout_seconds", json(probe.timeout_seconds_));
                    probes.set(probe.name_, move(item));
                } catch (const exception& error) {
                    json item = json::object();
                    item.set("error", json(error.what()));
                    probes.set(probe.name_, move(item));
                }
            }
            extra.set("Local probes", move(probes));
        }
        if (cfg.config_exec_ && cfg.process_network_) {
            static constexpr string_view script =
                    "kprobe:tcp_recvmsg { @peek[tid] = arg3 & 2; } "
                    "kretprobe:tcp_sendmsg /retval > 0/ { @tx[pid] = sum(retval); } "
                    "kretprobe:tcp_recvmsg /retval > 0 && !@peek[tid]/ { @rx[pid] = sum(retval); delete(@peek[tid]); } "
                    "interval:s:1 { print(@tx); print(@rx); clear(@tx); clear(@rx); clear(@peek); exit(); } "
                    "END { clear(@tx); clear(@rx); clear(@peek); }";
            try {
                json item = json::object();
                item.set("output", json(run_command("bpftrace", {"-q", "-f", "json", "-e", script}, 8000)));
                extra.set("Process TCP bytes / 1s (bpftrace)", move(item));
            } catch (const exception& error) {
                json item = json::object();
                item.set("error", json(error.what()));
                extra.set("Process TCP bytes / 1s (bpftrace)", move(item));
            }
        }
        try {
            const string output = run_command("smartctl", {"--scan-open", "--json"}, 3000);
            const json scan = json::parse(output.view());
            const json* devices = scan.get("devices");
            if (devices != nullptr && devices->is_array()) {
                size_t count = 0;
                for (const auto& device: devices->as_array()) {
                    if (count++ >= 32) {
                        break;
                    }
                    const json* name = device.get("name");
                    if (name == nullptr || !name->is_string()) {
                        continue;
                    }
                    try {
                        const string detail = run_command("smartctl", {"-a", "--json", name->as_string()}, 3000, true);
                        extra.set(format("SMART {}", name->as_string()), json::parse(detail.view()));
                    } catch (const exception& error) {
                        json item = json::object();
                        item.set("error", json(error.what()));
                        extra.set(format("SMART {}", name->as_string()), move(item));
                    }
                }
            }
        } catch (const exception& e) {
            NEFORCE_REPORT_EXCEPTION(e);
            // smartctl is optional.
        }
#else
        static_cast<void>(cfg);
#endif
        return extra;
    }

} // namespace neforce::neflux
