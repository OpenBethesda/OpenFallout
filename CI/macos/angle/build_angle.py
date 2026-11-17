#!/usr/bin/env python3
"""Builds the project's ANGLE at the version recorded in CI/macos/angle/angle.json and assembles the release asset
(docs/openfallout/angle.md). Run on a Mac, once per ANGLE version; CI never runs it.

    CI/macos/angle/build_angle.py WORK [--checkout DIR] [--out-name NAME] [--jobs N] [--record]

WORK        a folder for depot_tools, the ANGLE checkout (about 13 GB) and the results: WORK/asset (the unpacked asset)
            and WORK/<asset name> (the archive to publish).
--checkout  an existing ANGLE checkout to use instead of fetching one into WORK/angle. It must be at the recorded
            commit and already synced (gclient sync); it is not synced again.
--out-name  the build folder inside the checkout (out/<NAME>, default OpenFallout).
--record    write the archive's SHA-256 and the digests of the libraries and the notices into angle.json.

Steps: fetch depot_tools and ANGLE at their recorded commits (unless --checkout), check the checkout's commit, generate
with ANGLE's own buildtools/mac/gn (the gn wrapper of depot_tools refuses to run with DEPOT_TOOLS_UPDATE=0) and build
libEGL and libGLESv2 with third_party/ninja/ninja, find the third-party code linked into the two libraries (from the
ninja graph: the objects and archives each library links, and the source file of each object) and assemble the
notices from those components' licence files, then the asset: lib/ (the linker's debug map stripped, as it names
the build Mac's folders and the times of the object files; install names @rpath/libEGL.dylib and
@rpath/libGLESv2.dylib; a UUID made from the library's own bytes; ad hoc signed), include/{EGL,GLES2,GLES3,KHR}, NOTICES, BUILDINFO.json, SHA256SUMS. The
libraries are checked: arm64 only, built for macOS 26.0, no link to OpenGL.framework. Two builds of the same commit
with the same arguments give the same asset byte for byte, wherever and whenever they were linked.
"""
import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
sys.path.insert(0, str(ROOT / "CI"))
import angle_asset  # noqa: E402
import engine_release  # noqa: E402

LIBRARIES = ("libEGL.dylib", "libGLESv2.dylib")
HEADER_FOLDERS = ("EGL", "GLES2", "GLES3", "KHR")
MINIMUM_MACOS = "26.0"
LICENCE_NAMES = ("LICENSE", "LICENSE.txt", "LICENSE.TXT", "LICENSE.md", "LICENCE", "COPYING", "NOTICE")
QUERY_BATCH = 400


class BuildError(Exception):
    pass


def run(command, cwd=None, env=None, capture=False):
    command = [str(part) for part in command]
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=capture)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip() if capture else ""
        raise BuildError(f"{command[0]} exited with {result.returncode}" + (f": {detail}" if detail else ""))
    return result


def output(command):
    return subprocess.run([str(part) for part in command], capture_output=True, text=True)


# ---------------------------------------------------------------------------------------------------------- sources

def depot_tools_environment(depot_tools):
    environment = dict(os.environ)
    environment.update(PATH=f"{depot_tools}{os.pathsep}{environment.get('PATH', '')}", DEPOT_TOOLS_UPDATE="0",
                       DEPOT_TOOLS_METRICS="0")
    return environment


GCLIENT = """solutions = [
  { "name": ".",
    "url": "%s",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
"""


def fetch_commands(version, work):
    """(folder, command) steps that fetch depot_tools and ANGLE at the recorded commits; gclient then fetches ANGLE's
    dependencies at the revisions its DEPS file names."""
    depot_tools, angle = work / "depot_tools", work / "angle"
    return [
        (work, ["git", "clone", "--no-checkout", version["depot_tools"]["repository"], str(depot_tools)]),
        (work, ["git", "-C", str(depot_tools), "checkout", "--detach", version["depot_tools"]["commit"]]),
        (work, ["git", "init", "-q", str(angle)]),
        (work, ["git", "-C", str(angle), "fetch", "--depth", "1", version["angle"]["repository"],
                version["angle"]["commit"]]),
        (work, ["git", "-C", str(angle), "checkout", "--detach", "FETCH_HEAD"]),
        (angle, ["gclient", "sync", "--no-history", "-j", "4"]),
    ]


