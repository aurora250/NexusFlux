#ifndef NEXUSFLUX_NET_TOOLS_HPP__
#define NEXUSFLUX_NET_TOOLS_HPP__

#include <NeFlux/common.hpp>

namespace neforce::neflux {
    NEFORCE_NODISCARD string check_target(const string& target);
    NEFORCE_NODISCARD string summarize_capture(const string& file_path);
} // namespace neforce::neflux
#endif // NEXUSFLUX_NET_TOOLS_HPP__
