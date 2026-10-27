cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# NexusForce dependency selection
#
# Configure-time switches (all cache variables):
#   NEXUSFLUX_NEXUSFORCE_MODE=auto|source|package|prebuilt
#   NEXUSFORCE_ROOT=<NexusForce checkout>          (default: ../NexusForce)
#   NEXUSFLUX_NEXUSFORCE_VCPKG_ROOT=<vcpkg root>  (source mode; default: config.json)
#   NEXUSFLUX_NEXUSFORCE_SOURCE_BUILD_DIR=<dir>   (source mode binary dir)
#   NEXUSFLUX_NEXUSFORCE_BUILD_DIR=<build tree>   (prebuilt/auto)
#   NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR=<prefix>     (package/auto)
#   NEXUSFLUX_DEPS_DIR=<dir>                      (.deps install root)
#
# "source" is the fully automatic path: it selects NexusForce's vcpkg
# toolchain before project(), adds the checkout as a subdirectory, and builds
# the library as part of the NexusFlux build.
# ---------------------------------------------------------------------------
set(NEXUSFLUX_NEXUSFORCE_MODE "auto" CACHE STRING
    "How NexusFlux obtains NexusForce: auto, source, package or prebuilt")
set_property(CACHE NEXUSFLUX_NEXUSFORCE_MODE PROPERTY STRINGS auto source package prebuilt)

set(NEXUSFORCE_ROOT "" CACHE PATH
    "NexusForce source tree (auto-detected from ../NexusForce)")
set(NEXUSFLUX_NEXUSFORCE_VCPKG_ROOT "" CACHE PATH
    "vcpkg root used by source mode (default: read from NexusForce/config.json)")
set(NEXUSFLUX_NEXUSFORCE_SOURCE_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/nexusforce" CACHE PATH
    "Build directory used for the in-tree NexusForce build")

include(cmake/NexusForceBootstrap.cmake)

project(NexusFlux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NEXUSFLUX_BUILD_TESTS "Build NeFlux unit tests" ON)
option(NEXUSFLUX_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

include(cmake/NeFluxCompilerOptions.cmake)

# Prebuilt search roots.  .deps/NexusForce is what scripts/install_nexusforce.py
# produces; a NexusForce build tree or an installed package can be pointed at
# explicitly.
set(NEXUSFLUX_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH
    "Local dependency install root")
set(NEXUSFLUX_NEXUSFORCE_BUILD_DIR "" CACHE PATH
    "NexusForce build tree used by prebuilt/auto mode")
set(NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR "" CACHE PATH
    "Installed NexusForce prefix used by package/auto mode")

include(cmake/NexusForceDependency.cmake)

include(GNUInstallDirs)

set(NEFLUX_SOURCES
    src/cli.cpp
    src/config.cpp
    src/json.cpp
    src/model.cpp
    src/util.cpp
    src/plot.cpp
    src/history.cpp
    src/filter.cpp
    src/tags.cpp
    src/diagnostics.cpp
    src/discovery.cpp
    src/recording.cpp
    src/gpu.cpp
    src/ros.cpp
    src/taskstats.cpp
    src/advanced.cpp
    src/collect.cpp
    src/connections.cpp
    src/ui/painter.cpp
    src/ui/app.cpp
)

add_library(NeFlux STATIC ${NEFLUX_SOURCES})
add_library(NeFlux::NeFlux ALIAS NeFlux)
target_include_directories(NeFlux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(NeFlux PUBLIC NexusForce::NexusForce)
neflux_compiler_options(NeFlux)
set_target_properties(NeFlux PROPERTIES FOLDER "library")

add_executable(nexus-flux src/main.cpp)
target_link_libraries(nexus-flux PRIVATE NeFlux::NeFlux)
neflux_compiler_options(nexus-flux)
set_target_properties(nexus-flux PROPERTIES FOLDER "tools")
nexusflux_deploy_nexusforce_runtime(nexus-flux)

if(WIN32)
    target_link_libraries(NeFlux PUBLIC ws2_32 iphlpapi psapi dxgi user32 advapi32)
endif()

if(UNIX)
    add_custom_command(TARGET nexus-flux POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:nexus-flux> $<TARGET_FILE_DIR:nexus-flux>/neflux)
    set_target_properties(nexus-flux PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

include(CMakePackageConfigHelpers)
install(TARGETS NeFlux nexus-flux
    EXPORT NeFluxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT NeFluxTargets NAMESPACE NeFlux:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeFlux)
configure_package_config_file(cmake/NeFluxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NeFluxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeFlux)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NeFluxConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/NeFluxConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/NeFluxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeFlux)
if(UNIX)
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink nexus-flux \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/neflux)")
endif()

include(cmake/NexusForceClangFormat.cmake)

if(NEXUSFLUX_BUILD_TESTS)
    enable_testing()
    add_executable(NeFluxTests tests/unit/test_main.cpp)
    target_link_libraries(NeFluxTests PRIVATE NeFlux::NeFlux)
    neflux_compiler_options(NeFluxTests)
    nexusflux_deploy_nexusforce_runtime(NeFluxTests)
    add_test(NAME NeFluxTests COMMAND NeFluxTests)
endif()
