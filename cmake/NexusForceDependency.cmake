# NexusForceDependency.cmake - resolve NexusForce and expose NexusForce::NexusForce.
#
# Four modes are supported through NEXUSFLUX_NEXUSFORCE_MODE:
#
#   auto      .deps/NexusForce install tree, then NEXUSFLUX_NEXUSFORCE_BUILD_DIR,
#             then find_package(NexusForce CONFIG).  This is the historical
#             behaviour and never compiles NexusForce by itself.
#   source    add_subdirectory() the NexusForce checkout and compile it as part
#             of the NexusFlux build.  This is the "one switch builds the
#             dependency" path; see NexusForceBootstrap.cmake for the vcpkg
#             setup that has to happen before project().
#   package   find_package(NexusForce 1.0 CONFIG REQUIRED).
#   prebuilt  only .deps/NexusForce or NEXUSFLUX_NEXUSFORCE_BUILD_DIR.
#
# Only source mode links the real NexusForce target, so its PUBLIC compile
# definitions (memory pool, SSO, SQLCipher, ICU, locale, ...) come from
# NexusForce itself instead of a hand-maintained copy.

# Compile definitions for an installed or prebuilt NexusForce tree.  These must
# mirror the options NexusForce was built with; NexusForce's headers change
# their layout/ABI with these macros (notably NEFORCE_USING_MEMORY_POOL and
# NEFORCE_USING_SSO), so the list is intentionally kept in sync with the
# default config.json used by scripts/install_nexusforce.py.
set(NEXUSFLUX_NEXUSFORCE_PREBUILT_DEFINITIONS
    NEXUSFORCE_VERSION_MAJOR=2
    NEXUSFORCE_VERSION_MINOR=0
    NEXUSFORCE_VERSION_PATCH=0
    "NEXUSFORCE_VERSION=\"2.0.0\""
    NEFORCE_USING_MEMORY_POOL
    NEFORCE_USING_SSO
    NEFORCE_USING_AES_NI
    NEFORCE_USING_PCLMUL
    NEFORCE_USING_SQLCIPHER
    NEFORCE_USING_IO_URING
    NEFORCE_SUPPORT_POSTGRESQL
    NEFORCE_SUPPORT_DB
    NEFORCE_SUPPORT_MYSQL
    NEFORCE_SUPPORT_SQLITE3
    NEFORCE_SUPPORT_SQLCIPHER
    NEFORCE_SUPPORT_HIREDIS
    NEFORCE_SUPPORT_ZLIB
    NEFORCE_SUPPORT_LZ4
    NEFORCE_SUPPORT_OPENSSL
    NEFORCE_SUPPORT_PCRE2
    PCRE2_CODE_UNIT_WIDTH=8
    NEFORCE_SUPPORT_LOCALE
    NEFORCE_SUPPORT_ICU
    SQLITE_HAS_CODEC
    SQLITE_TEMP_STORE=2
)

