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
                row.id_ = format("{}:{}:{}:{}:{:x}", row.protocol_, row.inode_, row.local_, row.remote_, entry.cookie);
                if (!row.local_.empty() && !row.remote_.empty()) {
                    rows.push_back(move(row));
                }
            }
        }
        return rows;
    }

    void collect_connections(connection_sample& sample, connection_owner_state& state) {
        static_cast<void>(state);
        sample.sampled_at_ = unix_time();
        sample.owners_sampled_at_ = sample.sampled_at_;
        sample.owner_scan_complete_ = true;
        try {
            sample.connections_ = collect_netlink();
        } catch (const exception& error) {
            sample.notes_.push_back(format("socket enumeration unavailable: {}", error.what()));
        }
        sample.status_ = sample.notes_.empty() ? "Current network namespace" : "Partial socket sample";
    }

} // namespace neforce::neflux::platform
