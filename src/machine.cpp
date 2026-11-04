#include <NeFlux/machine.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/string/string_view.hpp>
#include <NeForce/core/system/process.hpp>

namespace neforce::neflux {

    namespace {

        optional<uint16_t> parse_port(string_view text) {
            const auto value = parse_u64(text);
            if (!value.has_value() || *value == 0 || *value > 65535) {
                return none;
            }
            return static_cast<uint16_t>(*value);
        }

        optional<pair<string, uint16_t>> parse_hostfwd(const string& command) {
            size_t offset = 0;
            while ((offset = command.find("hostfwd=", offset)) != string::npos) {
                offset += 8;
                const size_t end = command.find_first_of(", ", offset);
                const string_view entry(command.data() + offset, (end == string::npos ? command.size() : end) - offset);
                const size_t arrow = entry.rfind("-:");
                if (arrow == string_view::npos) {
                    continue;
                }
                const string_view left = entry.substr(0, arrow);
                const string_view right = entry.substr(arrow + 2);
                if (parse_port(right).value_or(0) != 22) {
                    continue;
                }
                const size_t scheme = left.find(':');
                if (scheme == string_view::npos) {
                    continue;
                }
                string_view authority = left.substr(scheme + 1);
                if (authority.starts_with("[")) {
                    const size_t close = authority.find(']');
                    if (close == string_view::npos || close + 1 >= authority.size() || authority[close + 1] != ':') {
                        continue;
                    }
                    const auto port = parse_port(authority.substr(close + 2));
                    if (port.has_value()) {
                        return pair<string, uint16_t>{string(authority.substr(0, close + 1)), *port};
                    }
                    continue;
                }
                const size_t colon = authority.rfind(':');
                if (colon == string_view::npos) {
                    continue;
                }
                const auto port = parse_port(authority.substr(colon + 1));
                if (port.has_value()) {
                    return pair<string, uint16_t>{string(authority.substr(0, colon)), *port};
                }
            }
            return none;
        }

    } // namespace

    optional<machine_target> parse_machine_selector(const string_view selector, string& error) {
        machine_target target;
        target.selector_ = string(selector);
        if (selector.empty() || selector == "local") {
            target.kind_ = machine_kind::local;
            target.name_ = "local";
            return target;
        }
        if (starts_with(selector, "qemu:")) {
            target.kind_ = machine_kind::qemu;
            target.name_ = string(selector.substr(5));
            if (target.name_.empty()) {
                error = "qemu machine name is empty";
                return none;
            }
            return target;
        }
        if (starts_with(selector, "ssh:")) {
            target.kind_ = machine_kind::ssh;
            const string_view rest = selector.substr(4);
            const size_t at = rest.rfind('@');
            string_view host_port = rest;
            if (at != string_view::npos) {
                target.user_ = string(rest.substr(0, at));
                host_port = rest.substr(at + 1);
            }
            if (host_port.starts_with("[")) {
                const size_t close = host_port.find(']');
                if (close == string_view::npos) {
                    error = "invalid ssh host";
                    return none;
                }
                target.host_ = string(host_port.substr(1, close - 1));
                if (close + 1 < host_port.size() && host_port[close + 1] == ':') {
                    const auto port = parse_port(host_port.substr(close + 2));
                    if (!port.has_value()) {
                        error = "invalid ssh port";
                        return none;
                    }
                    target.port_ = *port;
                }
            } else {
                const size_t colon = host_port.rfind(':');
                if (colon == string_view::npos) {
                    target.host_ = string(host_port);
                } else {
                    target.host_ = string(host_port.substr(0, colon));
                    const auto port = parse_port(host_port.substr(colon + 1));
                    if (!port.has_value()) {
                        error = "invalid ssh port";
                        return none;
                    }
                    target.port_ = *port;
                }
            }
            target.name_ = target.host_;
            return target;
        }
        error = format("unknown machine selector: {}", selector);
        return none;
    }

    optional<pair<string, uint16_t>> machine_ssh_endpoint(const virtual_machine& vm, string& error) {
        const json* command = vm.extra_.get("command");
        if (command == nullptr || !command->is_string()) {
            error = format("VM {} has no QEMU command line", vm.name_);
            return none;
        }
        const auto endpoint = parse_hostfwd(command->as_string());
        if (!endpoint.has_value()) {
            error = format("VM {} has no SSH hostfwd", vm.name_);
        }
        return endpoint;
    }

    bool resolve_machine_target(machine_target& target, const config& cfg, const bool start_vm, string& error) {
        if (target.kind_ == machine_kind::local || target.kind_ == machine_kind::ssh) {
            if (target.kind_ == machine_kind::ssh && target.host_.empty()) {
                error = "ssh machine has no host";
                return false;
            }
            return true;
        }
        inventory data;
        try {
            data = discover_machine_inventory(cfg);
        } catch (const exception& failure) {
            error = format("cannot inspect QEMU processes: {}", failure.what());
            return false;
        }
        const auto find_vm = [&](const inventory& value) -> const virtual_machine* {
            for (const auto& item: value.virtual_machines_) {
                if (item.name_ == target.name_ || item.id_ == target.name_) {
                    return &item;
                }
            }
            return nullptr;
        };
        const virtual_machine* found = find_vm(data);
        if (found == nullptr && start_vm && !target.start_argv_.empty()) {
            try {
                vector<string> args(target.start_argv_.begin() + 1, target.start_argv_.end());
                ::neforce::process::spawn(target.start_argv_[0], args);
            } catch (const exception& failure) {
                error = format("cannot start QEMU: {}", failure.what());
                return false;
            }
            const int64_t deadline = monotonic_ms() + 15000;
            while (found == nullptr && monotonic_ms() < deadline) {
                sleep_ms(200);
                try {
                    data = discover_machine_inventory(cfg);
                } catch (const exception&) {
                    continue;
                }
                found = find_vm(data);
            }
        }
        if (found == nullptr) {
            error = format("QEMU machine not found: {}", target.name_);
            return false;
        }
        if (start_vm && found->state_ != "running") {
            if (found->backend_ == "libvirt") {
                try {
                    run_command("virsh", {"start", found->name_}, 10000);
                } catch (const exception& failure) {
                    error = format("cannot start VM {}: {}", found->name_, failure.what());
                    return false;
                }
                try {
                    data = discover_machine_inventory(cfg);
                } catch (const exception& failure) {
                    error = format("cannot refresh VM {}: {}", found->name_, failure.what());
                    return false;
                }
                found = nullptr;
                for (const auto& item: data.virtual_machines_) {
                    if (item.name_ == target.name_ || item.id_ == target.name_) {
                        found = &item;
                        break;
                    }
                }
                if (found == nullptr) {
                    error = format("VM disappeared after start: {}", target.name_);
                    return false;
                }
            } else {
                error = format("unmanaged QEMU process is not running: {}", found->name_);
                return false;
            }
        }
        const auto endpoint = machine_ssh_endpoint(*found, error);
        if (!endpoint.has_value()) {
            error = format("{}; add -netdev user,hostfwd=tcp:127.0.0.1:<port>-:22 or use --machine ssh:...", error);
            return false;
        }
        target.host_ = endpoint->first;
        target.port_ = endpoint->second;
        target.name_ = found->name_;
        if (target.user_.empty()) {
            error = format("VM {} requires --machine-user", found->name_);
            return false;
        }
        return true;
    }

} // namespace neforce::neflux
