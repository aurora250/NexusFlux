#include "../platform_backend.hpp"
#include <NeFlux/cli.hpp>
#include <NeForce/core/exception/exception.hpp>

namespace neforce::neflux::platform {

    vector<string> capture_packets(const cli_args& args) {
        static_cast<void>(args);
        NEFORCE_THROW_EXCEPTION(value_exception("Packet capture is not supported on Windows"));
    }

} // namespace neforce::neflux::platform
