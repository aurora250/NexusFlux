#include "../platform_backend.hpp"

namespace neforce::neflux::platform {

    string docker_host() { return "unix:///var/run/docker.sock"; }

    void discover_ownership(inventory& data) { static_cast<void>(data); }

    void discover_virtual_machines(inventory& data) { static_cast<void>(data); }

} // namespace neforce::neflux::platform