def fetch_sources(version, work):
    angle = work / "angle"
    if angle.exists():
        raise BuildError(f"{angle} exists; pass it as --checkout or remove it")
    environment = depot_tools_environment(work / "depot_tools")
    for folder, command in fetch_commands(version, work):
        if command[0] == "gclient":
            (angle / ".gclient").write_text(GCLIENT % version["angle"]["repository"])
        run(command, cwd=folder, env=environment)
    return angle


def check_checkout(checkout, version):
    result = output(["git", "-C", checkout, "rev-parse", "HEAD"])
    head = result.stdout.strip()
    if result.returncode != 0 or head != version["angle"]["commit"]:
        raise BuildError(f"{checkout} is at {head or 'no commit'}, not the recorded {version['angle']['commit']}")
    # The asset names the recorded commit: sources that differ from it are never built into it. Untracked files (the
    # build output, the dependencies gclient fetched) are not changes to ANGLE's own sources.
    status = output(["git", "-C", checkout, "status", "--porcelain", "--untracked-files=no"])
    if status.returncode != 0:
        raise BuildError(f"git status failed in {checkout} ({status.stderr.strip()}); its sources cannot be trusted")
    changed = status.stdout.splitlines()
    if changed:
        names = ", ".join(line[3:] for line in changed[:5])  # a line is two status letters, a space, the path
        raise BuildError(f"{checkout} has changed files ({names}); the asset is built only from the recorded commit")
    for tool in ("buildtools/mac/gn", "third_party/ninja/ninja"):
        if not (checkout / tool).is_file():
            raise BuildError(f"{checkout} has no {tool}; is it synced (gclient sync)?")


# ------------------------------------------------------------------------------------------------------------ build

def build_commands(version, checkout, out_name, jobs):
    out = checkout / "out" / out_name
    gn = [str(checkout / "buildtools/mac/gn"), "gen", str(out), "--args=" + " ".join(version["gn_args"])]
    ninja = [str(checkout / "third_party/ninja/ninja"), "-C", str(out), "-j", str(jobs), *version["targets"]]
    return gn, ninja


# ---------------------------------------------------------------------------------------------------------- notices

def ninja_query(checkout, out):
    """A query function over the real build graph: {target: (rule, explicit inputs)}."""
    def query(targets):
        edges = {}
        for start in range(0, len(targets), QUERY_BATCH):
            batch = targets[start:start + QUERY_BATCH]
            result = output([checkout / "third_party/ninja/ninja", "-C", out, "-t", "query", *batch])
            if result.returncode != 0:
                raise BuildError(f"ninja -t query failed: {result.stderr.strip() or result.stdout.strip()}")
            edges.update(parse_query(result.stdout))
        return edges
    return query


def parse_query(text):
    edges, target, rule, inputs, section = {}, None, None, [], None
    for line in text.splitlines():
        if line and not line.startswith(" "):
            if target is not None:
                edges[target] = (rule, inputs)
            target, rule, inputs, section = line.rstrip(":"), None, [], None
        elif line.startswith("  input:"):
            rule, section = line.split(":", 1)[1].strip(), "input"
        elif line.startswith("  outputs:") or line.startswith("  validations:"):
            section = None
        elif section == "input" and line.startswith("    ") and not line.strip().startswith("|"):
            inputs.append(line.strip())
    if target is not None:
        edges[target] = (rule, inputs)
    return edges


def linked_sources(query, targets):
    """The source files of every object the targets link, directly or from the static archives they link."""
    edges = query(list(targets))
    objects, archives = [], []
    for target in targets:
        for item in edges[target][1]:
            (objects if item.endswith(".o") else archives if item.endswith(".a") else []).append(item)
    if archives:
        for _, inputs in query(archives).values():
            objects += [item for item in inputs if item.endswith(".o")]
    objects = sorted(set(objects))
    sources = {inputs[0] for _, inputs in query(objects).values() if inputs}
    return sorted(sources)


def component_of(source):
    """The folder whose licence covers a source file: the innermost third_party/<name> above it, or "" for ANGLE.
    Paths are relative to the build folder (out/<name>): ../../ is the checkout, gen/ holds files generated from the
    folder of the same name."""
    path = source[len("../../"):] if source.startswith("../../") else source
    if path.startswith("gen/"):
        path = path[len("gen/"):]
    parts = path.split("/")
    for index in range(len(parts) - 3, -1, -1):
        if parts[index] == "third_party":
            return "/".join(parts[:index + 2])
    return ""


@dataclass
class Component:
    path: str
    name: str
    licence: str
    files: list = field(default_factory=list)
    sources: int = 0


