#include "../platform_backend.hpp"
#include <NeForce/core/container/set.hpp>
#include <NeFlux/util.hpp>
#include <NeFlux/discovery.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/process.hpp>

namespace neforce::neflux::platform {

    string docker_host() { return "unix:///var/run/docker.sock"; }

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
            set<string> libvirt_names;
            const bool has_virsh = path("/usr/bin/virsh").exists() || path("/bin/virsh").exists() ||
                                   path("/usr/local/bin/virsh").exists();
            if (has_virsh) {
                try {
                    const string listing = run_command("virsh", {"-r", "list", "--all"}, 3000);
                    data.vm_managers_.push_back("libvirt");
                    for (const auto& line: split_lines(listing)) {
                        const auto parts = split_whitespace(line.view());
                        if (parts.size() < 3 || parts[0] == "Id" || parts[0].find_first_not_of('-') == string::npos) {
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
        }


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


    string service_json_field(const json& value, const char* name) { return field(value, {name}); }

    void discover_services(inventory& data) {
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
    }

} // namespace neforce::neflux::platform
