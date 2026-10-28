#!/usr/bin/env python3
# Build NexusForce and install a self-contained copy into NexusFlux/.deps/NexusForce.
# NexusFlux's CMakeLists.txt (NEXUSFLUX_NEXUSFORCE_MODE=auto) prefers this tree
# over an installed NexusForce package.
#
# Alternatively configure NexusFlux with -DNEXUSFLUX_NEXUSFORCE_MODE=source to
# compile NexusForce in-tree without installing it first.
#
# Works on Linux, macOS and Windows. All options are CLI parameters (no env vars).
#
# Usage:
#   python scripts/install_nexusforce.py [Release|Debug] [options]
#
# Options:
#   --nexusforce-root PATH   NexusForce source tree
#                            (default: <NexusFlux>/../NexusForce-Linux)
#   --build-dir PATH         NexusForce build directory
#                            (default: auto-detect <root>/cmake-build-release-clang-x64
#                                      or <root>/build; falls back to <root>/build
#                                      when --clean-build is given)
#   --deps-dir PATH          Dependency root (default: <NexusFlux>/.deps)
#   -j, --jobs N             Parallel build jobs (default: number of CPU cores)
#   --clean-build            Delete the build dir and re-configure it before building.
#                            Useful when CMakeCache.txt holds a stale Visual Studio path.
#   --generator NAME         CMake generator used with --clean-build
#                            (default: "Visual Studio 17 2022" on Windows, "Ninja" elsewhere)
#   -A, --arch ARCH          CMake -A value used with --clean-build
#                            (default: "x64" on Windows, ignored elsewhere)
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def _fmt(cmd: list[str]) -> str:
    return " ".join(f'"{c}"' if " " in c else c for c in cmd)


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    print("+", _fmt(cmd))
    return subprocess.run(cmd, check=True, **kwargs)


def parse_args(argv: list[str]) -> argparse.Namespace:
    is_windows = platform.system() == "Windows"
    p = argparse.ArgumentParser(
        description="Build NexusForce and install it into NexusFlux/.deps/NexusForce.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("config", nargs="?", default="Release", choices=["Release", "Debug"],
                   help="Build configuration")
    p.add_argument("--nexusforce-root", default=None,
                   help="NexusForce source tree")
    p.add_argument("--build-dir", default=None,
                   help="NexusForce build directory")
    p.add_argument("--deps-dir", default=None,
                   help="Dependency root that contains NexusForce/")
    p.add_argument("-j", "--jobs", default=None,
                   help="Parallel build jobs")
    p.add_argument("--clean-build", action="store_true",
                   help="Delete the build dir and re-configure it before building")
    p.add_argument("--generator", default="Visual Studio 17 2022" if is_windows else "Ninja",
                   help="CMake generator (only used with --clean-build)")
    p.add_argument("-A", "--arch", default="x64" if is_windows else None,
                   help="CMake architecture (only used with --clean-build, Windows)")
    return p.parse_args(argv)


def find_shared_lib(prefix: Path) -> bool:
    if not prefix.exists():
        return False
    names = {"libNexusForce.so", "libNexusForce.dylib", "NexusForce.dll"}
    for p in prefix.rglob("*"):
        if p.is_file() and (p.name in names or p.name.startswith("libNexusForce.so.")):
            return True
    return False


def resolve_build_dir(args: argparse.Namespace, nexusforce_root: Path) -> Path | None:
    if args.build_dir:
        return Path(args.build_dir)
    candidates = [
        nexusforce_root / "cmake-build-release-clang-x64",
        nexusforce_root / "build",
        ]
    for c in candidates:
        if (c / "CMakeCache.txt").is_file():
            return c
    if args.clean_build:
        return nexusforce_root / "build"
    return None


