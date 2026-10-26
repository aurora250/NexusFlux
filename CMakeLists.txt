cmake_minimum_required(VERSION 3.19)

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
set(NEXUSFORCE_ROOT "" CACHE PATH "NexusForce source tree (optional)")
if(NOT NEXUSFORCE_ROOT)
    foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce-Linux" "${CMAKE_CURRENT_SOURCE_DIR}/../NexusForce")
        if(EXISTS "${candidate}/include/NeForce")
            set(NEXUSFORCE_ROOT "${candidate}")
            break()
        endif()
    endforeach()
endif()

include(cmake/NeFluxCompilerOptions.cmake)

# Accept either an installed NexusForce package or an existing source build tree.
if(NEXUSFORCE_ROOT AND EXISTS "${NEXUSFORCE_ROOT}/cmake-build-release-clang-x64/lib/Release/libNexusForce.so")
    add_library(NexusForce::NexusForce SHARED IMPORTED GLOBAL)
    set_target_properties(NexusForce::NexusForce PROPERTIES
        IMPORTED_LOCATION "${NEXUSFORCE_ROOT}/cmake-build-release-clang-x64/lib/Release/libNexusForce.so"
        INTERFACE_INCLUDE_DIRECTORIES "${NEXUSFORCE_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS "NEXUSFORCE_VERSION_MAJOR=1;NEXUSFORCE_VERSION_MINOR=0;NEXUSFORCE_VERSION_PATCH=1;NEXUSFORCE_VERSION=\"1.0.1\";NEFORCE_USING_MEMORY_POOL;NEFORCE_USING_SSO;NEFORCE_USING_AES_NI;NEFORCE_USING_PCLMUL;NEFORCE_USING_SQLCIPHER;NEFORCE_USING_IO_URING;NEFORCE_SUPPORT_POSTGRESQL;NEFORCE_SUPPORT_DB;NEFORCE_SUPPORT_MYSQL;NEFORCE_SUPPORT_SQLITE3;NEFORCE_SUPPORT_HIREDIS;NEFORCE_SUPPORT_ZLIB;NEFORCE_SUPPORT_LZ4;NEFORCE_SUPPORT_OPENSSL;NEFORCE_SUPPORT_PCRE2;PCRE2_CODE_UNIT_WIDTH=8;NEFORCE_SUPPORT_LOCALE;NEFORCE_SUPPORT_ICU;NEFORCE_USING_MEMORY_POOL_OVERRIDE")
    if(UNIX)
        set_property(TARGET NexusForce::NexusForce APPEND PROPERTY INTERFACE_LINK_OPTIONS "-Wl,-rpath,${NEXUSFORCE_ROOT}/cmake-build-release-clang-x64/lib/Release")
    endif()
else()
    find_package(NexusForce 1.0 REQUIRED)
endif()

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
    src/ui/painter.cpp
    src/ui/app.cpp
)

if(WIN32)
    list(APPEND NEFLUX_SOURCES src/collect_generic.cpp src/connections_generic.cpp)
elseif(APPLE)
    message(FATAL_ERROR "NexusFlux supports Linux and Windows 10+; macOS is not a target")
else()
    list(APPEND NEFLUX_SOURCES src/collect_linux.cpp src/connections_linux.cpp)
endif()

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

if(WIN32)
    target_link_libraries(NeFlux PUBLIC ws2_32 iphlpapi psapi dxgi user32 advapi32)
endif()

if(UNIX)
    add_custom_command(TARGET nexus-flux POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:nexus-flux> $<TARGET_FILE_DIR:nexus-flux>/neflux)
endif()

include(GNUInstallDirs)
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
    add_test(NAME NeFluxTests COMMAND NeFluxTests)
endif()
