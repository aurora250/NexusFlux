#include "../platform_backend.hpp"
#include <NeFlux/cli.hpp>
#include <NeFlux/util.hpp>

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

} // namespace neforce::neflux::platform
