#include "../platform_backend.hpp"
#include <NeFlux/connections.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>

namespace neforce::neflux::platform {

    vector<connection> parse_proc_connections(const string& protocol, const string& text) {
        static_cast<void>(protocol);
        static_cast<void>(text);
        return {};
    }

    void apply_tcp_info(vector<connection>& rows, const string& text) {
        static_cast<void>(rows);
        static_cast<void>(text);
    }

        vector<connection> collect_posix_netstat() {
            vector<connection> rows;
            for (const auto& spec: vector<pair<const char*, const char*>>{{"tcp", "tcp"}, {"udp", "udp"}}) {
                string output;
                try {
                    output = run_command("netstat", {"-an", "-p", spec.first}, 2000);
                } catch (const exception&) {
                    continue;
                }
                for (const auto& line: split_lines(output)) {
                    const auto parts = split_whitespace(line.view());
                    if (parts.size() < 4) {
                        continue;
                    }
                    const string_view proto = parts[0].view();
                    if (!proto.starts_with("tcp") && !proto.starts_with("udp")) {
                        continue;
                    }
                    connection row;
                    row.protocol_ = string(proto);
                    row.local_ = string(parts[3]);
                    row.remote_ = string(parts[4]);
                    if (parts.size() >= 6) {
                        row.state_ = string(parts.back());
                    } else {
                        row.state_ = "UNKNOWN";
                    }
                    row.receive_queue_ = parse_u64(parts[1].view());
                    row.send_queue_ = parse_u64(parts[2].view());
                    row.id_ = format("{}:{}:{}:{}", row.protocol_, row.local_, row.remote_,
                                     static_cast<uint64_t>(rows.size()));
                    rows.push_back(move(row));
                }
            }
            return rows;
        }

    void collect_connections(connection_sample& sample, connection_owner_state& state) {
        static_cast<void>(state);
        sample.sampled_at_ = unix_time();
        sample.owners_sampled_at_ = sample.sampled_at_;
        sample.owner_scan_complete_ = true;
        sample.connections_ = collect_posix_netstat();
        sample.status_ = "Current network namespace";
    }

} // namespace neforce::neflux::platform