def reconfigure(nexusforce_root: Path, build_dir: Path,
                generator: str, arch: str | None) -> None:
    cmd = ["cmake", "-S", str(nexusforce_root), "-B", str(build_dir), "-G", generator]
    if arch:
        cmd += ["-A", arch]
    # NexusFlux only needs core/TUI/HTTP/regex.  Keep the dependency install
    # minimal and disable the unused NexusForce backends.
    manifest = Path(__file__).resolve().parent.parent / "cmake" / "nexusforce-manifest"
    cmd += [
        f"-DVCPKG_MANIFEST_DIR={manifest}",
        "-DNEXUSFORCE_ENABLE_POSTGRESQL=OFF",
        "-DNEXUSFORCE_ENABLE_MYSQL=OFF",
        "-DNEXUSFORCE_ENABLE_SQLITE3=OFF",
        "-DNEXUSFORCE_ENABLE_REDIS=OFF",
        "-DNEXUSFORCE_ENABLE_ZLIB=OFF",
        "-DNEXUSFORCE_ENABLE_LZ4=OFF",
        "-DNEXUSFORCE_ENABLE_ZSTD=OFF",
        "-DNEXUSFORCE_ENABLE_ICU=OFF",
        "-DNEXUSFORCE_ENABLE_LOCALE=OFF",
        "-DNEXUSFORCE_USING_SQLCIPHER=OFF",
        "-DNEXUSFORCE_BUILD_TESTS=OFF",
        "-DNEXUSFORCE_BUILD_EXAMPLES=OFF",
        "-DNEXUSFORCE_BUILD_BENCHMARKS=OFF",
    ]
    run(cmd)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    script_dir = Path(__file__).resolve().parent
    flux_root = script_dir.parent

    nexusforce_root = Path(args.nexusforce_root).resolve() if args.nexusforce_root \
        else (flux_root / ".." / "NexusForce-Linux").resolve()

    if not (nexusforce_root / "CMakeLists.txt").is_file():
        print(f"error: NexusForce source tree not found at {nexusforce_root}", file=sys.stderr)
        print("       pass --nexusforce-root to point at the NexusForce checkout",
              file=sys.stderr)
        return 1

    build_dir = resolve_build_dir(args, nexusforce_root)
    if build_dir is None:
        print("error: no configured NexusForce build directory found", file=sys.stderr)
        print("       configure one first, pass --build-dir, or use --clean-build",
              file=sys.stderr)
        return 1

    deps_dir = Path(args.deps_dir) if args.deps_dir else (flux_root / ".deps")
    prefix = deps_dir / "NexusForce"
    jobs = str(args.jobs) if args.jobs else str(os.cpu_count() or 2)

    print(f"NexusForce : {nexusforce_root}")
    print(f"Build dir  : {build_dir}")
    print(f"Install to : {prefix} ({args.config})")

    try:
        # --- Optional: wipe stale cache and reconfigure ----------------------
        if args.clean_build:
            if build_dir.exists():
                print(f"removing build dir: {build_dir}")
                shutil.rmtree(build_dir)
            reconfigure(nexusforce_root, build_dir, args.generator, args.arch)
        elif not (build_dir / "CMakeCache.txt").is_file():
            print(f"error: {build_dir} is not configured "
                  f"(no CMakeCache.txt); run --clean-build or configure it manually",
                  file=sys.stderr)
            return 1

        # --- Determine optional build targets --------------------------------
        build_targets = ["NexusForce"]
        help_proc = subprocess.run(
            ["cmake", "--build", str(build_dir), "--target", "help"],
            capture_output=True, text=True, check=False,
        )
        if "NexusForceMemoryPoolOverrideStatic" in (help_proc.stdout + help_proc.stderr):
            build_targets.append("NexusForceMemoryPoolOverrideStatic")

        # --- Build -----------------------------------------------------------
        run(["cmake", "--build", str(build_dir),
             "--target", *build_targets,
             "--config", args.config,
             "--parallel", jobs])

        # --- Install ---------------------------------------------------------
        if prefix.exists():
            shutil.rmtree(prefix)
        run(["cmake", "--install", str(build_dir),
             "--prefix", str(prefix), "--config", args.config],
            stdout=subprocess.DEVNULL)

    except FileNotFoundError as e:
        print(f"error: {e} (is cmake on PATH?)", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print(f"error: command failed with exit code {e.returncode}", file=sys.stderr)
        return e.returncode or 1

    if not find_shared_lib(prefix):
        print(f"error: install did not produce a NexusForce shared library under {prefix}",
              file=sys.stderr)
        return 1

    print(f"done: NexusFlux will now resolve NexusForce from {prefix}")
    print("re-run the NexusFlux CMake configure step to pick it up")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))