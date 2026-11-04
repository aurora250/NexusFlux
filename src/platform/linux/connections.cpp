#include "../platform_backend.hpp"
#include <NeFlux/connections.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>

namespace neforce::neflux::platform {

    namespace {

        string state_name(uint8_t state, const string& protocol) {
            return string(socket_base::state_name(state, protocol == "UDP"));
        }

        vector<connection> collect_netlink() {
            vector<connection> rows;
            const int64_t deadline = monotonic_ms() + 1000;
            for (const auto& item: vector<tuple<ip_family, socket_base::protocol, string_view>>{
                         {ip_family::inet4, socket_base::protocol::tcp, "tcp"},
                         {ip_family::inet6, socket_base::protocol::tcp, "tcp6"},
                         {ip_family::inet4, socket_base::protocol::udp, "udp"},
                         {ip_family::inet6, socket_base::protocol::udp, "udp6"}}) {
                if (monotonic_ms() > deadline) {
                    throw value_exception("socket dump deadline");
                }
                const auto entries = socket_base::enumerate_sockets(get<0>(item), get<1>(item));
                if (!entries.has_value()) {
                    throw value_exception(format("inet_diag unavailable for {}", get<2>(item)).data());
                }
                for (const auto& entry: *entries) {
                    connection row;
                    row.protocol_ = get<2>(item);
                    row.local_ = entry.local;
                    row.remote_ = entry.remote;
                    row.state_ = string(socket_base::state_name(entry.status));
                    row.inode_ = entry.inode;
                    row.receive_queue_ = entry.receive_queue;
                    row.send_queue_ = entry.send_queue;
                    if (entry.pid != 0) {
                        row.pids_.push_back(entry.pid);
                    }
                    row.id_ = format("{}:{}:{}:{}:{:x}", row.protocol_, row.inode_, row.local_, row.remote_,
                                     entry.cookie);
                    if (!row.local_.empty() && !row.remote_.empty()) {
                        rows.push_back(move(row));
                    }
                }
            }
            return rows;
        }

    } // namespace

    vector<connection> parse_proc_connections(const string& protocol, const string& text) {
        vector<connection> rows;
        bool first = true;
        for (const auto& line: split_lines(text)) {
            if (first) {
                first = false;
                continue;
            }
            if (rows.size() >= 20000) {
                break;
            }
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 10) {
                continue;
            }
            const auto inode = parse_u64(parts[9].view());
            if (!inode.has_value()) {
                continue;
            }
            const auto queues = split(parts[4].view(), ':', true);
            if (queues.size() != 2) {
                continue;
            }
            connection row;
            row.id_ = format("{}:{}:{}:{}", protocol, *inode, decode_address(parts[1]), decode_address(parts[2]));
            row.protocol_ = protocol;
            row.local_ = decode_address(parts[1]);
            row.remote_ = decode_address(parts[2]);
            uint32_t state_code = 0;
            bool state_valid = true;
            for (const char c: parts[3]) {
                const int digit =
                        c >= '0' && c <= '9'
                                ? c - '0'
                                : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                if (digit < 0) {
                    state_valid = false;
                    break;
                }
                state_code = state_code * 16 + static_cast<uint32_t>(digit);
            }
            row.state_ = state_valid ? state_name(static_cast<uint8_t>(state_code), protocol) : "UNKNOWN";
            row.inode_ = *inode;
            const auto parse_hex = [](string_view value) -> optional<uint64_t> {
                if (value.empty()) {
                    return 0;
                }
                uint64_t result = 0;
                for (const char c: value) {
                    const int digit = c >= '0' && c <= '9'
                                              ? c - '0'
                                              : (c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                      : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                    if (digit < 0) {
                        return none;
                    }
                    result = result * 16 + static_cast<uint64_t>(digit);
                }
                return result;
            };
            const auto send = parse_hex(queues[0].view());
            const auto receive = parse_hex(queues[1].view());
            if (send.has_value()) {
                row.send_queue_ = *send;
            }
            if (receive.has_value()) {
                row.receive_queue_ = *receive;
            }
            rows.push_back(move(row));
        }
        return rows;
    }