def readme_fields(folder):
    readme = folder / "README.chromium"
    fields = {}
    if readme.is_file():
        for line in readme.read_text(encoding="utf-8", errors="replace").splitlines():
            key, _, value = line.partition(":")
            if value and key.strip() in ("Name", "License", "License File") and key.strip() not in fields:
                fields[key.strip()] = value.strip()
    return fields


def licence_files(checkout, folder, fields):
    named = [name.strip().lstrip("/") for name in fields.get("License File", "").split(",") if name.strip()]
    found = [folder / name for name in named if (folder / name).is_file()]
    if found:
        return found
    for base in (folder, folder / "src"):
        for name in LICENCE_NAMES:
            if (base / name).is_file():
                return [base / name]
    return []


def components(checkout, sources):
    counts = {}
    for source in sources:
        counts[component_of(source)] = counts.get(component_of(source), 0) + 1
    found, missing = [], []
    for path in sorted(counts):
        folder = checkout / path if path else checkout
        fields = readme_fields(folder) if path else {}
        files = licence_files(checkout, folder, fields)
        if not files:
            missing.append(path or "ANGLE (the checkout's root)")
            continue
        name = "ANGLE" if not path else fields.get("Name", path.rsplit("/", 1)[-1])
        licence = "BSD-3-Clause" if not path else fields.get("License", "see the text below")
        found.append(Component(path, name, licence, files, counts[path]))
    if missing:
        raise BuildError(f"no licence file found for linked code in: {', '.join(missing)}")
    return found


RULE = "=" * 100


def notices_text(found, commit, branch):
    lines = [
        "Notices for libEGL.dylib and libGLESv2.dylib",
        "",
        f"These two libraries are ANGLE, built by the OpenFallout project from commit {commit} (branch {branch}) of",
        "https://chromium.googlesource.com/angle/angle with the Metal backend only. They hold code from the projects",
        "below. The list was made from the build itself: the objects and archives linked into the two libraries and",
        "the source file of each object. Each project's licence follows. The headers in include/ carry their own",
        "licence at the top of each file.",
        "",
    ]
    for component in found:
        where = f" ({component.path})" if component.path else ""
        lines += [RULE, f"{component.name}{where}, licence: {component.licence}", RULE, ""]
        for path in component.files:
            lines.append(path.read_text(encoding="utf-8", errors="replace").rstrip() + "\n")
    return "\n".join(lines)


# --------------------------------------------------------------------------------------------------------- assemble

def toolchain(checkout):
    clang = checkout / "third_party/llvm-build/Release+Asserts/bin/clang"
    first = lambda command: (output(command).stdout.strip().splitlines() or ["unknown"])[0]
    return {"clang": first([clang, "--version"]) if clang.is_file() else "not found",
            "sdk": first(["xcrun", "--sdk", "macosx", "--show-sdk-version"]),
            "xcode": first(["xcodebuild", "-version"])}


LC_UUID = 0x1B


def set_content_uuid(library):
    """Replaces the UUID the linker gave the library (a hash that also covered the debug map) with one made from the
    library's own bytes: the SHA-256 of the file without its signature and with the UUID zeroed, as a version 5 UUID.
    The same code then always has the same UUID."""
    data = bytearray(library.read_bytes())
    count, offset = struct.unpack_from("<I", data, 16)[0], 32
    for _ in range(count):
        command, size = struct.unpack_from("<II", data, offset)
        if command == LC_UUID:
            data[offset + 8:offset + 24] = bytes(16)
            uuid = bytearray(hashlib.sha256(angle_asset.code_digest(bytes(data)).encode()).digest()[:16])
            uuid[6] = (uuid[6] & 0x0F) | 0x50
            uuid[8] = (uuid[8] & 0x3F) | 0x80
            data[offset + 8:offset + 24] = uuid
            library.write_bytes(data)
            return
        offset += size
    raise BuildError(f"{library.name} has no LC_UUID")


