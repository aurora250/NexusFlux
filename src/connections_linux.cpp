#include <NeFlux/common.hpp>
#include <NeFlux/connections.hpp>

#include <NeFlux/discovery.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
#include <arpa/inet.h>
#include <dirent.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
namespace neforce::neflux {

    namespace {

        // TODO: using neforce::endian instead

        uint32_t read_u32(const void* data, size_t offset, size_t size) {
            if (offset + 4 > size) {
                return 0;
            }
            uint32_t value = 0;
            memory_copy(&value, static_cast<const char*>(data) + offset, 4);
            return value;
        }

        uint64_t read_u64(const void* data, size_t offset, size_t size) {
            if (offset + 8 > size) {
                return 0;
            }
            uint64_t value = 0;
            memory_copy(&value, static_cast<const char*>(data) + offset, 8);
            return value;
        }

        // TODO: using neforce::ip_address / ports instead

        string endpoint_text(const uint8_t* data, size_t address_offset, size_t port_offset, size_t size) {
            if (size < port_offset + 2 || size < address_offset + 4) {
                return {};
            }
            uint16_t port = 0;
            memory_copy(&port, data + port_offset, 2);
            const uint16_t host_port = ntohs(port);
            char buffer[INET6_ADDRSTRLEN] = {};
            if (data[0] == AF_INET) {
                if (inet_ntop(AF_INET, data + address_offset, buffer, sizeof(buffer)) == nullptr) {
                    return {};
                }
                return format("{}:{}", buffer, host_port);
            }
            if (data[0] == AF_INET6) {
                if (inet_ntop(AF_INET6, data + address_offset, buffer, sizeof(buffer)) == nullptr) {
                    return {};
                }
                return format("[{}]:{}", buffer, host_port);
            }
            return {};
        }

        string state_name(uint8_t state, const string& protocol) {
            switch (state) {
                case 1:
                    return "ESTABLISHED";
                case 2:
                    return "SYN_SENT";
                case 3:
                    return "SYN_RECV";
                case 4:
                    return "FIN_WAIT1";
                case 5:
                    return "FIN_WAIT2";
                case 6:
                    return "TIME_WAIT";
                case 7:
                    return starts_with(protocol.view(), "udp") ? "BOUND" : "CLOSED";
                case 8:
                    return "CLOSE_WAIT";
                case 9:
                    return "LAST_ACK";
                case 10:
                    return "LISTEN";
                case 11:
                    return "CLOSING";
                default:
                    return "UNKNOWN";
            }
        }

        struct owners {
            map<uint64_t, vector<uint32_t>> pids;
            map<uint32_t, string> names;
            bool complete = true;
            uint64_t unreadable = 0;
        };

