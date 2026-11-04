#ifndef NEXUSFLUX_EXTRAS_HPP__
#define NEXUSFLUX_EXTRAS_HPP__

#include <NeFlux/config.hpp>
#include <NeFlux/json.hpp>

namespace neforce::neflux {
    NEFORCE_NODISCARD json discover_config_extras(const config& cfg);
} // namespace neforce::neflux
#endif // NEXUSFLUX_EXTRAS_HPP__