def assemble(checkout, out, asset, version, found):
    if asset.exists():
        raise BuildError(f"{asset} exists")
    (asset / "lib").mkdir(parents=True)
    for name in LIBRARIES:
        library = asset / "lib" / name
        shutil.copyfile(out / name, library)
        library.chmod(0o755)
        # The linker's debug map names every object file by its absolute path on the build Mac, with its time: it
        # goes, and with it the only bytes that differ between two builds of the same code. Exported symbols stay.
        run(["strip", "-S", library], capture=True)
        run(["install_name_tool", "-id", f"@rpath/{name}", library], capture=True)
        # a fixed identifier keeps the ad hoc signature, and so the asset, the same from build to build
        sign = ["codesign", "--force", "--sign", "-", "--identifier", name.removesuffix(".dylib"), library]
        run(sign, capture=True)
        set_content_uuid(library)
        run(sign, capture=True)
    for folder in HEADER_FOLDERS:
        target = asset / "include" / folder
        target.mkdir(parents=True)
        for header in sorted((checkout / "include" / folder).glob("*.h")):
            shutil.copyfile(header, target / header.name)
    (asset / angle_asset.NOTICES).write_text(notices_text(found, version["angle"]["commit"],
                                                          version["angle"]["branch"]), encoding="utf-8")
    info = {"repository": version["angle"]["repository"], "branch": version["angle"]["branch"],
            "commit": version["angle"]["commit"], "gn_args": version["gn_args"], "built_with": toolchain(checkout),
            "components": [component.path or "." for component in found]}
    (asset / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    angle_asset.write_sums(asset)


def check_libraries(folder):
    problems = []
    for name in LIBRARIES:
        library = folder / name
        archs = output(["lipo", "-archs", library]).stdout.split()
        if archs != ["arm64"]:
            problems.append(f"{name}: architecture {' '.join(archs) or 'unknown'}, not arm64 only")
        built_for = engine_release.parse_vtool(output(["vtool", "-show-build", library]).stdout)
        if built_for is None or not engine_release.same_macos(built_for, MINIMUM_MACOS):
            problems.append(f"{name}: built for macOS {built_for}, not {MINIMUM_MACOS}")
        links = output(["otool", "-L", library]).stdout
        if "/OpenGL.framework/" in links:
            problems.append(f"{name} links OpenGL.framework")
        installed = output(["otool", "-D", library]).stdout.strip().splitlines()
        if not installed or installed[-1] != f"@rpath/{name}":
            problems.append(f"{name}: install name {installed[-1] if installed else 'none'}, not @rpath/{name}")
        if output(["codesign", "--verify", "--strict", library]).returncode != 0:
            problems.append(f"{name}: not validly signed")
    if problems:
        raise BuildError("; ".join(problems))
    return (f"{', '.join(LIBRARIES)}: arm64 only, built for macOS {MINIMUM_MACOS}, install names @rpath/<name>, "
            f"no link to OpenGL.framework, ad hoc signed")


def record(version_file, archive, asset):
    version = json.loads(Path(version_file).read_text(encoding="utf-8"))
    version["release"]["sha256"] = angle_asset.sha256_file(archive)
    version["files"] = angle_asset.recorded_files(asset)
    Path(version_file).write_text(json.dumps(version, indent=2) + "\n", encoding="utf-8")


# ------------------------------------------------------------------------------------------------------------- main

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("work", type=Path)
    parser.add_argument("--checkout", type=Path)
    parser.add_argument("--out-name", default="OpenFallout")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--record", action="store_true")
    options = parser.parse_args(argv)
    version = angle_asset.load_version()
    work = options.work.absolute()
    try:
        work.mkdir(parents=True, exist_ok=True)
        checkout = options.checkout.absolute() if options.checkout else fetch_sources(version, work)
        check_checkout(checkout, version)
        gn, ninja = build_commands(version, checkout, options.out_name, options.jobs)
        run(gn, cwd=checkout)
        run(ninja, cwd=checkout)
        out = checkout / "out" / options.out_name
        found = components(checkout, linked_sources(ninja_query(checkout, out), list(LIBRARIES)))
        for component in found:
            print(f"linked: {component.path or 'ANGLE'} ({component.licence}, {component.sources} source files)")
        asset = work / "asset"
        if asset.exists():
            shutil.rmtree(asset)
        assemble(checkout, out, asset, version, found)
        print(check_libraries(asset / "lib"))
        archive = work / version["release"]["asset"]
        angle_asset.write_archive(asset, archive)
        print(f"asset: {archive} ({archive.stat().st_size} bytes), SHA-256 {angle_asset.sha256_file(archive)}")
        print(json.dumps(angle_asset.recorded_files(asset), indent=2))
        if options.record:
            record(angle_asset.VERSION_FILE, archive, asset)
            print(f"recorded in {angle_asset.VERSION_FILE}")
    except (BuildError, angle_asset.AssetError, OSError) as error:
        print(f"FAIL angle build: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
