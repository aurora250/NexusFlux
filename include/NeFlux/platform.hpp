#ifndef NEFLUX_PLATFORM_HPP__
#define NEFLUX_PLATFORM_HPP__

/**
 * @file platform.hpp
 * @brief 平台判定
 */

#include <NeForce/core/config/c++config.hpp>

#if defined(NEFORCE_PLATFORM_WINDOWS)
#    define NEFLUX_PLATFORM_WINDOWS 1
#    define NEFLUX_PLATFORM_LINUX 0
#    define NEFLUX_PLATFORM_MACOS 0
#elif defined(NEFORCE_PLATFORM_LINUX)
#    define NEFLUX_PLATFORM_WINDOWS 0
#    define NEFLUX_PLATFORM_LINUX 1
#    define NEFLUX_PLATFORM_MACOS 0
#else
#    define NEFLUX_PLATFORM_WINDOWS 0
#    define NEFLUX_PLATFORM_LINUX 0
#    define NEFLUX_PLATFORM_MACOS 0
#endif

#endif // NEFLUX_PLATFORM_HPP__
