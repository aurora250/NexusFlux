#include <NeFlux/common.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>
#if NEFLUX_PLATFORM_WINDOWS
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <winsvc.h>
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

#if NEFLUX_PLATFORM_WINDOWS
        string service_start_mode(DWORD type) {
            switch (type) {
                case SERVICE_BOOT_START:
                    return "boot";
                case SERVICE_SYSTEM_START:
                    return "system";
                case SERVICE_AUTO_START:
                    return "auto";
                case SERVICE_DEMAND_START:
                    return "manual";
                case SERVICE_DISABLED:
                    return "disabled";
                default:
                    return "unknown";
            }
        }

        void service_state(const DWORD state, string& active, string& sub) {
            switch (state) {
                case SERVICE_RUNNING:
                    active = "active";
                    sub = "running";
                    break;
                case SERVICE_STOPPED:
                    active = "inactive";
                    sub = "stopped";
                    break;
                case SERVICE_START_PENDING:
                    active = "activating";
                    sub = "start-pending";
                    break;
                case SERVICE_STOP_PENDING:
                    active = "deactivating";
                    sub = "stop-pending";
                    break;
                case SERVICE_PAUSED:
                    active = "active";
                    sub = "paused";
                    break;
                case SERVICE_PAUSE_PENDING:
                    active = "deactivating";
                    sub = "pause-pending";
                    break;
                case SERVICE_CONTINUE_PENDING:
                    active = "activating";
                    sub = "continue-pending";
                    break;
                default:
                    active = "unknown";
                    sub = "unknown";
                    break;
            }
        }

        void discover_windows_services(inventory& data) {
            const ::SC_HANDLE manager =
                    ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
            if (manager == nullptr) {
                data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                return;
            }
            DWORD needed = 0;
            DWORD returned = 0;
            DWORD resume = 0;
            ::EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0,
                                    &needed, &returned, &resume, nullptr);
            if (needed == 0) {
                data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                ::CloseServiceHandle(manager);
                return;
            }
            vector<BYTE> buffer(static_cast<size_t>(needed) + 8192);
            for (;;) {
                if (::EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                            buffer.data(), static_cast<DWORD>(buffer.size()), &needed, &returned,
                                            &resume, nullptr) == FALSE) {
                    if (::GetLastError() == ERROR_MORE_DATA) {
                        buffer.resize(static_cast<size_t>(needed) + 8192);
                        continue;
                    }
                    data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                    break;
                }
                auto* entries = reinterpret_cast<::ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
                for (DWORD index = 0; index < returned; ++index) {
                    const auto& raw = entries[index];
                    service item;
                    item.name_ = wcharacter::to_string(raw.lpServiceName);
                    item.scope_ = "system";
                    item.description_ = wcharacter::to_string(raw.lpDisplayName);
                    item.extra_ = json::object();
                    item.extra_.set("display_name", json(item.description_));
                    service_state(raw.ServiceStatusProcess.dwCurrentState, item.active_, item.sub_);
                    if (raw.ServiceStatusProcess.dwProcessId != 0) {
                        item.pid_ = raw.ServiceStatusProcess.dwProcessId;
                    }
                    const ::SC_HANDLE handle = ::OpenServiceW(manager, raw.lpServiceName, SERVICE_QUERY_CONFIG);
                    if (handle != nullptr) {
                        DWORD config_size = 0;
                        ::QueryServiceConfigW(handle, nullptr, 0, &config_size);
                        if (config_size != 0) {
                            vector<BYTE> config(config_size);
                            if (::QueryServiceConfigW(handle,
                                                      reinterpret_cast<::LPQUERY_SERVICE_CONFIGW>(config.data()),
                                                      config_size, &config_size) == TRUE) {
                                const auto* value = reinterpret_cast<::LPQUERY_SERVICE_CONFIGW>(config.data());
                                item.load_ = service_start_mode(value->dwStartType);
                                item.extra_.set("start_mode", json(item.load_));
                                item.extra_.set("binary_path", json(wcharacter::to_string(value->lpBinaryPathName)));
                            }
                        }
                        DWORD description_size = 0;
                        ::QueryServiceConfig2W(handle, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &description_size);
                        if (description_size != 0) {
                            vector<BYTE> description(description_size);
                            if (::QueryServiceConfig2W(handle, SERVICE_CONFIG_DESCRIPTION, description.data(),
                                                       description_size, &description_size) == TRUE) {
                                const auto* value = reinterpret_cast<::LPSERVICE_DESCRIPTIONW>(description.data());
                                if (value->lpDescription != nullptr) {
                                    item.description_ = wcharacter::to_string(value->lpDescription);
                                }
                            }
                        }
                        ::CloseServiceHandle(handle);
                    }
                    if (item.load_.empty()) {
                        item.load_ = "unknown";
                    }
                    if (item.description_.empty()) {
                        item.description_ = "Windows service";
                    }
                    data.services_.push_back(move(item));
                }
                if (returned == 0 || resume == 0) {
                    break;
                }
            }
            ::CloseServiceHandle(manager);
            data.service_managers_.push_back("Windows SCM");
        }
