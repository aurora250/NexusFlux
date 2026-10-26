#!/usr/bin/env bash
# Build NexusForce and install a self-contained copy into NexusFlux/.deps/NexusForce.
# NexusFlux's CMakeLists.txt prefers this tree over ../NexusForce-Linux and installed packages.
#
# Usage:
#   scripts/install_nexusforce.sh [Release|Debug]
#
# Environment:
#   NEXUSFORCE_ROOT       NexusForce source tree (default: <NexusFlux>/../NexusForce-Linux)
#   NEXUSFORCE_BUILD_DIR  NexusForce build directory (default: <root>/cmake-build-release-clang-x64)
#   NEXUSFLUX_DEPS_DIR    Dependency root (default: <NexusFlux>/.deps)
#   JOBS                  Parallel build jobs (default: nproc)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
flux_root="$(cd "${script_dir}/.." && pwd)"
nexusforce_root="${NEXUSFORCE_ROOT:-${flux_root}/../NexusForce-Linux}"
config="${1:-Release}"

if [[ ! -f "${nexusforce_root}/CMakeLists.txt" ]]; then
    echo "error: NexusForce source tree not found at ${nexusforce_root}" >&2
    echo "       set NEXUSFORCE_ROOT to the NexusForce checkout" >&2
    exit 1
fi

if [[ -z "${NEXUSFORCE_BUILD_DIR:-}" ]]; then
    if [[ -f "${nexusforce_root}/cmake-build-release-clang-x64/CMakeCache.txt" ]]; then
        build_dir="${nexusforce_root}/cmake-build-release-clang-x64"
    elif [[ -f "${nexusforce_root}/build/CMakeCache.txt" ]]; then
        build_dir="${nexusforce_root}/build"
    else
        echo "error: no configured NexusForce build directory found" >&2
        echo "       configure one first or set NEXUSFORCE_BUILD_DIR" >&2
        exit 1
    fi
else
    build_dir="${NEXUSFORCE_BUILD_DIR}"
fi

deps_dir="${NEXUSFLUX_DEPS_DIR:-${flux_root}/.deps}"
prefix="${deps_dir}/NexusForce"
jobs="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

echo "NexusForce : ${nexusforce_root}"
echo "Build dir  : ${build_dir}"
echo "Install to : ${prefix} (${config})"

build_targets=(NexusForce)
if cmake --build "${build_dir}" --target help 2>/dev/null | grep -q "NexusForceMemoryPoolOverrideStatic"; then
    build_targets+=(NexusForceMemoryPoolOverrideStatic)
fi
cmake --build "${build_dir}" --target "${build_targets[@]}" --config "${config}" --parallel "${jobs}"

rm -rf "${prefix}"
cmake --install "${build_dir}" --prefix "${prefix}" --config "${config}" > /dev/null

if [[ ! -e "${prefix}/lib/libNexusForce.so" && ! -e "${prefix}/bin/NexusForce.dll" ]]; then
    echo "error: install did not produce a NexusForce shared library under ${prefix}" >&2
    exit 1
fi

echo "done: NexusFlux will now resolve NexusForce from ${prefix}"
echo "re-run the NexusFlux CMake configure step to pick it up"
