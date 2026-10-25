#include <NeFlux/common.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#if NEFLUX_PLATFORM_WINDOWS
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#endif
namespace neforce::neflux {

    namespace {

        string field(const json& value, std::initializer_list<const char*> names) {
            for (const char* name: names) {
                const json* item = value.get(name);
                if (item == nullptr) {
                    continue;
                }
                if (item->is_string()) {
                    return item->as_string();
                }
                if (item->is_number() || item->is_integer()) {
                    return format("{}", item->as_integer());
                }
                if (item->is_bool()) {
                    return item->as_bool() ? "true" : "false";
                }
            }
            return {};
        }

        string docker_host() {
            auto host = environment::get("DOCKER_HOST");
            if (!host.empty()) {
                return move(host);
            }
#if NEFLUX_PLATFORM_WINDOWS
            return "npipe:////./pipe/docker_engine";
#else
            return "unix:///var/run/docker.sock";
#endif
        }

        bool docker_endpoint_local() {
            const auto host = environment::get("DOCKER_HOST");
            if (host.empty()) {
                return true;
            }
            if (starts_with(host.view(), "unix://") || starts_with(host.view(), "npipe://")) {
                return true;
            }
            if (starts_with(host.view(), "tcp://")) {
                const string authority = host.substr(6);
                const auto colon = authority.rfind(':');
                const string name = colon == string::npos ? authority : authority.substr(0, colon);
                return name == "localhost" || name == "127.0.0.1" || name == "[::1]";
            }
            return false;
        }

        vector<string> runtime_args(const string& runtime, const vector<string>& args) {
            vector<string> result;
            if (runtime == "docker") {
                result.push_back("--host");
                result.push_back(docker_host());
            } else if (runtime == "podman") {
                result.push_back("--remote=false");
            }
            for (const auto& item: args) {
                result.push_back(item);
            }
            return result;
        }

        void discover_runtime(inventory& data, const string& runtime) {
            if (runtime == "docker" && !docker_endpoint_local()) {
                data.notes_.push_back("docker: Nonlocal Docker context skipped (local monitoring only)");
                return;
            }
            const string not_installed_message = format("{} not installed or cannot be started", runtime);
            string listing;
            try {
                listing = run_command(
                        runtime, runtime_args(runtime, {"ps", "-a", "--no-trunc", "--format", "{{json .}}"}), 3000);
            } catch (const exception& error) {
                const string message = trim(error.what());
                if (!contains_ignore_case(message.view(), "not installed") &&
                    !message.contains(not_installed_message)) {
                    data.notes_.push_back(format("{}: {}", runtime, message));
                }
                return;
            }
            data.runtimes_.push_back(runtime);
            auto containers = parse_containers(runtime, listing);
            bool running = false;
            for (const auto& item: containers) {
                if (item.state_ == "running" || item.state_ == "Up") {
                    running = true;
                    break;
                }
            }
            if (running) {
                try {
                    const string stats = run_command(
                            runtime, runtime_args(runtime, {"stats", "--no-stream", "--format", "{{json .}}"}), 5000);
                    apply_container_stats(containers, stats);
                } catch (const exception& error) {
                    data.notes_.push_back(format("{} stats: {}", runtime, trim(error.what())));
                }
            }
            for (auto& item: containers) {
                data.containers_.push_back(move(item));
            }
        }

        void discover_ownership(inventory& data) {
            if (data.containers_.empty()) {
                return;
            }
            json cgroups = json::object();
            bool complete = true;
            uint64_t unreadable = 0;
            const int64_t deadline = monotonic_ms() + 200;
            for (const auto& full: ::neforce::path("/proc").children()) {
                if (monotonic_ms() > deadline) {
                    complete = false;
                    break;
                }
                const auto pid = parse_u64(full.filename());
                if (!pid.has_value()) {
                    continue;
                }
                const auto cgroup = read_text_file(full.str() + "/cgroup");
                if (!cgroup.has_value()) {
                    ++unreadable;
                    continue;
                }
                bool owned = false;
                for (const auto& container: data.containers_) {
                    const string short_id = container.id_.size() > 12 ? container.id_.substr(0, 12) : container.id_;
                    if (cgroup->contains(container.id_) || (!short_id.empty() && cgroup->contains(short_id))) {
                        owned = true;
                        break;
                    }
                }
                if (!owned) {
                    continue;
                }
                const auto stat = read_text_file(full.str() + "/stat");
                uint64_t start = 0;
                if (stat.has_value()) {
                    const auto close = stat->rfind(')');
                    if (close != string::npos) {
                        const auto fields = split_whitespace(stat->view().substr(close + 1));
                        if (fields.size() > 19) {
                            const auto value = parse_u64(fields[19].view());
                            if (value.has_value()) {
                                start = *value;
                            }
                        }
                    }
                }
                cgroups.set(format("{}:{}", *pid, start), json(*cgroup));
            }
            json ownership = json::object();
            ownership.set("cgroups", move(cgroups));
            ownership.set("complete", json(complete));
            ownership.set("unreadable_processes", json(unreadable));
            data.extra_.set("Application ownership", move(ownership));
        }

