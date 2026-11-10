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
        try {
            const auto routes = run_command("netstat", {"-rn"}, 2500);
            text.append("## Routes (netstat -rn)\n");
            text.append(routes);
        } catch (const exception& error) {
            text.append(format("## Routes\nunavailable: {}\n", error.what()));
        }
        if (const auto resolv = read_text_file("/etc/resolv.conf"); resolv.has_value()) {
            text.append("\n## DNS (/etc/resolv.conf)\n");
            text.append(*resolv);
        }
        return text;
    }

} // namespace neforce::neflux::platform
