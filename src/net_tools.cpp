#include "net_tools.hpp"
#include <NeFlux/util.hpp>
#include <NeForce/core/async/io_context.hpp>
#include <NeForce/network/http/http_client.hpp>
#include <NeForce/network/tcp/tcp_client.hpp>

namespace neforce::neflux {

    string check_target(const string& target) {
        if (starts_with(target.view(), "http://") || starts_with(target.view(), "https://")) {
            const int64_t began = monotonic_ms();
            try {
                neforce::io_context context;
                neforce::http::http_client client(context);
                client.set_timeout(neforce::milliseconds(5000));
                client.set_follow_redirects(true);
                const auto response = client.get(target);
                const double elapsed = static_cast<double>(monotonic_ms() - began) / 1000.0;
                return format("HTTP {} · {} in {:.2f}s", static_cast<int>(response.status),
                              response.is_success() ? "success" : "failure", elapsed);
            } catch (const exception& error) {
                return format("HTTP check failed: {}", error.what());
            }
        }
        string host = target;
        const auto slash = host.find('/');
        if (slash != string::npos) {
            host = host.substr(0, slash);
        }
        uint16_t port = 80;
        const auto colon = host.rfind(':');
        if (colon != string::npos) {
            const auto value = parse_u64(host.view().substr(colon + 1));
            if (!value.has_value() || *value == 0 || *value > 65535) {
                return format("Invalid TCP port in {}", target);
            }
            port = static_cast<uint16_t>(*value);
            host = host.substr(0, colon);
        }
        if (host.empty()) {
            return "Target must be a URL or host:port";
        }
        const int64_t began = monotonic_ms();
        try {
            neforce::io_context context;
            neforce::tcp_client client(context);
            client.set_connect_timeout(neforce::milliseconds(4000));
            const bool connected = client.connect(host, neforce::ports(port));
            const double elapsed = static_cast<double>(monotonic_ms() - began) / 1000.0;
            if (!connected) {
                return format("TCP {}:{} failed ({:.2f}s)", host, static_cast<uint64_t>(port), elapsed);
            }
            client.disconnect();
            return format("TCP {}:{} connected in {:.2f}s", host, static_cast<uint64_t>(port), elapsed);
        } catch (const exception& error) {
            const double elapsed = static_cast<double>(monotonic_ms() - began) / 1000.0;
            return format("TCP {}:{} failed: {} ({:.2f}s)", host, static_cast<uint64_t>(port), error.what(), elapsed);
        }
    }

    string summarize_capture(const string& file_path) {
        const auto text = read_text_file(file_path);
        if (!text.has_value() || text->size() < 24) {
            return format("Cannot read PCAP: {}", file_path);
        }
        const auto byte_at = [&text](size_t offset) -> uint8_t { return static_cast<uint8_t>((*text)[offset]); };
        const uint32_t magic = static_cast<uint32_t>(byte_at(0)) | (static_cast<uint32_t>(byte_at(1)) << 8) |
                               (static_cast<uint32_t>(byte_at(2)) << 16) | (static_cast<uint32_t>(byte_at(3)) << 24);
        const bool little = magic == 0xA1B2C3D4;
        const bool nano = magic == 0xA1B23C4D;
        if (!little && !nano) {
            return "Not a little-endian PCAP file";
        }
        const auto read32 = [&text](size_t offset) {
            return static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset])) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 1])) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 2])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 3])) << 24);
        };
        string out = format("PCAP {} · linktype {} · {} bytes\n", file_path, read32(20), text->size());
        size_t offset = 24;
        size_t index = 0;
        while (offset + 16 <= text->size() && index < 60) {
            const uint32_t seconds = read32(offset);
            const uint32_t captured = read32(offset + 8);
            const uint32_t original = read32(offset + 12);
            offset += 16;
            if (captured > text->size() - offset) {
                out.append("truncated packet record\n");
                break;
            }
            out.append(format("#{} t={} len={}/{} cap={}\n", index + 1, seconds, original, captured, captured));
            offset += captured;
            ++index;
        }
        return out;
    }

} // namespace neforce::neflux
