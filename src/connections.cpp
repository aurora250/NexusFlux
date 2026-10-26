#include <NeForce/core/container/flat_unordered_map.hpp>
#include <NeFlux/common.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/socket_base.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <arpa/inet.h>
#    include <dirent.h>
#    include <unistd.h>
#elif NEFLUX_PLATFORM_WINDOWS
#    include <winsock2.h>
#    include <ws2tcpip.h>
#endif
namespace neforce::neflux {

    namespace {

        string state_name(uint8_t state, const string& protocol) {
            return string(socket_base::state_name(state, protocol == "UDP"));
        }

#if NEFLUX_PLATFORM_LINUX
        struct owners {
            flat_unordered_map<uint64_t, vector<uint32_t>> pids;
            bool complete = true;
            uint64_t unreadable = 0;
        };

        owners scan_owners() {
            owners result;
            ::DIR* proc = ::opendir("/proc");
            if (proc == nullptr) {
                result.complete = false;
                return result;
            }
            const int64_t deadline = monotonic_ms() + 700;
            while (::dirent* entry = ::readdir(proc)) {
                uint32_t pid = 0;
                bool numeric = true;
                for (const char* cursor = entry->d_name; *cursor != '\0'; ++cursor) {
                    if (*cursor < '0' || *cursor > '9') {
                        numeric = false;
                        break;
                    }
                    pid = pid * 10 + static_cast<uint32_t>(*cursor - '0');
                }
                if (!numeric || entry->d_name[0] == '\0' || pid == 0) {
                    continue;
                }
                const string fd_directory = format("/proc/{}/fd", pid);
                ::DIR* descriptors = ::opendir(fd_directory.data());
                if (descriptors == nullptr) {
                    ++result.unreadable;
                    continue;
                }
                while (::dirent* descriptor = ::readdir(descriptors)) {
                    if (monotonic_ms() > deadline) {
                        result.complete = false;
                        break;
                    }
                    if (descriptor->d_name[0] == '.') {
                        continue;
                    }
                    const string link_path = fd_directory + "/" + descriptor->d_name;
                    char target[512] = {};
                    const ::ssize_t length = ::readlink(link_path.data(), target, sizeof(target) - 1);
                    if (length <= 0) {
                        continue;
                    }
                    const string_view text(target, static_cast<size_t>(length));
                    if (!starts_with(text, "socket:[")) {
                        continue;
                    }
                    const auto close = text.find(']', 8);
                    if (close == string_view::npos) {
                        continue;
                    }
                    const auto inode = parse_u64(text.substr(8, close - 8));
                    if (!inode.has_value()) {
                        continue;
                    }
                    auto& list = result.pids[*inode];
                    bool present = false;
                    for (const uint32_t existing: list) {
                        if (existing == pid) {
                            present = true;
                            break;
                        }
                    }
                    if (!present) {
                        list.push_back(pid);
                    }
                }
                ::closedir(descriptors);
                if (!result.complete) {
                    break;
                }
            }
            ::closedir(proc);
            return result;
        }
#endif

        vector<connection> collect_netlink() {
            vector<connection> rows;
            const int64_t deadline = monotonic_ms() + 1000;
            for (const auto& item: vector<tuple<ip_family, socket_base::protocol, string_view>>{
                         {ip_family::INET4, socket_base::protocol::TCP, "tcp"},
                         {ip_family::INET6, socket_base::protocol::TCP, "tcp6"},
                         {ip_family::INET4, socket_base::protocol::UDP, "udp"},
                         {ip_family::INET6, socket_base::protocol::UDP, "udp6"}}) {
                if (monotonic_ms() > deadline) {
                    throw value_exception("socket dump deadline");
                }
                const auto entries = socket_base::enumerate_sockets(get<0>(item), get<1>(item));
                if (!entries.has_value()) {
#if NEFLUX_PLATFORM_LINUX
                    throw value_exception(format("inet_diag unavailable for {}", get<2>(item)).data());
#else
                    continue;
#endif
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
            char buffer[INET6_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET6, bytes, buffer, sizeof(buffer)) == nullptr) {
                return address;
            }
            return format("[{}]:{}", buffer, port);
        }
        return address;
    }

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
#if NEFLUX_PLATFORM_LINUX
        int64_t owners_at = -1;
        owners owner_data;
        uint64_t owners_sampled_at = 0;
#endif
    };

    connection_collector::connection_collector() :
    impl_(make_unique<impl>()) {}

    connection_collector::~connection_collector() = default;

    connection_sample connection_collector::collect(bool sockets) {
        connection_sample sample;
        sample.sampled_at_ = unix_time();
#if NEFLUX_PLATFORM_LINUX
        if (impl_->owners_at < 0 || monotonic_ms() - impl_->owners_at >= 5000) {
            impl_->owner_data = scan_owners();
            impl_->owners_at = monotonic_ms();
            impl_->owners_sampled_at = sample.sampled_at_;
        }
        sample.owners_sampled_at_ = impl_->owners_sampled_at;
        sample.owner_scan_complete_ = impl_->owner_data.complete;
        sample.unreadable_processes_ = impl_->owner_data.unreadable;
#else
        sample.owner_scan_complete_ = true;
        sample.owners_sampled_at_ = sample.sampled_at_;
#endif
        if (!sockets) {
            sample.status_ = "Loading connection details…";
            return sample;
        }
        bool native = true;
        try {
            sample.connections_ = collect_netlink();
        } catch (const exception& error) {
            native = false;
            sample.notes_.push_back(format("inet_diag unavailable; using procfs/ss: {}", error.what()));
        }
#if NEFLUX_PLATFORM_LINUX
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
                apply_tcp_info(sample.connections_, text);
            } catch (const exception& error) {
                sample.notes_.push_back(format(
                        "TCP rates unavailable: {}. Install iproute2 (ss); UDP byte counters remain unavailable.",
                        error.what()));
            }
        }
        sample.status_ = sample.notes_.empty() ? "Current network namespace" : "Partial socket sample";
        for (auto& row: sample.connections_) {
            const auto found = impl_->owner_data.pids.find(row.inode_);
            if (found != impl_->owner_data.pids.end()) {
                row.pids_ = found->second;
            }
        }
#else
        sample.status_ = native ? "Current network namespace" : "Socket table unavailable";
#endif
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
