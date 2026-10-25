#include <NeFlux/common.hpp>
#include <NeFlux/connections.hpp>

#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>

#include <NeForce/core/system/process.hpp>

namespace neforce::neflux {

    namespace {

        string query(const string& program, const vector<string>& args) {
            try {
                return ::neforce::process::capture(program, args, 3000);
            } catch (...) {
                return {};
            }
        }

        string state_text(const string& value) {
            if (value == "ESTABLISHED" || value == "LISTEN" || value == "BOUND" || value == "TIME_WAIT" ||
                value == "CLOSE_WAIT" || value == "SYN_SENT") {
                return value;
            }
            return value.empty() ? "UNKNOWN" : value;
        }

    } // namespace

    string decode_address(const string& address) { return address; }

    vector<connection> parse_proc_connections(const string& protocol, const string& text) {
        static_cast<void>(protocol);
        static_cast<void>(text);
        return {};
    }

    void apply_tcp_info(vector<connection>& rows, const string& text) {
        static_cast<void>(rows);
        static_cast<void>(text);
    }

    string state_help(const string& state) {
        if (state == "ESTABLISHED") {
            return "Connected. Idle connections can legitimately have zero traffic.";
        }
        if (state == "LISTEN" || state == "BOUND") {
            return "Waiting for incoming traffic. Wildcard addresses accept traffic on all local interfaces.";
        }
        if (state == "SYN_SENT") {
            return "Waiting for the peer to accept TCP. Check the destination, route and firewall.";
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
        map<string, pair<optional<uint64_t>, optional<uint64_t>>> previous;
        double previous_at = 0.0;
        string status;
    };

    connection_collector::connection_collector() :
    impl_(make_unique<impl>()) {}

    connection_collector::~connection_collector() = default;

    connection_sample connection_collector::collect(bool sockets) {
        connection_sample sample;
        sample.sampled_at_ = unix_time();
        if (!sockets) {
            sample.status_ = "Loading connection details…";
            return sample;
        }
#if NEFLUX_PLATFORM_WINDOWS
        const string text = query("netstat", {"-ano"});
        for (const auto& line: split_lines(text)) {
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 4) {
                continue;
            }
            if (parts[0] != "TCP" && parts[0] != "UDP") {
                continue;
            }
            connection row;
            row.protocol_ = lower_ascii(parts[0].view());
            row.local_ = parts[1];
            row.remote_ = parts[2];
            row.state_ = parts[0] == "TCP" ? state_text(parts[3]) : "BOUND";
            const size_t pid_index = parts[0] == "TCP" ? 4 : 3;
            if (parts.size() > pid_index) {
                const auto pid = parse_u64(parts[pid_index].view());
                if (pid.has_value()) {
                    row.pids_.push_back(static_cast<uint32_t>(*pid));
                }
            }
            row.id_ = format("{}:{}:{}:{}", row.protocol_, row.local_, row.remote_,
                             row.pids_.empty() ? string("0") : format("{}", row.pids_[0]));
            sample.connections_.push_back(move(row));
        }
#elif NEFLUX_PLATFORM_MACOS
        const string text = query("lsof", {"-nP", "-i"});
        for (const auto& line: split_lines(text)) {
            const auto parts = split_whitespace(line.view());
            if (parts.size() < 9 || parts[0] == "COMMAND") {
                continue;
            }
            const auto colon = parts[8].find(':');
            if (colon == string::npos) {
                continue;
            }
            connection row;
            row.protocol_ = lower_ascii(parts[7].view());
            row.local_ = parts[8];
            row.remote_ = parts.size() > 9 ? parts[9] : "*:*";
            row.state_ = parts.size() > 10 ? parts[9] : "BOUND";
            if (row.remote_.find("->") != string::npos) {
                const auto arrow = row.remote_.find("->");
                row.local_ = row.remote_.substr(0, arrow);
                row.remote_ = row.remote_.substr(arrow + 2);
            }
            const auto pid = parse_u64(parts[1].view());
            if (pid.has_value()) {
                row.pids_.push_back(static_cast<uint32_t>(*pid));
                row.process_names_.push_back(parts[0]);
            }
            row.id_ = format("{}:{}:{}:{}", row.protocol_, row.local_, row.remote_, parts[1]);
            sample.connections_.push_back(move(row));
        }
#else
        sample.status_ = "Connection monitoring unavailable on this platform";
#endif
        sample.status_ = sample.connections_.empty() ? "No readable connections" : "Connection table";
        const double now = static_cast<double>(monotonic_ms()) / 1000.0;
        const double seconds = impl_->previous_at > 0.0 ? now - impl_->previous_at : 0.0;
        map<string, pair<optional<uint64_t>, optional<uint64_t>>> next;
        for (auto& row: sample.connections_) {
            const auto found = impl_->previous.find(row.id_);
            if (found != impl_->previous.end() && seconds > 0.0) {
                row.rx_per_sec_ = none;
                row.tx_per_sec_ = none;
            }
            next[row.id_] = {row.received_, row.sent_};
        }
        impl_->previous = next;
        impl_->previous_at = now;
        impl_->status = sample.status_;
        return sample;
    }

    const string& connection_collector::status() const noexcept { return impl_->status; }

} // namespace neforce::neflux
