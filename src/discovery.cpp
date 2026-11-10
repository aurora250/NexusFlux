#include <NeFlux/common.hpp>
#include <NeFlux/advanced.hpp>
#include <NeFlux/discovery.hpp>
#include "platform/platform_backend.hpp"
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>
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
                result.push_back(platform::docker_host());
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

        string service_json_field(const json& value, const char* name) { return field(value, {name}); }


        void discover_systemd(inventory& data) { platform::discover_services(data); }

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

    namespace {} // namespace

    inventory discover_selected(const config& cfg, bool containers, bool services, bool extras) {
        inventory data;
        data.updated_at_ = unix_time();
        if (containers) {
            for (const char* runtime: {"docker", "podman"}) {
                discover_runtime(data, runtime);
            }
            platform::discover_virtual_machines(data);
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
            platform::discover_ownership(data);
        }
        json state = json::object();
        state.set("containers", json(containers));
        state.set("services", json(services));
        state.set("extras", json(extras));
        data.extra_.set("Discovery state", move(state));
        static_cast<void>(cfg);
        return data;
    }

    inventory discover_machine_inventory(const config& cfg) {
        inventory data;
        data.updated_at_ = unix_time();
        platform::discover_virtual_machines(data);
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

    namespace {
        string format_ipv6(const uint8_t (&bytes)[16]) {
            uint16_t groups[8] = {};
            for (size_t index = 0; index < 8; ++index) {
                groups[index] = static_cast<uint16_t>((static_cast<uint16_t>(bytes[index * 2]) << 8) |
                                                      static_cast<uint16_t>(bytes[index * 2 + 1]));
            }
            size_t best_start = 8;
            size_t best_length = 0;
            for (size_t index = 0; index < 8;) {
                if (groups[index] != 0) {
                    ++index;
                    continue;
                }
                size_t end = index;
                while (end < 8 && groups[end] == 0) {
                    ++end;
                }
                if (end - index > best_length) {
                    best_start = index;
                    best_length = end - index;
                }
                index = end;
            }
            string out;
            for (size_t index = 0; index < 8; ++index) {
                if (index == best_start && best_length > 1) {
                    out.append("::");
                    index += best_length - 1;
                    continue;
                }
                if (!out.empty() && !out.ends_with("::")) {
                    out.push_back(':');
                }
                out.append(format("{:x}", groups[index]));
            }
            return out.empty() ? string("::") : out;
        }
    } // namespace

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
                address = format_ipv6(bytes);
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
