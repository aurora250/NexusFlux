#!/usr/bin/env sh
# Syntax-check the Windows source branches with MinGW without linking NexusForce.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NEXUSFORCE_ROOT=${NEXUSFORCE_ROOT:-"$ROOT/../NexusForce-Linux"}
CXX=${MINGW_CXX:-x86_64-w64-mingw32-g++}

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "MinGW compiler not found: $CXX" >&2
    exit 1
fi
if [ ! -d "$NEXUSFORCE_ROOT/include/NeForce" ]; then
    echo "NexusForce headers not found under $NEXUSFORCE_ROOT" >&2
    exit 1
fi

SHIM=$(mktemp -d)
trap 'rm -rf "$SHIM"' EXIT INT TERM
printf '#pragma once\n#include <windows.h>\n' >"$SHIM/WinBase.h"

status=0
for source in "$ROOT"/src/*.cpp "$ROOT"/src/ui/*.cpp; do
    case "$source" in
        */collect_linux.cpp|*/connections_linux.cpp) continue ;;
    esac
    if ! "$CXX" -std=c++20 -fsyntax-only \
        -I"$SHIM" -I"$ROOT/include" -isystem "$NEXUSFORCE_ROOT/include" \
        -DNEFORCE_USING_SSO -DNEFORCE_USING_MEMORY_POOL -DNEFORCE_USING_MEMORY_POOL_OVERRIDE \
        -DPCRE2_CODE_UNIT_WIDTH=8 -DNEXUSFORCE_VERSION=\"1.0.1\" \
        -DNEXUSFORCE_VERSION_MAJOR=1 -DNEXUSFORCE_VERSION_MINOR=0 -DNEXUSFORCE_VERSION_PATCH=1 \
        "$source"; then
        echo "FAILED: $source" >&2
        status=1
    fi
done
exit "$status"