    void apply_tcp_info(vector<connection>& rows, const string& text) {
        flat_unordered_map<uint64_t, flat_unordered_map<string, string>> data;
        for (const auto& line: split_lines(text)) {
            flat_unordered_map<string, string> fields;
            for (const auto& token: split_whitespace(line.view())) {
                const auto colon = token.find(':');
                if (colon != string::npos) {
                    fields[token.substr(0, colon)] = token.substr(colon + 1);
                }
            }
            const auto found = fields.find("ino");
            if (found != fields.end()) {
                const auto inode = parse_u64(found->second.view());
                if (inode.has_value()) {
                    data[*inode] = _NEFORCE move(fields);
                }
            }
        }
        for (auto& row: rows) {
            if (!starts_with(row.protocol_.view(), "tcp")) {
                continue;
            }
            const auto found = data.find(row.inode_);
            if (found == data.end()) {
                continue;
            }
            const auto& fields = found->second;
            if (const auto cookie = fields.find("sk"); cookie != fields.end()) {
                row.id_.append(":");
                row.id_.append(cookie->second);
            }
            if (const auto value = fields.find("bytes_received"); value != fields.end()) {
                row.received_ = parse_u64(value->second.view());
            }
            if (const auto value = fields.find("bytes_sent"); value != fields.end()) {
                row.sent_ = parse_u64(value->second.view());
            }
            if (const auto value = fields.find("rtt"); value != fields.end()) {
                const auto slash = value->second.find('/');
                row.rtt_ms_ = parse_f64(value->second.view().substr(0, slash));
            }
            if (const auto value = fields.find("retrans"); value != fields.end()) {
                const auto parts = split(value->second.view(), '/', true);
                if (parts.size() >= 2) {
                    row.retransmits_ = parse_u64(parts[1].view());
                }
            }
        }
    }

    void collect_connections(connection_sample& sample, connection_owner_state& state) {
        sample.sampled_at_ = unix_time();
        if (state.owners_at < 0 || monotonic_ms() - state.owners_at >= 5000) {
            state.owner_data = ::neforce::process::scan_socket_owners(700);
            state.owners_at = monotonic_ms();
            state.owners_sampled_at = sample.sampled_at_;
        }
        sample.owners_sampled_at_ = state.owners_sampled_at;
        sample.owner_scan_complete_ = state.owner_data.complete;
        sample.unreadable_processes_ = state.owner_data.unreadable_processes;
        bool native = true;
        try {
            sample.connections_ = collect_netlink();
        } catch (const exception& error) {
            native = false;
            sample.notes_.push_back(format("inet_diag unavailable; using procfs/ss: {}", error.what()));
        }
        if (!native) {
            for (const auto& item: vector<pair<const char*, const char*>>{
                         {"tcp", "tcp"}, {"tcp6", "tcp6"}, {"udp", "udp"}, {"udp6", "udp6"}}) {
                const auto text = read_text_file(format("/proc/net/{}", item.first));
                if (!text.has_value()) {
                    sample.notes_.push_back(format("{}: unavailable", item.first));
                    continue;
                }
                auto rows = parse_proc_connections(item.second, *text);
                for (auto& row: rows) {
                    sample.connections_.push_back(move(row));
                }
            }
            try {
                const string text = run_command("ss", {"-H", "-O", "-t", "-a", "-n", "-i", "-e"}, 1500);
                platform::apply_tcp_info(sample.connections_, text);
            } catch (const exception& error) {
                sample.notes_.push_back(format(
                        "TCP rates unavailable: {}. Install iproute2 (ss); UDP byte counters remain unavailable.",
                        error.what()));
            }
        }
        for (auto& row: sample.connections_) {
            const auto found = state.owner_data.owners.find(row.inode_);
            if (found != state.owner_data.owners.end()) {
                row.pids_.clear();
                row.pids_.reserve(found->second.size());
                for (const auto pid: found->second) {
                    row.pids_.push_back(static_cast<uint32_t>(pid));
                }
            }
        }
        sample.status_ = sample.notes_.empty() ? "Current network namespace" : "Partial socket sample";
    }

} // namespace neforce::neflux::platform
