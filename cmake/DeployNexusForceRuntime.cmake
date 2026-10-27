# DeployNexusForceRuntime.cmake - build-time runtime DLL deployment.
#
# Invoked by nexusflux_deploy_nexusforce_runtime() through `cmake -P` after an
# executable is linked.  It resolves the executable's PE import closure (NexusForce
# plus its vcpkg dependencies) and copies the required DLLs next to the target.
#
# Required input variables:
#   EXECUTABLE  absolute path to the just-linked executable
#   DESTINATION directory that should receive the DLLs
#   SEARCH_DIRS semicolon-separated directories used to resolve imported DLLs

if(NOT DEFINED EXECUTABLE OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "DeployNexusForceRuntime.cmake: EXECUTABLE and DESTINATION are required")
endif()
if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "DeployNexusForceRuntime.cmake: '${EXECUTABLE}' does not exist")
endif()

# CMP0207 (CMake 4.0) normalizes paths before applying the exclusion regexes,
# which avoids warnings for System32 matches like C:\WINDOWS\system32/....
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXECUTABLE}"
    DIRECTORIES ${SEARCH_DIRS}
    RESOLVED_DEPENDENCIES_VAR _nexusflux_resolved
    UNRESOLVED_DEPENDENCIES_VAR _nexusflux_unresolved
    PRE_EXCLUDE_REGEXES
        "api-ms-"
        "ext-ms-"
        "^[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee]140"
        "^[Mm][Ss][Vv][Cc][Pp]140"
    POST_EXCLUDE_REGEXES
        ".*[Ss]ystem32.*"
)

foreach(_nexusflux_dependency IN LISTS _nexusflux_resolved)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_nexusflux_dependency}" "${DESTINATION}"
        RESULT_VARIABLE _nexusflux_copy_result)
    if(NOT _nexusflux_copy_result EQUAL 0)
        message(WARNING
            "NexusFlux: failed to copy runtime dependency '${_nexusflux_dependency}' "
            "to '${DESTINATION}'")
    endif()
endforeach()

if(_nexusflux_unresolved)
    message(WARNING
        "NexusFlux: unresolved runtime dependencies for '${EXECUTABLE}': "
        "${_nexusflux_unresolved}")
endif()