# Add an imported target for an installed NexusForce tree such as
# .deps/NexusForce.  Both Release (NexusForce.dll) and Debug (NexusForced.dll)
# installs are recognised.
function(_nexusflux_nexusforce_import_prefix prefix)
    if(TARGET NexusForce::NexusForce)
        return()
    endif()

    add_library(NexusForce::NexusForce SHARED IMPORTED GLOBAL)
    set_target_properties(NexusForce::NexusForce PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include"
        INTERFACE_COMPILE_DEFINITIONS "${NEXUSFLUX_NEXUSFORCE_PREBUILT_DEFINITIONS}")

    if(WIN32)
        set(_nexusflux_nf_any FALSE)
        set(_nexusflux_nf_configs "")
        foreach(_nexusflux_nf_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${_nexusflux_nf_config}" _nexusflux_nf_CONFIG)
            if(_nexusflux_nf_config STREQUAL "Debug")
                set(_nexusflux_nf_name "NexusForced")
            else()
                set(_nexusflux_nf_name "NexusForce")
            endif()
            if(EXISTS "${prefix}/bin/${_nexusflux_nf_name}.dll"
               AND EXISTS "${prefix}/lib/${_nexusflux_nf_name}.lib")
                set_target_properties(NexusForce::NexusForce PROPERTIES
                    IMPORTED_LOCATION_${_nexusflux_nf_CONFIG} "${prefix}/bin/${_nexusflux_nf_name}.dll"
                    IMPORTED_IMPLIB_${_nexusflux_nf_CONFIG} "${prefix}/lib/${_nexusflux_nf_name}.lib")
                list(APPEND _nexusflux_nf_configs "${_nexusflux_nf_CONFIG}")
                set(_nexusflux_nf_any TRUE)
            endif()
        endforeach()
        if(_nexusflux_nf_configs)
            set_target_properties(NexusForce::NexusForce PROPERTIES
                IMPORTED_CONFIGURATIONS "${_nexusflux_nf_configs}")
        endif()

        # Single-config installs only have one of the two names; also provide a
        # generic location so a Release install can serve any consumer config.
        if(EXISTS "${prefix}/bin/NexusForce.dll" AND EXISTS "${prefix}/lib/NexusForce.lib")
            set_target_properties(NexusForce::NexusForce PROPERTIES
                IMPORTED_LOCATION "${prefix}/bin/NexusForce.dll"
                IMPORTED_IMPLIB "${prefix}/lib/NexusForce.lib")
            set(_nexusflux_nf_any TRUE)
        elseif(EXISTS "${prefix}/bin/NexusForced.dll" AND EXISTS "${prefix}/lib/NexusForced.lib")
            set_target_properties(NexusForce::NexusForce PROPERTIES
                IMPORTED_LOCATION "${prefix}/bin/NexusForced.dll"
                IMPORTED_IMPLIB "${prefix}/lib/NexusForced.lib")
            set(_nexusflux_nf_any TRUE)
        endif()

        if(NOT _nexusflux_nf_any)
            set_property(TARGET NexusForce::NexusForce PROPERTY
                _NEXUSFLUX_IMPORT_INCOMPLETE TRUE)
        endif()
    else()
        if(EXISTS "${prefix}/lib/libNexusForce.so")
            set(_nexusflux_nf_lib "${prefix}/lib/libNexusForce.so")
        elseif(EXISTS "${prefix}/lib/libNexusForce.dylib")
            set(_nexusflux_nf_lib "${prefix}/lib/libNexusForce.dylib")
        else()
            file(GLOB _nexusflux_nf_libs
                "${prefix}/lib/libNexusForce.so.*"
                "${prefix}/lib/libNexusForce.*.dylib")
            list(SORT _nexusflux_nf_libs)
            list(LENGTH _nexusflux_nf_libs _nexusflux_nf_len)
            if(_nexusflux_nf_len GREATER 0)
                list(GET _nexusflux_nf_libs 0 _nexusflux_nf_lib)
            endif()
        endif()

        if(_nexusflux_nf_lib)
            set_target_properties(NexusForce::NexusForce PROPERTIES
                IMPORTED_LOCATION "${_nexusflux_nf_lib}"
                INTERFACE_LINK_OPTIONS "-Wl,-rpath,${prefix}/lib")
        else()
            set_property(TARGET NexusForce::NexusForce PROPERTY
                _NEXUSFLUX_IMPORT_INCOMPLETE TRUE)
        endif()
    endif()
endfunction()

# Add an imported target for a NexusForce build tree (cmake-build-*/ or build/).
# NexusForce writes binaries to bin/<Config> and libraries to lib/<Config>.
function(_nexusflux_nexusforce_import_build_tree build_dir)
    if(TARGET NexusForce::NexusForce)
        return()
    endif()

    set(_nexusflux_nf_found FALSE)
    set(_nexusflux_nf_location "")
    set(_nexusflux_nf_implib "")
    set(_nexusflux_nf_props "")
    set(_nexusflux_nf_configs "")

    foreach(_nexusflux_nf_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${_nexusflux_nf_config}" _nexusflux_nf_CONFIG)
        if(_nexusflux_nf_config STREQUAL "Debug")
            set(_nexusflux_nf_postfix "d")
        else()
            set(_nexusflux_nf_postfix "")
        endif()

        if(WIN32)
            set(_nexusflux_nf_dll "${build_dir}/bin/${_nexusflux_nf_config}/NexusForce${_nexusflux_nf_postfix}.dll")
            set(_nexusflux_nf_lib "${build_dir}/lib/${_nexusflux_nf_config}/NexusForce${_nexusflux_nf_postfix}.lib")
            if(NOT EXISTS "${_nexusflux_nf_dll}")
                set(_nexusflux_nf_dll "${build_dir}/bin/NexusForce${_nexusflux_nf_postfix}.dll")
                set(_nexusflux_nf_lib "${build_dir}/lib/NexusForce${_nexusflux_nf_postfix}.lib")
            endif()
            if(EXISTS "${_nexusflux_nf_dll}" AND EXISTS "${_nexusflux_nf_lib}")
                list(APPEND _nexusflux_nf_props
                    IMPORTED_LOCATION_${_nexusflux_nf_CONFIG} "${_nexusflux_nf_dll}"
                    IMPORTED_IMPLIB_${_nexusflux_nf_CONFIG} "${_nexusflux_nf_lib}")
                list(APPEND _nexusflux_nf_configs "${_nexusflux_nf_CONFIG}")
                if(NOT _nexusflux_nf_location)
                    set(_nexusflux_nf_location "${_nexusflux_nf_dll}")
                    set(_nexusflux_nf_implib "${_nexusflux_nf_lib}")
                endif()
                set(_nexusflux_nf_found TRUE)
            endif()
        else()
            set(_nexusflux_nf_so "${build_dir}/lib/${_nexusflux_nf_config}/libNexusForce.so")
            if(NOT EXISTS "${_nexusflux_nf_so}")
                set(_nexusflux_nf_so "${build_dir}/lib/libNexusForce.so")
            endif()
            if(EXISTS "${_nexusflux_nf_so}")
                list(APPEND _nexusflux_nf_props
                    IMPORTED_LOCATION_${_nexusflux_nf_CONFIG} "${_nexusflux_nf_so}")
                list(APPEND _nexusflux_nf_configs "${_nexusflux_nf_CONFIG}")
                if(NOT _nexusflux_nf_location)
                    set(_nexusflux_nf_location "${_nexusflux_nf_so}")
                endif()
                set(_nexusflux_nf_found TRUE)
            endif()
        endif()
    endforeach()

    if(NOT _nexusflux_nf_found)
        return()
    endif()

    if(NOT EXISTS "${NEXUSFORCE_ROOT}/include/NeForce")
        message(FATAL_ERROR
            "NexusFlux: found a NexusForce library in '${build_dir}', but its headers "
            "were not found at '${NEXUSFORCE_ROOT}/include/NeForce'.  Pass "
            "-DNEXUSFORCE_ROOT=<NexusForce checkout> as well.")
    endif()

    add_library(NexusForce::NexusForce SHARED IMPORTED GLOBAL)
    set_target_properties(NexusForce::NexusForce PROPERTIES
        ${_nexusflux_nf_props}
        IMPORTED_CONFIGURATIONS "${_nexusflux_nf_configs}"
        IMPORTED_LOCATION "${_nexusflux_nf_location}"
        INTERFACE_INCLUDE_DIRECTORIES "${NEXUSFORCE_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS "${NEXUSFLUX_NEXUSFORCE_PREBUILT_DEFINITIONS}")
    if(WIN32 AND _nexusflux_nf_implib)
        set_target_properties(NexusForce::NexusForce PROPERTIES
            IMPORTED_IMPLIB "${_nexusflux_nf_implib}")
    endif()
    if(UNIX)
        get_filename_component(_nexusflux_nf_libdir "${_nexusflux_nf_location}" DIRECTORY)
        set_target_properties(NexusForce::NexusForce PROPERTIES
            INTERFACE_LINK_OPTIONS "-Wl,-rpath,${_nexusflux_nf_libdir}")
    endif()
endfunction()

# add_subdirectory() the NexusForce checkout.  NexusForce's own options are
# normal variables here, so option() in its CMakeLists keeps these defaults
# while an explicit -DNEXUSFORCE_BUILD_<x>=ON on the command line still wins.
function(_nexusflux_nexusforce_add_source)
    if(NOT EXISTS "${NEXUSFORCE_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "NexusFlux: source mode needs a NexusForce checkout at '${NEXUSFORCE_ROOT}'.")
    endif()

    if(NOT DEFINED NEXUSFORCE_BUILD_TESTS)
        set(NEXUSFORCE_BUILD_TESTS OFF)
    endif()
    # NexusFlux only uses core/TUI/HTTP/regex.  Force the minimal dependency
    # profile unless the user explicitly disables NEXUSFLUX_NEXUSFORCE_MINIMAL.
    if(NEXUSFLUX_NEXUSFORCE_MINIMAL)
        foreach(_nexusflux_backend POSTGRESQL MYSQL SQLITE3 REDIS ZLIB LZ4 ZSTD ICU LOCALE)
            set(NEXUSFORCE_ENABLE_${_nexusflux_backend} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(NEXUSFORCE_USING_SQLCIPHER OFF CACHE BOOL "" FORCE)
        unset(_nexusflux_backend)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_POSTGRESQL)
        set(NEXUSFORCE_ENABLE_POSTGRESQL OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_MYSQL)
        set(NEXUSFORCE_ENABLE_MYSQL OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_SQLITE3)
        set(NEXUSFORCE_ENABLE_SQLITE3 OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_REDIS)
        set(NEXUSFORCE_ENABLE_REDIS OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_ZLIB)
        set(NEXUSFORCE_ENABLE_ZLIB OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_LZ4)
        set(NEXUSFORCE_ENABLE_LZ4 OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_ICU)
        set(NEXUSFORCE_ENABLE_ICU OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_LOCALE)
        set(NEXUSFORCE_ENABLE_LOCALE OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_USING_SQLCIPHER)
        set(NEXUSFORCE_USING_SQLCIPHER OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_ENABLE_ZSTD)
        set(NEXUSFORCE_ENABLE_ZSTD OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_BUILD_EXAMPLES)
        set(NEXUSFORCE_BUILD_EXAMPLES OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_BUILD_BENCHMARKS)
        set(NEXUSFORCE_BUILD_BENCHMARKS OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_BUILD_DOCS)
        set(NEXUSFORCE_BUILD_DOCS OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_FORMAT)
        set(NEXUSFORCE_FORMAT OFF)
    endif()
    if(NOT DEFINED NEXUSFORCE_CLANG_TIDY)
        set(NEXUSFORCE_CLANG_TIDY OFF)
    endif()

    message(STATUS "NexusFlux: compiling NexusForce from ${NEXUSFORCE_ROOT}")
    add_subdirectory("${NEXUSFORCE_ROOT}" "${NEXUSFLUX_NEXUSFORCE_SOURCE_BUILD_DIR}")

    if(NOT TARGET NexusForce::NexusForce)
        message(FATAL_ERROR
            "NexusFlux: NexusForce was added from '${NEXUSFORCE_ROOT}' but did not "
            "define the NexusForce::NexusForce target.")
    endif()
    set_target_properties(NexusForce PROPERTIES FOLDER "dependencies")
    if(UNIX)
        set_target_properties(NexusForce PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()

function(_nexusflux_nexusforce_find_package out_var)
    set(_nexusflux_nf_hints)
    if(NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR)
        list(APPEND _nexusflux_nf_hints "${NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR}")
        if(EXISTS "${NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR}/NexusForceConfig.cmake")
            set(NexusForce_DIR "${NEXUSFLUX_NEXUSFORCE_PACKAGE_DIR}"
                CACHE PATH "The directory containing a CMake configuration file for NexusForce." FORCE)
        endif()
    endif()
    list(APPEND _nexusflux_nf_hints "${NEXUSFLUX_DEPS_DIR}/NexusForce")

    if(NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "package")
        # Not QUIET: a missing NexusForce dependency should say which package it
        # needs instead of only producing the generic "not found" message below.
        find_package(NexusForce 1.0 CONFIG HINTS ${_nexusflux_nf_hints})
    else()
        find_package(NexusForce 1.0 CONFIG QUIET HINTS ${_nexusflux_nf_hints})
    endif()
    if(NexusForce_FOUND)
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Deploy the NexusForce runtime closure next to a Windows target.  This only
# applies to source mode (TARGET NexusForce is the in-tree library); imported
# targets rely on the DLLs shipped in their install tree.
function(nexusflux_deploy_nexusforce_runtime target)
    if(NOT WIN32 OR NOT TARGET NexusForce OR NOT TARGET ${target})
        return()
    endif()

    set(_nexusflux_nf_search_dirs
        "$<TARGET_FILE_DIR:${target}>"
        "$<TARGET_FILE_DIR:NexusForce>")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        list(APPEND _nexusflux_nf_search_dirs
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DEXECUTABLE=$<TARGET_FILE:${target}>"
                "-DDESTINATION=$<TARGET_FILE_DIR:${target}>"
                "-DSEARCH_DIRS=${_nexusflux_nf_search_dirs}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DeployNexusForceRuntime.cmake"
        VERBATIM
        COMMENT "Deploying NexusForce runtime DLLs for ${target}")
endfunction()

if(NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "source")
    _nexusflux_nexusforce_add_source()
    return()
endif()

if(NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "auto" OR NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "prebuilt")
    set(_nexusflux_nf_installed "${NEXUSFLUX_DEPS_DIR}/NexusForce")
    if(EXISTS "${_nexusflux_nf_installed}/include/NeForce")
        _nexusflux_nexusforce_import_prefix("${_nexusflux_nf_installed}")
        get_property(_nexusflux_nf_incomplete TARGET NexusForce::NexusForce PROPERTY _NEXUSFLUX_IMPORT_INCOMPLETE)
        if(_nexusflux_nf_incomplete)
            message(FATAL_ERROR
                "NexusFlux: '${_nexusflux_nf_installed}' contains NexusForce headers but no "
                "NexusForce.dll/libNexusForce.so.  Re-run scripts/install_nexusforce.py or use "
                "-DNEXUSFLUX_NEXUSFORCE_MODE=source.")
        endif()
        message(STATUS "NexusFlux: NexusForce from ${_nexusflux_nf_installed} (installed tree)")
    endif()

    if(NOT TARGET NexusForce::NexusForce AND NEXUSFLUX_NEXUSFORCE_BUILD_DIR)
        _nexusflux_nexusforce_import_build_tree("${NEXUSFLUX_NEXUSFORCE_BUILD_DIR}")
        if(TARGET NexusForce::NexusForce)
            message(STATUS "NexusFlux: NexusForce from ${NEXUSFLUX_NEXUSFORCE_BUILD_DIR} (build tree)")
        elseif(NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "prebuilt")
            message(FATAL_ERROR
                "NexusFlux: no NexusForce library found under "
                "'${NEXUSFLUX_NEXUSFORCE_BUILD_DIR}'.")
        endif()
    endif()
endif()

if(NOT TARGET NexusForce::NexusForce AND
   (NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "auto" OR NEXUSFLUX_NEXUSFORCE_MODE STREQUAL "package"))
    _nexusflux_nexusforce_find_package(_nexusflux_nf_package_found)
    if(_nexusflux_nf_package_found)
        message(STATUS "NexusFlux: NexusForce from find_package (${NexusForce_DIR})")
    endif()
endif()

if(NOT TARGET NexusForce::NexusForce)
    message(FATAL_ERROR
        "NexusFlux: NexusForce was not found (mode: ${NEXUSFLUX_NEXUSFORCE_MODE}).\n"
        "Choose one of:\n"
        "  -DNEXUSFLUX_NEXUSFORCE_MODE=source       build ${NEXUSFORCE_ROOT} as part of NexusFlux\n"
        "  -DNEXUSFLUX_NEXUSFORCE_MODE=package      use an installed NexusForce (NexusForce_DIR)\n"
        "  -DNEXUSFLUX_NEXUSFORCE_MODE=prebuilt     use .deps/NexusForce or NEXUSFLUX_NEXUSFORCE_BUILD_DIR\n"
        "Source mode also accepts -DNEXUSFLUX_NEXUSFORCE_VCPKG_ROOT=<vcpkg root>.")
endif()