#endif

        void discover_systemd(inventory& data) {
#if NEFLUX_PLATFORM_WINDOWS
            discover_windows_services(data);
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
        const auto result = ::neforce::process::run_capture(program, args, timeout_ms);
        string output = result.output;
        if (include_stderr && !result.error.empty()) {
            output.append("\n[stderr]\n");
            output.append(result.error);
        }
        if (result.exit_code != 0 && output.empty()) {
            throw process_exception(result.error.empty() ? "query failed" : result.error.data());
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

    namespace {

        string vm_join_state(const vector<string>& parts, size_t start) {
            string state;
            for (size_t index = start; index < parts.size(); ++index) {
                if (!state.empty()) {
                    state.push_back(' ');
                }
                state.append(parts[index]);
            }
            return state;
        }

        optional<uint64_t> vm_kib_value(const string& text) {
            const auto parts = split_whitespace(text.view());
            if (parts.empty()) {
                return none;
            }
            const auto value = parse_u64(parts[0].view());
            return value.has_value() ? optional<uint64_t>(*value * 1024ULL) : none;
        }

        string vm_process_name(const vector<string>& arguments) {
            string name;
            for (size_t index = 1; index + 1 < arguments.size(); ++index) {
                if (arguments[index] != "-name") {
                    continue;
                }
                name = arguments[index + 1];
                if (starts_with(name.view(), "guest=")) {
                    name = name.substr(6);
                }
                const auto comma = name.find(',');
                if (comma != string::npos) {
                    name = name.substr(0, comma);
                }
                break;
            }
            return name;
        }

        optional<uint64_t> vm_process_memory(const vector<string>& arguments) {
            for (size_t index = 1; index + 1 < arguments.size(); ++index) {
                if (arguments[index] != "-m" && arguments[index] != "--memory") {
                    continue;
                }
                string value = arguments[index + 1];
                value.trim();
                if (value.empty()) {
                    return none;
                }
                uint64_t multiplier = 1024ULL * 1024ULL;
                if (const char suffix = value.back(); suffix == 'G' || suffix == 'g') {
                    multiplier = 1024ULL * 1024ULL * 1024ULL;
                    value = value.substr(0, value.size() - 1);
                } else if (suffix == 'M' || suffix == 'm') {
                    value = value.substr(0, value.size() - 1);
                }
                const auto number = parse_u64(value.view());
                return number.has_value() ? optional<uint64_t>(*number * multiplier) : none;
            }
            return none;
        }

        optional<uint32_t> vm_process_vcpus(const vector<string>& arguments) {
            for (size_t index = 1; index + 1 < arguments.size(); ++index) {
                if (arguments[index] != "-smp" && arguments[index] != "--smp") {
                    continue;
                }
                string value = arguments[index + 1];
                const auto comma = value.find(',');
                if (comma != string::npos) {
                    value = value.substr(0, comma);
                }
                const auto number = parse_u64(value.view());
                return number.has_value() ? optional<uint32_t>(static_cast<uint32_t>(*number)) : none;
            }
            return none;
        }

        void discover_virtual_machines(inventory& data) {
#if NEFLUX_PLATFORM_LINUX
            set<string> libvirt_names;
            const bool has_virsh = path("/usr/bin/virsh").exists() || path("/bin/virsh").exists() ||
                                   path("/usr/local/bin/virsh").exists();
            if (has_virsh) {
                try {
                    const string listing = run_command("virsh", {"-r", "list", "--all"}, 3000);
                    data.vm_managers_.push_back("libvirt");
                    for (const auto& line: split_lines(listing)) {
                        const auto parts = split_whitespace(line.view());
                        if (parts.size() < 3 || parts[0] == "Id" ||
                            parts[0].find_first_not_of('-') == string::npos) {
                            continue;
                        }
                        virtual_machine vm;
                        vm.backend_ = "libvirt";
                        vm.id_ = parts[0] == "-" ? string() : parts[0];
                        vm.name_ = parts[1];
                        vm.state_ = vm_join_state(parts, 2);
                        libvirt_names.insert(vm.name_);
                        data.virtual_machines_.push_back(move(vm));
                    }
                    for (auto& vm: data.virtual_machines_) {
                        try {
                            const string details = run_command("virsh", {"-r", "dominfo", vm.name_}, 2500);
                            string state;
                            for (const auto& line: split_lines(details)) {
                                const auto colon = line.find(':');
                                if (colon == string::npos) {
                                    continue;
                                }
                                const string key = trim(line.view().substr(0, colon));
                                const string value = trim(line.view().substr(colon + 1));
                                if (key == "UUID") {
                                    vm.uuid_ = value;
                                } else if (key == "State") {
                                    state = value;
                                } else if (key == "CPU(s)") {
                                    const auto count = parse_u64(value.view());
                                    if (count.has_value()) {
                                        vm.vcpus_ = static_cast<uint32_t>(*count);
                                    }
                                } else if (key == "Max memory") {
                                    vm.max_memory_ = vm_kib_value(value);
                                } else if (key == "Used memory") {
                                    vm.memory_ = vm_kib_value(value);
                                } else if (key == "Autostart") {
                                    vm.autostart_ = value;
                                }
                            }
                            if (!state.empty()) {
                                vm.state_ = state;
                            }
                            vm.description_ = "libvirt domain";
                        } catch (const exception& error) {
                            data.notes_.push_back(format("libvirt {}: {}", vm.name_, trim(error.what())));
                        }
                    }
                } catch (const exception& error) {
                    data.notes_.push_back(format("libvirt: {}", trim(error.what())));
                }
            }

            set<string> seen_names = libvirt_names;
            for (const auto& entry: path("/proc").children()) {
                const string directory = entry.str();
                const auto slash = directory.rfind('/');
                const string pid_text = slash == string::npos ? directory : directory.substr(slash + 1);
                const auto pid = parse_u64(pid_text.view());
                if (!pid.has_value()) {
                    continue;
                }
                string command = read_text_file(directory + "/cmdline").value_or(string());
                if (command.empty()) {
                    continue;
                }
                vector<string> arguments;
                size_t begin = 0;
                while (begin <= command.size()) {
                    const auto end = command.find('\0', begin);
                    arguments.push_back(command.substr(begin, end == string::npos ? string::npos : end - begin));
                    if (end == string::npos) {
                        break;
                    }
                    begin = end + 1;
                }
                if (arguments.empty()) {
                    continue;
                }
                const auto argv0_slash = arguments[0].rfind('/');
                const string executable =
                        argv0_slash == string::npos ? arguments[0] : arguments[0].substr(argv0_slash + 1);
                if (!executable.starts_with("qemu-system") && !executable.starts_with("qemu-kvm")) {
                    continue;
                }
                string name = vm_process_name(arguments);
                if (name.empty()) {
                    name = format("{} ({})", executable, pid_text);
                }
                if (seen_names.count(name) != 0) {
                    continue;
                }
                virtual_machine vm;
                vm.name_ = name;
                vm.id_ = pid_text;
                vm.backend_ = "QEMU";
                vm.state_ = "running";
                vm.pid_ = static_cast<uint32_t>(*pid);
                vm.max_memory_ = vm_process_memory(arguments);
                vm.memory_ = vm.max_memory_;
                vm.vcpus_ = vm_process_vcpus(arguments);
                vm.description_ = executable;
                string command_line;
                for (const auto& argument: arguments) {
                    if (!command_line.empty()) {
                        command_line.push_back(' ');
                    }
                    command_line.append(argument);
                }
                vm.extra_ = json::object();
                vm.extra_.set("command", json(command_line));
                data.virtual_machines_.push_back(move(vm));
                seen_names.insert(name);
            }
            bool qemu_process = false;
            for (const auto& vm: data.virtual_machines_) {
                qemu_process = qemu_process || vm.backend_ == "QEMU";
            }
            if (qemu_process) {
                bool has_qemu_manager = false;
                for (const auto& manager: data.vm_managers_) {
                    has_qemu_manager = has_qemu_manager || manager == "QEMU";
                }
                if (!has_qemu_manager) {
                    data.vm_managers_.push_back("QEMU");
                }
            }
            neforce::tim_sort(data.virtual_machines_.begin(), data.virtual_machines_.end(),
                                 [](const virtual_machine& a, const virtual_machine& b) { return a.name_ < b.name_; });
#endif
        }

    } // namespace

    inventory discover_selected(const config& cfg, bool containers, bool services, bool extras) {
        inventory data;
        data.updated_at_ = unix_time();
        if (containers) {
            for (const char* runtime: {"docker", "podman"}) {
                discover_runtime(data, runtime);
            }
            discover_virtual_machines(data);
        }
        if (services) {
            discover_systemd(data);
        }
        neforce::tim_sort(data.containers_.begin(), data.containers_.end(),
                             [](const container& a, const container& b) { return a.name_ < b.name_; });
        neforce::tim_sort(data.services_.begin(), data.services_.end(), [](const service& a, const service& b) {
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
        for (const auto& item: vector<tuple<ip_family, socket_base::protocol, const char*>>{
                     {ip_family::inet4, socket_base::protocol::tcp, "TCP"},
                     {ip_family::inet6, socket_base::protocol::tcp, "TCP6"},
                     {ip_family::inet4, socket_base::protocol::udp, "UDP"},
                     {ip_family::inet6, socket_base::protocol::udp, "UDP6"}}) {
            const auto entries = socket_base::enumerate_sockets(get<0>(item), get<1>(item));
            if (!entries.has_value()) {
                continue;
            }
            for (const auto& entry: *entries) {
                if (entry.status != socket_base::state::listen && entry.status != socket_base::state::unconnected) {
                    continue;
                }
                const auto delimiter = entry.local.rfind(':');
                if (delimiter == string::npos) {
                    continue;
                }
                listener value;
                value.protocol_ = get<2>(item);
                string address = entry.local.substr(0, delimiter);
                if (address.size() >= 2 && address.front() == '[' && address.back() == ']') {
                    address = address.substr(1, address.size() - 2);
                }
                value.address_ = move(address);
                const auto port = parse_u64(string_view(entry.local.view()).substr(delimiter + 1));
                if (!port.has_value()) {
                    continue;
                }
                value.port_ = static_cast<uint16_t>(*port);
                value.uid_ = entry.uid;
                out.push_back(move(value));
            }
        }
        neforce::tim_sort(out.begin(), out.end(), [](const listener& a, const listener& b) {
            if (a.port_ != b.port_) {
                return a.port_ < b.port_;
            }
            return a.protocol_ < b.protocol_;
        });
        return out;
    }

} // namespace neforce::neflux
