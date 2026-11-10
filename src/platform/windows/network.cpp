#include "../platform_backend.hpp"
#include <NeForce/core/string/format.hpp>
#include <NeFlux/cli.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeFlux/discovery.hpp>

namespace neforce::neflux::platform {

    vector<string> capture_packets(const cli_args& args) {
        static_cast<void>(args);
        NEFORCE_THROW_EXCEPTION(value_exception("Packet capture is not supported on Windows"));
    }

    string network_routes_dns() {
        string text;
        try {
            text.append("## Routes\n");
            text.append(run_command("route", {"print"}, 3000));
        } catch (const exception& error) {
            text.append(format("## Routes\nunavailable: {}\n", error.what()));
        }
        try {
            text.append("\n## DNS / adapters\n");
            text.append(run_command("ipconfig", {"/all"}, 3000));
        } catch (const exception& error) {
            text.append(format("\n## DNS / adapters\nunavailable: {}\n", error.what()));
        }
        return text;
    }

} // namespace neforce::neflux::platform
