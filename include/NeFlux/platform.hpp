#ifndef NEXUSFLUX_PLATFORM_HPP__
#define NEXUSFLUX_PLATFORM_HPP__

/**
 * @file platform.hpp
 * @brief 平台判定
 */

#include <NeForce/core/config/c++config.hpp>

#if defined(NEFORCE_PLATFORM_WINDOWS)
#    define NEFLUX_PLATFORM_WINDOWS 1
#    define NEFLUX_PLATFORM_LINUX 0
#    define NEFLUX_PLATFORM_MACOS 0
#    define NEFLUX_PLATFORM_FREEBSD 0
#elif defined(NEFORCE_PLATFORM_LINUX)
#    define NEFLUX_PLATFORM_WINDOWS 0
#    define NEFLUX_PLATFORM_LINUX 1
#    define NEFLUX_PLATFORM_MACOS 0
#    define NEFLUX_PLATFORM_FREEBSD 0
#elif defined(NEFORCE_PLATFORM_MACOS)
#    define NEFLUX_PLATFORM_WINDOWS 0
#    define NEFLUX_PLATFORM_LINUX 0
#    define NEFLUX_PLATFORM_MACOS 1
#    define NEFLUX_PLATFORM_FREEBSD 0
#elif defined(NEFORCE_PLATFORM_FREEBSD)
#    define NEFLUX_PLATFORM_WINDOWS 0
#    define NEFLUX_PLATFORM_LINUX 0
#    define NEFLUX_PLATFORM_MACOS 0
#    define NEFLUX_PLATFORM_FREEBSD 1
#else
#    error "NexusFlux supports Windows, Linux, macOS and FreeBSD"
#endif

#endif // NEXUSFLUX_PLATFORM_HPP__
