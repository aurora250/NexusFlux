# NexusForceBootstrap.cmake - pre-project() setup for NexusFlux's NexusForce dependency.
#
# This file is included before project() on purpose.  In "source" mode NexusForce
# is added as a subdirectory, and NexusForce is a vcpkg manifest project: its
# toolchain and manifest variables must be in place before CMake initializes the
# compilers, otherwise find_package(OpenSSL/pcre2/...) cannot see the vcpkg
# packages that NexusForce's vcpkg.json installs.
#
# Everything here is a no-op unless NEXUSFLUX_NEXUSFORCE_MODE is "source".

set(NEXUSFLUX_NEXUSFORCE_MODES auto source package prebuilt)
if(NOT NEXUSFLUX_NEXUSFORCE_MODE IN_LIST NEXUSFLUX_NEXUSFORCE_MODES)
    message(FATAL_ERROR
        "NexusFlux: NEXUSFLUX_NEXUSFORCE_MODE='${NEXUSFLUX_NEXUSFORCE_MODE}' is invalid.\n"
        "Valid values: ${NEXUSFLUX_NEXUSFORCE_MODES}")
endif()

# Auto-detect the sibling checkout only when the user did not pass a path.
if(NOT NEXUSFORCE_ROOT)
    if(WIN32)
        set(_nexusflux_nf_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce-Windows"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce-Linux")
    else()
        set(_nexusflux_nf_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce-Linux"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce-Windows")
    endif()
    foreach(_nexusflux_nf_candidate IN LISTS _nexusflux_nf_candidates)
        if(EXISTS "${_nexusflux_nf_candidate}/include/NeForce")
            get_filename_component(_nexusflux_nf_candidate "${_nexusflux_nf_candidate}" ABSOLUTE)
            file(TO_CMAKE_PATH "${_nexusflux_nf_candidate}" _nexusflux_nf_candidate)
            set(NEXUSFORCE_ROOT "${_nexusflux_nf_candidate}"
                CACHE PATH "NexusForce source tree (used by source mode)" FORCE)
            break()
        endif()
    endforeach()
    unset(_nexusflux_nf_candidate)
    unset(_nexusflux_nf_candidates)
endif()

if(NEXUSFORCE_ROOT)
    get_filename_component(_nexusflux_nf_root "${NEXUSFORCE_ROOT}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_nexusflux_nf_root}" _nexusflux_nf_root)
    set(NEXUSFORCE_ROOT "${_nexusflux_nf_root}"
        CACHE PATH "NexusForce source tree (used by source mode)" FORCE)
    unset(_nexusflux_nf_root)
endif()

if(NOT NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "source")
    return()
endif()

if(NOT EXISTS "${NEXUSFORCE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "NexusFlux: source mode needs a NexusForce checkout, but NEXUSFORCE_ROOT\n"
        "  '${NEXUSFORCE_ROOT}'\n"
        "does not contain CMakeLists.txt.  Pass -DNEXUSFORCE_ROOT=<path>.")
endif()

# Read the vcpkg root from NexusForce's config.json, falling back to the
# VCPKG_ROOT environment variable.  This mirrors NexusForce's own top-level
# CMakeLists so an embedded build uses the same package manager configuration.
function(_nexusflux_nexusforce_config_vcpkg out_var)
    set(_json "${NEXUSFORCE_ROOT}/config.json")
    if(NOT EXISTS "${_json}")
        return()
    endif()

    file(READ "${_json}" _content)
    if(WIN32)
        string(JSON _root ERROR_VARIABLE _error GET "${_content}" vcpkg windows)
    else()
        string(JSON _root ERROR_VARIABLE _error GET "${_content}" vcpkg linux)
    endif()
    if(_error OR NOT _root)
        return()
    endif()

    if(_root MATCHES "^~([/\\\\]|$)")
        if(WIN32)
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _home)
        else()
            file(TO_CMAKE_PATH "$ENV{HOME}" _home)
        endif()
        string(REPLACE "~" "${_home}" _root "${_root}")
    endif()
    file(TO_CMAKE_PATH "${_root}" _root)
    set(${out_var} "${_root}" PARENT_SCOPE)
endfunction()

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(_nexusflux_nf_vcpkg_root "")
    if(NEXUSFLUX_NEXUSFORCE_VCPKG_ROOT)
        set(_nexusflux_nf_vcpkg_root "${NEXUSFLUX_NEXUSFORCE_VCPKG_ROOT}")
    elseif(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(_nexusflux_nf_vcpkg_root "$ENV{VCPKG_ROOT}")
    else()
        _nexusflux_nexusforce_config_vcpkg(_nexusflux_nf_vcpkg_root)
    endif()

    if(_nexusflux_nf_vcpkg_root)
        get_filename_component(_nexusflux_nf_vcpkg_root "${_nexusflux_nf_vcpkg_root}" ABSOLUTE)
        file(TO_CMAKE_PATH "${_nexusflux_nf_vcpkg_root}" _nexusflux_nf_vcpkg_root)
        if(EXISTS "${_nexusflux_nf_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE
                "${_nexusflux_nf_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "vcpkg toolchain used to build NexusForce from source" FORCE)
        endif()
    endif()
    unset(_nexusflux_nf_vcpkg_root)

    if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
        message(FATAL_ERROR
            "NexusFlux: source mode needs vcpkg, but no vcpkg toolchain was found.\n"
            "  Pass -DNEXUSFLUX_NEXUSFORCE_VCPKG_ROOT=<vcpkg root>,\n"
            "  set the VCPKG_ROOT environment variable,\n"
            "  or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake.")
    endif()
endif()

# Install NexusForce's vcpkg.json dependencies into the NexusFlux build tree
# (not into the NexusForce checkout).  The toolchain caches these before
# project(), so they must be set here rather than in the source-mode branch
# after project().
set(VCPKG_MANIFEST_MODE ON)
set(VCPKG_MANIFEST_DIR "${NEXUSFORCE_ROOT}")
if(NOT VCPKG_INSTALLED_DIR)
    set(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()

message(STATUS "NexusFlux: NexusForce source mode enabled (${NEXUSFORCE_ROOT})")
message(STATUS "NexusFlux: vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
