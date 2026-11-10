#include "../platform_backend.hpp"
#include <NeFlux/cli.hpp>
#include <NeFlux/util.hpp>
#include <NeFlux/discovery.hpp>

namespace neforce::neflux::platform {

    vector<string> capture_packets(const cli_args& args) {
        vector<string> command = {"-i", args.interface_, "-w", args.file_, "-c", format("{}", args.packets_), "-s", "0",
                                  "-q"};
        if (!args.capture_filter_.empty()) {
            command.push_back(args.capture_filter_);
        }
        const auto output = run_command("tcpdump", command, static_cast<int>(args.seconds_ * 1000 + 5000), true);
        return {output};
    }

    string network_routes_dns() {
        string text;
        auto append = [&text](const string& title, const string& value) {
            if (value.empty()) {
                return;
            }
            text.append("## ");
            text.append(title);
            text.push_back('\n');
            text.append(value);
            if (!text.ends_with("\n")) {
                text.push_back('\n');
            }
        };
        try {
            append("IPv4 routes", run_command("ip", {"-4", "route"}, 2000));
            append("IPv6 routes", run_command("ip", {"-6", "route"}, 2000));
        } catch (const exception& error) {
            append("Routes", format("unavailable: {}", error.what()));
        }
        if (const auto resolv = read_text_file("/etc/resolv.conf"); resolv.has_value()) {
            append("DNS (/etc/resolv.conf)", *resolv);
        }
        return text;
    }

} // namespace neforce::neflux::platform