        owners scan_owners() {
            owners result;
            DIR* proc = opendir("/proc");
            if (proc == nullptr) {
                result.complete = false;
                return result;
            }
            const int64_t deadline = monotonic_ms() + 700;
            while (dirent* entry = readdir(proc)) {
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
                const string base = format("/proc/{}", pid);
                const string fd_directory = base + "/fd";
                DIR* descriptors = opendir(fd_directory.data());
                if (descriptors == nullptr) {
                    ++result.unreadable;
                    continue;
                }
                if (const auto comm = read_text_file(base + "/comm"); comm.has_value()) {
                    result.names[pid] = trim(comm->view());
                }
                while (dirent* descriptor = readdir(descriptors)) {
                    if (monotonic_ms() > deadline) {
                        result.complete = false;
                        break;
                    }
                    if (descriptor->d_name[0] == '.') {
                        continue;
                    }
                    const string link_path = fd_directory + "/" + descriptor->d_name;
                    char target[512] = {};
                    const ssize_t length = readlink(link_path.data(), target, sizeof(target) - 1);
                    if (length <= 0) {
                        continue;
                    }
                    target[length] = '\0';
                    const string_view text(target, static_cast<size_t>(length));
                    if (!starts_with(text, "socket:[")) {
                        continue;
                    }
                    const auto close = text.find(']', 8);
                    if (close == string_view::npos) {
                        continue;
                    }
                    const auto inode_value = parse_u64(text.substr(8, close - 8));
                    if (!inode_value.has_value()) {
                        continue;
                    }
                    auto& list = result.pids[*inode_value];
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
                closedir(descriptors);
                if (!result.complete) {
                    break;
                }
            }
            closedir(proc);
            return result;
        }

        vector<connection> collect_netlink() {
            vector<connection> rows;
            const int descriptor = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
            if (descriptor < 0) {
                throw value_exception("socket: SOCK_DIAG unavailable");
            }
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 250000;
            setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_nl address{};
            address.nl_family = AF_NETLINK;
            if (bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
                close(descriptor);
                throw value_exception("bind: netlink unavailable");
            }
            const int64_t started = monotonic_ms();
            struct request_packet {
                nlmsghdr header;
                inet_diag_req_v2 request;
            };
            vector<uint8_t> buffer(65536);
            uint32_t sequence = 1;
            try {
                for (const auto& item: vector<tuple<int, int, const char*>>{{AF_INET, IPPROTO_TCP, "tcp"},
                                                                            {AF_INET6, IPPROTO_TCP, "tcp6"},
                                                                            {AF_INET, IPPROTO_UDP, "udp"},
                                                                            {AF_INET6, IPPROTO_UDP, "udp6"}}) {
                    request_packet packet{};
                    packet.header.nlmsg_len = sizeof(packet);
                    packet.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
                    packet.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
                    packet.header.nlmsg_seq = sequence++;
                    packet.request.sdiag_family = static_cast<uint8_t>(get<0>(item));
                    packet.request.sdiag_protocol = static_cast<uint8_t>(get<1>(item));
                    packet.request.idiag_ext = 2;
                    packet.request.idiag_states = 0xFFFFFFFFU;
                    packet.request.id.idiag_cookie[0] = 0xFFFFFFFFU;
                    packet.request.id.idiag_cookie[1] = 0xFFFFFFFFU;
                    if (sendto(descriptor, &packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&address),
                               sizeof(address)) < 0) {
                        throw value_exception("sendto: socket dump unavailable");
                    }
                    bool done = false;
                    while (!done) {
                        if (monotonic_ms() - started > 1000) {
                            throw value_exception("socket dump deadline");
                        }
                        const ssize_t received = recv(descriptor, buffer.data(), buffer.size(), MSG_TRUNC);
                        if (received < 0) {
                            throw value_exception("recv: socket dump failed");
                        }
                        if (received == 0 || static_cast<size_t>(received) > buffer.size()) {
                            throw value_exception("truncated socket dump");
                        }
                        size_t offset = 0;
                        while (offset + sizeof(nlmsghdr) <= static_cast<size_t>(received)) {
                            const auto* header = reinterpret_cast<const nlmsghdr*>(buffer.data() + offset);
                            const size_t length = header->nlmsg_len;
                            if (length < sizeof(nlmsghdr) || length > static_cast<size_t>(received) - offset) {
                                throw value_exception("invalid netlink message");
                            }
                            if (header->nlmsg_type == NLMSG_DONE) {
                                done = true;
                                break;
                            }
                            if (header->nlmsg_type == NLMSG_ERROR) {
                                throw value_exception("netlink error");
                            }
                            const uint8_t* payload = buffer.data() + offset + sizeof(nlmsghdr);
                            const size_t payload_size = length - sizeof(nlmsghdr);
                            if (payload_size >= 72) {
                                const uint8_t family = payload[0];
                                if (family == AF_INET || family == AF_INET6) {
                                    connection row;
                                    row.protocol_ = get<2>(item);
                                    row.local_ = endpoint_text(payload, 8, 4, payload_size);
                                    row.remote_ = endpoint_text(payload, 24, 6, payload_size);
                                    row.state_ = state_name(payload[1], row.protocol_);
                                    row.inode_ = read_u32(payload, 68, payload_size);
                                    const uint64_t cookie = read_u64(payload, 44, payload_size);
                                    row.receive_queue_ = read_u32(payload, 56, payload_size);
                                    row.send_queue_ = read_u32(payload, 60, payload_size);
                                    row.id_ = format("{}:{}:{}:{}:{:x}", row.protocol_, row.inode_, row.local_,
                                                     row.remote_, cookie);
                                    size_t attribute = 72;
                                    while (attribute + 4 <= payload_size) {
                                        uint16_t raw_length = 0;
                                        uint16_t raw_type = 0;
                                        memory_copy(&raw_length, payload + attribute, 2);
                                        memory_copy(&raw_type, payload + attribute + 2, 2);
                                        const size_t attribute_size = raw_length;
                                        const size_t kind = raw_type & 0x3FFFU;
                                        if (attribute_size < 4 || attribute_size > payload_size - attribute) {
                                            break;
                                        }
                                        if (kind == INET_DIAG_INFO && starts_with(row.protocol_.view(), "tcp")) {
                                            const uint8_t* info = payload + attribute + 4;
                                            const size_t info_size = attribute_size - 4;
                                            if (info_size > 68) {
                                                row.rtt_ms_ =
                                                        static_cast<double>(read_u32(info, 68, info_size)) / 1000.0;
                                            }
                                            if (info_size > 100) {
                                                row.retransmits_ = read_u32(info, 100, info_size);
                                            }
                                            if (info_size >= 136) {
                                                row.received_ = read_u64(info, 128, info_size);
                                            }
                                            if (info_size >= 208) {
                                                row.sent_ = read_u64(info, 200, info_size);
                                            }
                                        }
                                        attribute += (attribute_size + 3) & ~static_cast<size_t>(3);
                                    }
                                    if (!row.local_.empty() && !row.remote_.empty()) {
                                        rows.push_back(move(row));
                                    }
                                }
                            }
                            offset += (length + 3) & ~static_cast<size_t>(3);
                        }
                    }
                }
            } catch (...) {
                close(descriptor);
                throw;
            }
            close(descriptor);
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
        map<uint64_t, map<string, string>> data;
        for (const auto& line: split_lines(text)) {
            map<string, string> fields;
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
        map<string, pair<optional<uint64_t>, optional<uint64_t>>> previous;
        double previous_at = 0.0;
        int64_t owners_at = -1;
        owners owner_data;
        uint64_t owners_sampled_at = 0;
        string status;
    };

    connection_collector::connection_collector() :
    impl_(make_unique<impl>()) {}

    connection_collector::~connection_collector() = default;

    connection_sample connection_collector::collect(bool sockets) {
        connection_sample sample;
        sample.sampled_at_ = unix_time();
        if (impl_->owners_at < 0 || monotonic_ms() - impl_->owners_at >= 5000) {
            impl_->owner_data = scan_owners();
            impl_->owners_at = monotonic_ms();
            impl_->owners_sampled_at = sample.sampled_at_;
        }
        sample.owners_sampled_at_ = impl_->owners_sampled_at;
        sample.owner_scan_complete_ = impl_->owner_data.complete;
        sample.unreadable_processes_ = impl_->owner_data.unreadable;
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
                for (const uint32_t pid: row.pids_) {
                    const auto name = impl_->owner_data.names.find(pid);
                    if (name != impl_->owner_data.names.end()) {
                        row.process_names_.push_back(name->second);
                    }
                }
            }
        }
        if (sample.connections_.size() > 20000) {
            sample.connections_.resize(20000);
            sample.truncated_ = true;
        }
        const double now = static_cast<double>(monotonic_ms()) / 1000.0;
        const double seconds = impl_->previous_at > 0.0 ? now - impl_->previous_at : 0.0;
        map<string, pair<optional<uint64_t>, optional<uint64_t>>> next;
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
        impl_->previous = next;
        impl_->previous_at = now;
        impl_->status = sample.status_;
        return sample;
    }

    const string& connection_collector::status() const noexcept { return impl_->status; }

} // namespace neforce::neflux
