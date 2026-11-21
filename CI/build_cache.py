#!/usr/bin/env python3
"""Fingerprint build environments and preserve unchanged checkout timestamps."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


STATE_FILE = ".ci-source-state.json"


def tracked_files(root):
    output = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
    return [Path(os.fsdecode(name)) for name in output.split(b"\0") if name]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Tracked files outside CMakeLists.txt and *.cmake whose content decides whether an old build folder may be reused.
# The Configure step re-runs CMake and `restore` re-dates changed sources, so what matters is what CMake and make
# cannot correct on their own: how the tools and dependencies were produced, and how the folder is restored.
# Every other file under CI/ (checkers, report and release scripts, the Metal/OSG/ANGLE builders, tests,
# the ANGLE probe) is never run by the Configure or Build steps of a cached tree, so it cannot change the folder.
KEY_FILES = frozenset((
    # Configure: the CMake options, compiler, generator and deployment target of every cached tree.
    "CI/before_script.macos.sh",
    # Build: the make invocation.
    "CI/macos/build.sh",
    # Sourced by the configure script (Qt version, the pinned dependency set).
    "CI/macos/deps_versions.sh",
    # Produce the Qt/openmw-deps trees (the dependency cache key hashes the same files), including the LuaJIT build and
    # its patch, which the dependency manifest passed to `key` does not describe.
    "CI/before_install.macos.sh",
    "CI/macos/fetch_deps_manifest.sh",
    "CI/macos/build_luajit_hrt.sh",
    "CI/macos/luajit-osx-hrt-void-return.patch",
    # The ANGLE libraries a bundle-angle run configures against.
    "CI/macos/angle/angle.json",
    # The timestamp reconciliation (restore/snapshot) and this fingerprint: a change here changes what a restored
    # folder means.
    "CI/build_cache.py",
    # The job that configures, builds and restores (its environment and the arguments it passes).
    ".github/workflows/macos.yml",
))


def is_key_rule(relative):
    return (relative.name == "CMakeLists.txt" or relative.suffix == ".cmake"
            or relative.as_posix() in KEY_FILES)


def build_key(root, compiler, dependency_files, mode):
    # A build tree contains absolute paths, generated rules and toolchain state.
    # Never fall back to a cache with a different environment fingerprint.
    identity = {
        "root": str(root.resolve()),
        "mode": mode,
        "image": os.environ.get("ImageVersion", ""),
        "compiler": str(Path(shutil.which(compiler) or compiler).resolve()),
        "cmake": subprocess.check_output(["cmake", "--version"], text=True),
        "environment": {key: os.environ.get(key, "") for key in (
            "SDKROOT", "DEVELOPER_DIR",
            "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS",
        )},
        "rules": {},
        "dependencies": [digest(Path(path)) for path in dependency_files],
    }
    identity["compiler_version"] = subprocess.check_output(
        [compiler, "--version"], text=True
    )
    if shutil.which("xcrun"):
        identity["sdk"] = subprocess.check_output(
            ["xcrun", "--show-sdk-path"], text=True
        )
    for relative in tracked_files(root):
        if is_key_rule(relative):
            path = root / relative
            if path.is_file() and not path.is_symlink():
                identity["rules"][relative.as_posix()] = digest(path)
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()


def snapshot(root, build):
    files = {}
    for relative in tracked_files(root):
        path = root / relative
        if path.is_file() and not path.is_symlink():
            files[relative.as_posix()] = {
                "sha256": digest(path), "mtime_ns": path.stat().st_mtime_ns,
            }
    build.mkdir(parents=True, exist_ok=True)
    (build / STATE_FILE).write_text(json.dumps({
        "version": 1, "root": str(root.resolve()), "files": files,
    }, sort_keys=True))
    print(f"Recorded timestamps for {len(files)} tracked files")


def restore(root, build):
    if not build.exists():
        print("No incremental build cache restored; starting a fresh build")
        return
    try:
        state = json.loads((build / STATE_FILE).read_text())
        # This snapshot binds source timestamps to a checkout root, not to the
        # compiler or configure options; the caller's cache key owns that check.
        if state["version"] != 1 or state["root"] != str(root.resolve()):
            raise ValueError("incompatible source snapshot")
        files = state["files"]
        if not isinstance(files, dict) or any(
                not isinstance(value, dict)
                or not isinstance(value.get("sha256"), str)
                or not isinstance(value.get("mtime_ns"), int)
                for value in files.values()):
            raise ValueError("invalid source snapshot")
    except (OSError, ValueError, KeyError, TypeError):
        # Without trustworthy metadata, none of the restored build tree is safe
        # to reuse. The next configure/build recreates it from scratch.
        shutil.rmtree(build)
        print("Discarded incomplete/incompatible build cache")
        return
    current_files = [relative for relative in tracked_files(root)
                     if (root / relative).is_file() and not (root / relative).is_symlink()]
    # CMake copy-only resources can survive removal of their source. A fresh
    # tree prevents a deleted asset from being shipped from an older snapshot.
    if files.keys() - {relative.as_posix() for relative in current_files}:
        shutil.rmtree(build)
        print("Discarded build cache because tracked files were removed")
        return
    unchanged = changed = 0
    # New/changed files must be newer than all restored build outputs, even if
    # a checkout or filesystem supplied an older timestamp.
    newest_output = max((path.stat().st_mtime_ns for path in build.rglob("*")
                         if path.is_file()), default=0)
    changed_time = max(time.time_ns(), newest_output + 1_000_000_000)
    for relative in current_files:
        path = root / relative
        previous = files.get(relative.as_posix(), {})
        if previous.get("sha256") == digest(path):
            modified = previous["mtime_ns"]
            unchanged += 1
        else:
            modified = changed_time
            changed += 1
        os.utime(path, ns=(path.stat().st_atime_ns, modified))
    print(f"Restored timestamps for {unchanged} unchanged files; {changed} require rebuilding")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("key", "snapshot", "restore"))
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--dependency-file", action="append", default=[])
    parser.add_argument("--mode", default="")
    args = parser.parse_args()
    root = Path.cwd().resolve()
    requested_build = Path(args.build_dir)
    build = requested_build.resolve()
    if build == root or root not in build.parents or requested_build.is_symlink():
        parser.error("build directory must be a directory beneath the checkout")
    if args.command == "key":
        print(build_key(root, args.compiler, args.dependency_file, args.mode))
    elif args.command == "snapshot":
        snapshot(root, build)
    else:
        restore(root, build)


if __name__ == "__main__":
    main()