        string service_json_field(const json& value, const char* name) { return field(value, {name}); }

        void discover_systemd(inventory& data) {
#if NEFLUX_PLATFORM_WINDOWS
            try {
                const string script =
                        R"PS(Get-CimInstance Win32_Service | ForEach-Object { "$($_.Name)`t$($_.State)`t$($_.ProcessId)`t$($_.StartMode)`t$($_.Description)" })PS";
                const string output =
                        run_command("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", script}, 5000);
                data.service_managers_.push_back("Windows SCM");
                for (const auto& line: split_lines(output)) {
                    const auto fields = split(line.view(), '\t', true);
                    if (fields.size() < 3) {
                        continue;
                    }
                    service item;
                    item.name_ = fields[0];
                    item.scope_ = "system";
                    item.load_ = fields.size() > 3 ? fields[3] : "installed";
                    item.active_ = fields[1] == "Running" ? "active" : "inactive";
                    item.sub_ = fields[1];
                    const auto pid = parse_u64(fields[2].view());
                    if (pid.has_value() && *pid > 0) {
                        item.pid_ = static_cast<uint32_t>(*pid);
                    }
                    item.description_.clear();
                    for (size_t index = 4; index < fields.size(); ++index) {
                        if (!item.description_.empty()) {
                            item.description_.push_back('\t');
                        }
                        item.description_.append(fields[index]);
                    }
                    if (item.description_.empty()) {
                        item.description_ = "Windows service";
                    }
                    data.services_.push_back(move(item));
                }
            } catch (const exception& error) {
                data.notes_.push_back(format("Windows SCM: {}", error.what()));
            }
            return;
#else
            if (!::neforce::path("/run/systemd/system").exists()) {
                return;
            }
            for (const char* scope: {"system", "user"}) {
                const string mode = format("--{}", scope);
                string listing;
                try {
                    listing = run_command(
                            "systemctl",
                            {mode, "list-units", "--type=service", "--all", "--output=json", "--no-pager", "--plain"},
                            3000);
                } catch (const exception& error) {
                    data.notes_.push_back(format("systemd/{}: {}", scope, trim(error.what())));
                    continue;
                }
                data.service_managers_.push_back(format("systemd/{}", scope));
                json parsed;
                try {
                    parsed = json::parse(listing.view());
                } catch (const exception&) {
                    data.notes_.push_back(format("systemd/{}: invalid JSON", scope));
                    continue;
                }
                for (const auto& value: parsed.as_array()) {
                    service item;
                    item.name_ = service_json_field(value, "unit");
                    item.scope_ = scope;
                    item.load_ = service_json_field(value, "load");
                    item.active_ = service_json_field(value, "active");
                    item.sub_ = service_json_field(value, "sub");
                    item.description_ = service_json_field(value, "description");
                    data.services_.push_back(move(item));
                }
                try {
                    const string output = run_command("systemctl",
                                                      {mode, "show", "--all",
                                                       "--property=Id,MainPID,MemoryCurrent,CPUUsageNSec,TasksCurrent,"
                                                       "NRestarts,ControlGroup,FragmentPath",
                                                       "--", "*.service"},
                                                      3000);
                    size_t begin = 0;
                    while (begin <= output.size()) {
                        const size_t end = output.find("\n\n", begin);
                        const string block = output.substr(begin, end == string::npos ? string::npos : end - begin);
                        begin = end == string::npos ? output.size() + 1 : end + 2;
                        if (block.empty()) {
                            continue;
                        }
                        map<string, string> fields;
                        for (const auto& line: split_lines(block)) {
                            const auto equals = line.find('=');
                            if (equals == string::npos) {
                                continue;
                            }
                            fields[line.substr(0, equals)] = line.substr(equals + 1);
                        }
                        const auto found = fields.find("Id");
                        if (found == fields.end()) {
                            continue;
                        }
                        for (auto& item: data.services_) {
                            if (item.scope_ != scope || item.name_ != found->second) {
                                continue;
                            }
                            if (const auto pid = fields.find("MainPID"); pid != fields.end()) {
                                const auto value = parse_u64(pid->second.view());
                                if (value.has_value() && *value > 0) {
                                    item.pid_ = static_cast<uint32_t>(*value);
                                }
                            }
                            if (const auto memory = fields.find("MemoryCurrent"); memory != fields.end()) {
                                const auto value = parse_u64(memory->second.view());
                                if (value.has_value() && *value != neforce::numeric_traits<uint64_t>::max()) {
                                    item.memory_ = *value;
                                }
                            }
                            if (const auto cpu = fields.find("CPUUsageNSec"); cpu != fields.end()) {
                                const auto value = parse_u64(cpu->second.view());
                                if (value.has_value() && *value != neforce::numeric_traits<uint64_t>::max()) {
                                    item.cpu_time_ns_ = *value;
                                }
                            }
                            if (const auto tasks = fields.find("TasksCurrent"); tasks != fields.end()) {
                                const auto value = parse_u64(tasks->second.view());
                                if (value.has_value() && *value != neforce::numeric_traits<uint64_t>::max()) {
                                    item.tasks_ = *value;
                                }
                            }
                            item.extra_ = json::object();
                            for (const auto& entry: fields) {
                                item.extra_.set(entry.first, json(entry.second));
                            }
                            break;
                        }
                    }
                } catch (const exception& error) {
                    data.notes_.push_back(format("systemd/{} resources: {}", scope, trim(error.what())));
                }
                try {
                    const string output = run_command(
                            "systemctl", {mode, "list-unit-files", "--type=service", "--output=json", "--no-pager"},
                            3000);
                    const json parsed_files = json::parse(output.view());
                    for (const auto& value: parsed_files.as_array()) {
                        const string name = service_json_field(value, "unit_file");
                        bool existing = false;
                        for (const auto& item: data.services_) {
                            if (item.scope_ == scope && item.name_ == name) {
                                existing = true;
                                break;
                            }
                        }
                        if (!existing) {
                            service item;
                            item.name_ = name;
                            item.scope_ = scope;
                            item.load_ = "installed";
                            item.active_ = "not-loaded";
                            item.sub_ = service_json_field(value, "state");
                            item.description_ = "Unit file; not currently loaded";
                            data.services_.push_back(move(item));
                        }
                    }
                } catch (const exception&) {
                    // Unit file listing is optional; loaded units are already available.
                }
            }
#endif
        }

    } // namespace


    string run_command(const string& program, const vector<string>& args, int timeout_ms, bool include_stderr) {
        ::neforce::process handle;
        handle.set_capture_stdout(true).set_capture_stderr(true);
        handle.start(program, args);
        const int code = handle.wait(timeout_ms);
        if (code == -1) {
            handle.terminate();
            throw process_exception("query timed out or shutting down");
        }
        string output = handle.stdout_output();
        if (include_stderr) {
            const auto& error = handle.stderr_output();
            if (!error.empty()) {
                output.append("\n[stderr]\n");
                output.append(error);
            }
        }
        if (code != 0 && output.empty()) {
            const auto& error = handle.stderr_output();
            throw process_exception(error.empty() ? "query failed" : error.data());
        }
        return output;
    }

    void inventory_rates::apply(inventory& data) {
        if (began_ < 0.0) {
            began_ = static_cast<double>(monotonic_ms()) / 1000.0;
        }
        const double seconds = static_cast<double>(monotonic_ms()) / 1000.0 - began_;
        map<string, entry> current;
        for (auto& item: data.containers_) {
            const string key = item.runtime_ + "/" + item.id_;
            entry value;
            value.seconds = seconds;
            value.counters[0] = item.network_rx_.has_value() ? *item.network_rx_ : 0;
            value.counters[1] = item.network_tx_.has_value() ? *item.network_tx_ : 0;
            value.counters[2] = item.block_read_.has_value() ? *item.block_read_ : 0;
            value.counters[3] = item.block_write_.has_value() ? *item.block_write_ : 0;
            value.valid = item.network_rx_.has_value() && item.network_tx_.has_value() &&
                          item.block_read_.has_value() && item.block_write_.has_value();
            const auto previous = previous_.find(key);
            if (previous != previous_.end() && previous->second.valid && value.valid) {
                const double elapsed = seconds - previous->second.seconds;
                if (elapsed > 0.0) {
                    item.network_rx_per_sec_ =
                            static_cast<double>(value.counters[0] - previous->second.counters[0]) / elapsed;
                    item.network_tx_per_sec_ =
                            static_cast<double>(value.counters[1] - previous->second.counters[1]) / elapsed;
                    item.block_read_per_sec_ =
                            static_cast<double>(value.counters[2] - previous->second.counters[2]) / elapsed;
                    item.block_write_per_sec_ =
                            static_cast<double>(value.counters[3] - previous->second.counters[3]) / elapsed;
                }
            }
            current[key] = value;
        }
        previous_ = _NEFORCE move(current);
    }

    inventory discover_with_config(const config& cfg) { return discover_selected(cfg, true, true, true); }

    inventory discover_with_sources(const config& cfg, const discovery_sources& sources) {
        return discover_selected(cfg, sources.containers_, sources.services_, sources.extras_);
    }

    inventory discover_selected(const config& cfg, bool containers, bool services, bool extras) {
        inventory data;
        data.updated_at_ = unix_time();
        if (containers) {
            for (const char* runtime: {"docker", "podman"}) {
                discover_runtime(data, runtime);
            }
        }
        if (services) {
            discover_systemd(data);
        }
        stable_sort_by_index(data.containers_,
                             [](const container& a, const container& b) { return a.name_ < b.name_; });
        stable_sort_by_index(data.services_, [](const service& a, const service& b) {
            const bool left_failed = a.active_ == "failed";
            const bool right_failed = b.active_ == "failed";
            if (left_failed != right_failed) {
                return left_failed;
            }
            if (a.name_ != b.name_) {
                return a.name_ < b.name_;
            }
            return a.scope_ < b.scope_;
        });
        if (extras) {
            data.listeners_ = discover_listeners();
            data.extra_ = discover_platform_extras(cfg);
            if (!data.extra_.is_object()) {
                data.extra_ = json::object();
            }
            discover_ownership(data);
        }
        json state = json::object();
        state.set("containers", json(containers));
        state.set("services", json(services));
        state.set("extras", json(extras));
        data.extra_.set("Discovery state", move(state));
        static_cast<void>(cfg);
        return data;
    }

    vector<container> parse_containers(const string& runtime, const string& output) {
        vector<container> out;
        for (const auto& line: split_lines(output)) {
            if (trim(line.view()).empty()) {
                continue;
            }
            json value;
            try {
                value = json::parse(line.view());
            } catch (const exception&) {
                continue;
            }
            container item;
            item.id_ = field(value, {"ID", "Id", "id"});
            if (item.id_.empty()) {
                continue;
            }
            item.runtime_ = runtime;
            item.name_ = field(value, {"Names", "Name"});
            item.image_ = field(value, {"Image"});
            item.state_ = field(value, {"State"});
            item.status_ = field(value, {"Status"});
            item.ports_ = field(value, {"Ports"});
            item.command_ = field(value, {"Command"});
            item.created_ = field(value, {"CreatedAt", "Created"});
            item.running_for_ = field(value, {"RunningFor"});
            out.push_back(move(item));
        }
        return out;
    }

    void apply_container_stats(vector<container>& containers, const string& output) {
        for (const auto& line: split_lines(output)) {
            json value;
            try {
                value = json::parse(line.view());
            } catch (const exception&) {
                continue;
            }
            const string id = field(value, {"ID", "Id", "ContainerID", "Container"});
            const string name = field(value, {"Name"});
            for (auto& item: containers) {
                const bool matches = (!id.empty() && (starts_with(item.id_.view(), id.view()) ||
                                                      starts_with(id.view(), item.id_.view()))) ||
                                     (!name.empty() && item.name_ == name);
                if (!matches) {
                    continue;
                }
                const string cpu = field(value, {"CPUPerc", "CPU", "CPUPercent"});
                const auto cpu_value = parse_f64(trim(cpu.view()).view());
                if (cpu_value.has_value()) {
                    item.cpu_ = *cpu_value;
                }
                const string memory = field(value, {"MemUsage", "MemUsageBytes"});
                const auto slash = memory.find('/');
                if (slash != string::npos) {
                    item.memory_ = parse_size(memory.view().substr(0, slash));
                    item.memory_limit_ = parse_size(memory.view().substr(slash + 1));
                }
                const string network = field(value, {"NetIO"});
                const auto net_slash = network.find('/');
                if (net_slash != string::npos) {
                    item.network_rx_ = parse_size(network.view().substr(0, net_slash));
                    item.network_tx_ = parse_size(network.view().substr(net_slash + 1));
                }
                const string block = field(value, {"BlockIO"});
                const auto block_slash = block.find('/');
                if (block_slash != string::npos) {
                    item.block_read_ = parse_size(block.view().substr(0, block_slash));
                    item.block_write_ = parse_size(block.view().substr(block_slash + 1));
                }
                const string memory_percent = field(value, {"MemPerc", "MemPercent"});
                const auto memory_percent_value = parse_f64(trim(memory_percent.view()).view());
                if (memory_percent_value.has_value()) {
                    item.memory_percent_ = *memory_percent_value;
                }
                const string pids = field(value, {"PIDs", "PIDS"});
                const auto pids_value = parse_u64(trim(pids.view()).view());
                if (pids_value.has_value()) {
                    item.pids_ = *pids_value;
                }
                break;
            }
        }
    }

    optional<uint64_t> parse_size(string_view text) {
        const auto value = trim_view(text);
        if (value.empty()) {
            return none;
        }
        size_t end = 0;
        while (end < value.size() && ((value[end] >= '0' && value[end] <= '9') || value[end] == '.')) {
            ++end;
        }
        const auto number = parse_f64(value.substr(0, end));
        if (!number.has_value() || !neforce::is_finite(*number) || *number < 0.0) {
            return none;
        }
        const string unit = lower_ascii(trim_view(value.substr(end)));
        double multiplier = 1.0;
        if (unit.empty() || unit == "b") {
            multiplier = 1.0;
        } else if (unit == "kb") {
            multiplier = 1e3;
        } else if (unit == "mb") {
            multiplier = 1e6;
        } else if (unit == "gb") {
            multiplier = 1e9;
        } else if (unit == "tb") {
            multiplier = 1e12;
        } else if (unit == "kib" || unit == "ki") {
            multiplier = 1024.0;
        } else if (unit == "mib" || unit == "mi") {
            multiplier = 1024.0 * 1024.0;
        } else if (unit == "gib" || unit == "gi") {
            multiplier = 1024.0 * 1024.0 * 1024.0;
        } else if (unit == "tib" || unit == "ti") {
            multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
        } else {
            return none;
        }
        return static_cast<uint64_t>(neforce::round(*number * multiplier));
    }

    vector<listener> parse_listeners(const string& text, const string& protocol) {
        vector<listener> out;
        bool first = true;
        for (const auto& line: split_lines(text)) {
            if (first) {
                first = false;
                continue;
            }
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 8) {
                continue;
            }
            const bool tcp = starts_with(protocol.view(), "TCP");
            if (tcp && parts[3] != "0A") {
                continue;
            }
            if (!tcp && parts[3] != "07") {
                continue;
            }
            const auto colon = parts[1].rfind(':');
            if (colon == string::npos) {
                continue;
            }
            const string ip_hex = parts[1].substr(0, colon);
            const string port_hex = parts[1].substr(colon + 1);
            string address;
            if (ip_hex.size() == 8) {
                uint32_t value = 0;
                bool valid = true;
                for (const char c: ip_hex) {
                    const int digit = c >= '0' && c <= '9'
                                              ? c - '0'
                                              : (c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                      : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                    if (digit < 0) {
                        valid = false;
                        break;
                    }
                    value = value * 16 + static_cast<uint32_t>(digit);
                }
                if (!valid) {
                    continue;
                }
                address = format("{}.{}.{}.{}", value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF,
                                 (value >> 24) & 0xFF);
            } else if (ip_hex.size() == 32) {
                uint8_t bytes[16] = {};
                bool valid = true;
                for (size_t chunk = 0; chunk < 4; ++chunk) {
                    uint32_t value = 0;
                    for (size_t offset = 0; offset < 8; ++offset) {
                        const char c = ip_hex[chunk * 8 + offset];
                        const int digit = c >= '0' && c <= '9'
                                                  ? c - '0'
                                                  : (c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                          : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                        if (digit < 0) {
                            valid = false;
                            break;
                        }
                        value = value * 16 + static_cast<uint32_t>(digit);
                    }
                    if (!valid) {
                        break;
                    }
                    bytes[chunk * 4] = value & 0xFF;
                    bytes[chunk * 4 + 1] = (value >> 8) & 0xFF;
                    bytes[chunk * 4 + 2] = (value >> 16) & 0xFF;
                    bytes[chunk * 4 + 3] = (value >> 24) & 0xFF;
                }
                if (!valid) {
                    continue;
                }
                char buffer[64] = {};
                if (inet_ntop(AF_INET6, bytes, buffer, sizeof(buffer)) == nullptr) {
                    continue;
                }
                address = buffer;
            } else {
                continue;
            }
            uint32_t port = 0;
            bool port_valid = true;
            for (const char c: port_hex) {
                const int digit =
                        c >= '0' && c <= '9'
                                ? c - '0'
                                : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                if (digit < 0) {
                    port_valid = false;
                    break;
                }
                port = port * 16 + static_cast<uint32_t>(digit);
            }
            const auto uid = parse_u64(parts[7].view());
            if (!port_valid || !uid.has_value()) {
                continue;
            }
            listener item;
            item.protocol_ = protocol;
            item.address_ = address;
            item.port_ = static_cast<uint16_t>(port);
            item.uid_ = static_cast<uint32_t>(*uid);
            out.push_back(move(item));
        }
        return out;
    }

    vector<listener> discover_listeners() {
        vector<listener> out;
#if !NEFLUX_PLATFORM_LINUX
#    if NEFLUX_PLATFORM_WINDOWS
        try {
            const string text = run_command("netstat", {"-ano"}, 3000);
            for (const auto& line: split_lines(text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 4 || (parts[0] != "TCP" && parts[0] != "UDP")) {
                    continue;
                }
                if (parts[0] == "TCP" && parts[3] != "LISTENING") {
                    continue;
                }
                const auto colon = parts[1].rfind(':');
                if (colon == string::npos) {
                    continue;
                }
                listener item;
                item.protocol_ = parts[0];
                item.address_ = parts[1].substr(0, colon);
                const auto port = parse_u64(parts[1].substr(colon + 1).view());
                if (port.has_value()) {
                    item.port_ = static_cast<uint16_t>(*port);
                }
                out.push_back(move(item));
            }
        } catch (const exception& error) {
            static_cast<void>(error);
        }
#    elif NEFLUX_PLATFORM_MACOS
        try {
            const string text = run_command("lsof", {"-nP", "-iTCP", "-sTCP:LISTEN"}, 3000);
            for (const auto& line: split_lines(text)) {
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 9 || parts[0] == "COMMAND") {
                    continue;
                }
                const auto colon = parts[8].rfind(':');
                if (colon == string::npos) {
                    continue;
                }
                listener item;
                item.protocol_ = "TCP";
                item.address_ = parts[8].substr(0, colon);
                const auto port = parse_u64(parts[8].substr(colon + 1).view());
                if (port.has_value()) {
                    item.port_ = static_cast<uint16_t>(*port);
                }
                out.push_back(move(item));
            }
        } catch (const exception& error) {
            static_cast<void>(error);
        }
#    endif
        return out;
#endif
        for (const auto& pair_value: vector<pair<const char*, const char*>>{
                     {"tcp", "TCP"}, {"tcp6", "TCP6"}, {"udp", "UDP"}, {"udp6", "UDP6"}}) {
            const auto text = read_text_file(format("/proc/net/{}", pair_value.first));
            if (!text.has_value()) {
                continue;
            }
            auto items = parse_listeners(*text, pair_value.second);
            for (auto& item: items) {
                out.push_back(move(item));
            }
        }
        stable_sort_by_index(out, [](const listener& a, const listener& b) {
            if (a.port_ != b.port_) {
                return a.port_ < b.port_;
            }
            return a.protocol_ < b.protocol_;
        });
        return out;
    }

} // namespace neforce::neflux
