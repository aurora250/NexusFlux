#include <NeForce/core/container/flat_unordered_map.hpp>
#include <NeFlux/common.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
#include "platform/platform_backend.hpp"
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>
namespace neforce::neflux {

    namespace {

    } // namespace

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
        if (out.empty()) {
            out = "::";
        }
        return out;
    }

    string decode_address(const string& address) {
        const auto colon = address.rfind(':');
        if (colon == string::npos) {
            return address;
        }
        const string ip = address.substr(0, colon);
        const string port_text = address.substr(colon + 1);
        uint32_t port = 0;
        for (const char c: port_text) {
            const int digit =
                    c >= '0' && c <= '9'
                            ? c - '0'
                            : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
            if (digit < 0) {
                return address;
            }
            port = port * 16 + static_cast<uint32_t>(digit);
        }
        if (ip.size() == 8) {
            uint32_t value = 0;
            for (const char c: ip) {
                const int digit =
                        c >= '0' && c <= '9'
                                ? c - '0'
                                : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                if (digit < 0) {
                    return address;
                }
                value = value * 16 + static_cast<uint32_t>(digit);
            }
            return format("{}.{}.{}.{}:{}", value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF,
                          (value >> 24) & 0xFF, port);
        }
        if (ip.size() == 32) {
            uint8_t bytes[16] = {};
            for (size_t chunk = 0; chunk < 4; ++chunk) {
                uint32_t value = 0;
                for (size_t offset = 0; offset < 8; ++offset) {
                    const char c = ip[chunk * 8 + offset];
                    const int digit = c >= '0' && c <= '9'
                                              ? c - '0'
                                              : (c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                      : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                    if (digit < 0) {
                        return address;
                    }
                    value = value * 16 + static_cast<uint32_t>(digit);
                }
                bytes[chunk * 4] = value & 0xFF;
                bytes[chunk * 4 + 1] = (value >> 8) & 0xFF;
                bytes[chunk * 4 + 2] = (value >> 16) & 0xFF;
                bytes[chunk * 4 + 3] = (value >> 24) & 0xFF;
            }
            return format("[{}]:{}", format_ipv6(bytes), port);
        }
        return address;
    }

    vector<connection> parse_proc_connections(const string& protocol, const string& text) {
        return platform::parse_proc_connections(protocol, text);
    }

    void apply_tcp_info(vector<connection>& rows, const string& text) {
        platform::apply_tcp_info(rows, text);
    }

    string state_help(const string& state) {
        if (state == "ESTABLISHED") {
            return "Connected. Idle connections can legitimately have zero traffic.";
        }
        if (state == "LISTEN" || state == "BOUND") {
            return "Waiting for incoming traffic. Wildcard addresses accept traffic on all local interfaces.";
        }
        if (state == "SYN_SENT") {
            return "Waiting for the peer to accept TCP. Check the destination, route and firewall; this alone does not "
                   "identify the cause.";
        }
        if (state == "CLOSE_WAIT") {
            return "The peer closed. If many accumulate, inspect whether this application closes its sockets.";
        }
        if (state == "TIME_WAIT") {
            return "Recently closed; the OS temporarily retains it. This is normally expected.";
        }
        return "Connection is opening or closing. Compare successive samples before diagnosing a fault.";
    }

    struct connection_collector::impl {
        flat_unordered_map<string, pair<optional<uint64_t>, optional<uint64_t>>> previous;
        double previous_at = 0.0;
        string status;
        platform::connection_owner_state owners;
    };

    connection_collector::connection_collector() :
    impl_(make_unique<impl>()) {}

    connection_collector::~connection_collector() = default;

    connection_sample connection_collector::collect(bool sockets) {
        connection_sample sample;
        sample.sampled_at_ = unix_time();
        if (!sockets) {
            sample.owners_sampled_at_ = sample.sampled_at_;
            sample.owner_scan_complete_ = true;
            sample.status_ = "Loading connection details…";
            return sample;
        }
        platform::collect_connections(sample, impl_->owners);
        if (sample.connections_.size() > 20000) {
            sample.connections_.resize(20000);
            sample.truncated_ = true;
        }
        const double now = static_cast<double>(monotonic_ms()) / 1000.0;
        const double seconds = impl_->previous_at > 0.0 ? now - impl_->previous_at : 0.0;
        flat_unordered_map<string, pair<optional<uint64_t>, optional<uint64_t>>> next;
        for (auto& row: sample.connections_) {
            const auto found = impl_->previous.find(row.id_);
            if (found != impl_->previous.end() && seconds > 0.0) {
                if (found->second.first.has_value() && row.received_.has_value() &&
                    *row.received_ >= *found->second.first) {
                    row.rx_per_sec_ = static_cast<double>(*row.received_ - *found->second.first) / seconds;
                }
                if (found->second.second.has_value() && row.sent_.has_value() && *row.sent_ >= *found->second.second) {
                    row.tx_per_sec_ = static_cast<double>(*row.sent_ - *found->second.second) / seconds;
                }
            }
            next[row.id_] = {row.received_, row.sent_};
        }
        impl_->previous = move(next);
        impl_->previous_at = now;
        impl_->status = sample.status_;
        return sample;
    }

    const string& connection_collector::status() const noexcept { return impl_->status; }

} // namespace neforce::neflux
