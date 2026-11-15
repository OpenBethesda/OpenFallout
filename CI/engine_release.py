#!/usr/bin/env python3
"""The statement of an engine release: what CI writes from the built disk image, what the shell reads, what the release
gate (CI/harness_release.py) compares with the uploaded assets. See docs/openfallout/engine-release.md.

    CI/engine_release.py write  DMG --tag TAG --commit SHA --out FILE   write the statement from the package
    CI/engine_release.py verify DMG FILE [--tag TAG] [--commit SHA]     check a statement against the package
    CI/engine_release.py notes  FILE [--played-on-mac MAC --played-on-macos VERSION] [--append NOTES]
                                                                         the release-notes lines the gate asks for

`write` and `verify` mount the disk image (read-only) and fail, naming each difference, when the statement, the property
lists of the app and its nested engine app, the version in CMakeLists.txt, the tag and the Mach-O files of the app do
not agree. `write` writes nothing in that case.
"""

import argparse
import contextlib
import hashlib
import json
import plistlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE_LISTS = ROOT / "CMakeLists.txt"

STATEMENT_NAME = "openfallout-engine-release.json"
SCHEMA = 1
FIELDS = ("schema", "version", "tag", "source_commit", "disk_image", "sha256", "minimum_macos", "architecture")
ARCHITECTURE = "arm64"
TAG_PREFIX = "openfallout-"

ENGINE = Path("Contents/MacOS/openfallout")
ENGINE_APP = Path("Contents/Helpers/OpenFallout Engine.app")
MINIMUM_KEY = "LSMinimumSystemVersion"
SHORT_VERSION_KEY = "CFBundleShortVersionString"
BUILD_VERSION_KEY = "CFBundleVersion"
MACHO_MAGICS = {bytes.fromhex(magic) for magic in ("cffaedfe", "cefaedfe", "feedfacf", "feedface", "cafebabe", "bebafeca")}

VERSION = re.compile(r"\d+\.\d+\.\d+")
TAG = re.compile(rf"{TAG_PREFIX}(?P<version>\d+\.\d+\.\d+)(?:-(?P<suffix>[A-Za-z0-9][A-Za-z0-9._-]*))?")
MACOS = re.compile(r"\d+(?:\.\d+){0,2}")
SHA256 = re.compile(r"[0-9a-f]{64}")
COMMIT = re.compile(r"[0-9a-f]{40}")
DMG_NAME = re.compile(r"[^/\\\s]+\.dmg")


class StatementError(ValueError):
    pass


class Refusal(RuntimeError):
    """The release gate refuses the release."""


def run(command, **kwargs):
    return subprocess.run([str(part) for part in command], capture_output=True, text=True, **kwargs)


# --------------------------------------------------------------------------------------------------------- versions

def cmake_version(text):
    """The version CMake gives the build (OPENFALLOUT_VERSION_MAJOR, _MINOR and _RELEASE in the top CMakeLists.txt)."""
    parts = []
    for name in ("MAJOR", "MINOR", "RELEASE"):
        found = re.search(rf"^\s*set\(\s*OPENFALLOUT_VERSION_{name}\s+(\d+)\s*\)", text, re.MULTILINE)
        if not found:
            raise StatementError(f"CMakeLists.txt does not set OPENFALLOUT_VERSION_{name} to a number")
        parts.append(found.group(1))
    return ".".join(parts)


def version_of_tag(tag):
    """The version a release tag names: `openfallout-<version>`, optionally followed by `-<suffix>` (a candidate or a
    test build, e.g. `-rc1`)."""
    found = TAG.fullmatch(tag or "")
    if not found:
        raise StatementError(f"tag {tag!r} is not {TAG_PREFIX}<major>.<minor>.<patch> with an optional -<suffix>")
    return found.group("version")


def disk_image_name(version, architecture):
    return f"OpenFallout-{version}-macOS-{architecture}.dmg"


def bundle_versions(version):
    """What both property lists carry for a release of this version. The one place that says so."""
    return {SHORT_VERSION_KEY: version, BUILD_VERSION_KEY: version}


def macos_tuple(text):
    parts = [int(part) for part in str(text).split(".")]
    return tuple(parts + [0] * (3 - len(parts)))


def same_macos(first, second):
    return macos_tuple(first) == macos_tuple(second)


def macos_newer(first, second):
    return macos_tuple(first) > macos_tuple(second)


# --------------------------------------------------------------------------------------------------------- statement

def build_statement(version, tag, commit, disk_image, sha256, minimum_macos, architecture):
    return dict(zip(FIELDS, (SCHEMA, version, tag, commit, disk_image, sha256, minimum_macos, architecture)))


def statement_problems(statement):
    """Everything wrong with the statement on its own: its shape, and that its tag, version and image name agree."""
    if not isinstance(statement, dict):
        return ["the statement is not a JSON object"]
    problems = []
    for field in FIELDS:
        if field not in statement:
            problems.append(f"the statement has no {field}")
    if problems:
        return problems
    if statement["schema"] != SCHEMA:
        problems.append(f"schema is {statement['schema']!r}, this tool reads {SCHEMA}")
    checks = (("version", VERSION), ("source_commit", COMMIT), ("sha256", SHA256), ("minimum_macos", MACOS))
    for field, pattern in checks:
        if not isinstance(statement[field], str) or not pattern.fullmatch(statement[field]):
            problems.append(f"{field} is {statement[field]!r}, not {pattern.pattern}")
    if statement["architecture"] != ARCHITECTURE:
        problems.append(f"architecture is {statement['architecture']!r}, it must be {ARCHITECTURE}")
    if problems:
        return problems
    try:
        tag_version = version_of_tag(statement["tag"])
    except StatementError as error:
        problems.append(f"tag: {error}")
    else:
        if tag_version != statement["version"]:
            problems.append(f"tag {statement['tag']} carries version {tag_version}, but version is {statement['version']}")
    if not isinstance(statement["disk_image"], str) or not DMG_NAME.fullmatch(statement["disk_image"]):
        problems.append(f"disk_image is {statement['disk_image']!r}, not the name of a .dmg file")
    return problems


def write_statement(path, statement):
    Path(path).write_text(json.dumps(statement, indent=2) + "\n", encoding="utf-8")


def read_statement(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise StatementError(f"{path} is not a readable statement ({error})") from None


def scheme_problems(statement, cmake, image, digest, tag, commit):
    """The statement against what is known outside the package: the version CMake gives, the disk image's name and
    digest, the tag CI builds and the commit it builds. All of them must be the statement's."""
    problems = statement_problems(statement)
    if problems:
        return problems
    wanted = disk_image_name(statement["version"], statement["architecture"])
    if statement["disk_image"] != wanted:
        problems.append(f"disk_image is {statement['disk_image']}, but a disk image of version {statement['version']} is "
                        f"named {wanted}")
    for field, expected, shown in (("version", cmake, "CMakeLists.txt"), ("disk_image", image, "the disk image"),
                                   ("sha256", digest, "the disk image"), ("tag", tag, "the release"),
                                   ("source_commit", commit, "the build")):
        if expected is not None and statement[field] != expected:
            problems.append(f"{field} is {statement[field]}, but {shown} says {expected}")
    return problems


# ----------------------------------------------------------------------------------------------------------- package

def read_plist(bundle):
    try:
        with (bundle / "Contents/Info.plist").open("rb") as file:
            return plistlib.load(file)
    except (OSError, plistlib.InvalidFileException):
        return None


def plist_minimum(app):
    """The minimum macOS the outer app's property list states, or None. The one value the shell reads (through the
    statement); the property list stays its source."""
    keys = read_plist(app)
    value = keys.get(MINIMUM_KEY) if keys else None
    return value if isinstance(value, str) and MACOS.fullmatch(value) else None


def package_problems(app, statement, machos, minos_of, archs_of):
    """What differs between the statement, the two property lists and the Mach-O files of the app.

    - the outer app's property list states the minimum, and it is the statement's;
    - the nested engine app's property list states the same minimum;
    - the programs this project builds (the engine, the launcher and the nested engine app's program) report exactly
      that minimum as the macOS they were built for;
    - no other Mach-O file (a library) was built for a newer macOS than that;
    - both property lists carry the statement's version;
    - the engine has the statement's architecture and no other."""
    problems = []
    minimum = plist_minimum(app)
    bundles = ((app, app.name), (app / ENGINE_APP, f"{app.name}/{ENGINE_APP}"))
    for bundle, name in bundles:
        keys = read_plist(bundle)
        if keys is None:
            problems.append(f"{name} has no readable Contents/Info.plist")
            continue
        value = keys.get(MINIMUM_KEY)
        if not value:
            problems.append(f"{name}: {MINIMUM_KEY} is not set, so macOS would not refuse an older system")
        elif not isinstance(value, str) or not MACOS.fullmatch(value):
            problems.append(f"{name}: {MINIMUM_KEY} is {value!r}, not a version")
        elif minimum is not None and not same_macos(value, minimum):
            problems.append(f"{name}: {MINIMUM_KEY} is {value}, but {app.name} says {minimum}")
        for key, wanted in bundle_versions(statement["version"]).items():
            if keys.get(key) != wanted:
                problems.append(f"{name}: {key} is {keys.get(key)!r}, but the version is {wanted}")
    shown = statement["minimum_macos"]
    shown = shown if isinstance(shown, str) and MACOS.fullmatch(shown) else None  # a malformed one is reported elsewhere
    if minimum is not None and shown is not None and not same_macos(minimum, shown):
        problems.append(f"minimum_macos is {statement['minimum_macos']}, but {app.name} states {minimum} in "
                        f"{MINIMUM_KEY}")
    own = {app / ENGINE}
    for bundle, _ in bundles:
        executable = (read_plist(bundle) or {}).get("CFBundleExecutable")
        if executable:
            own.add(bundle / "Contents/MacOS" / executable)
    reference = minimum or shown
    for binary in sorted(set(machos) | own) if reference is not None else []:
        name = binary.relative_to(app)
        built_for = minos_of(binary)
        if built_for is not None and not MACOS.fullmatch(built_for):
            built_for = None
        if built_for is None:
            problems.append(f"{name}: the macOS it was built for cannot be read")
        elif binary in own:
            if not same_macos(built_for, reference):
                problems.append(f"{name}: built for macOS {built_for}, but the minimum is {reference}")
        elif macos_newer(built_for, reference):
            problems.append(f"{name}: built for macOS {built_for}, newer than the minimum {reference}")
    archs = archs_of(app / ENGINE)
    if archs != [statement["architecture"]]:
        problems.append(f"{ENGINE}: architecture is {' '.join(archs) or 'unknown'}, the statement says "
                        f"{statement['architecture']} only")
    return problems


def is_macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as file:
        return file.read(4) in MACHO_MAGICS


def parse_vtool(text):
    """The macOS a binary was built for, from `vtool -show-build` (LC_BUILD_VERSION minos, or the older
    LC_VERSION_MIN_MACOSX version); for a universal file the arm64 slice."""
    slices, current, command = {}, "", ""
    for line in text.splitlines():
        header = re.search(r"\(architecture (\w+)\)", line)
        if header:
            current, command = header.group(1), ""
            continue
        words = line.split()
        if len(words) == 2 and words[0] == "cmd":
            command = words[1]
        elif len(words) == 2 and command == "LC_BUILD_VERSION" and words[0] == "minos":
            slices.setdefault(current, words[1])
        elif len(words) == 2 and command == "LC_VERSION_MIN_MACOSX" and words[0] == "version":
            slices.setdefault(current, words[1])
    for name in ("arm64", "arm64e", ""):
        if name in slices:
            return slices[name]
    return next(iter(slices.values()), None)


def minos_of(path):
    return parse_vtool(run(["vtool", "-show-build", path]).stdout)


def archs_of(path):
    return run(["lipo", "-archs", path]).stdout.split()


@contextlib.contextmanager
def mounted(dmg):
    import check_mac_package

    with check_mac_package.mounted(dmg) as volume:
        yield volume


def sha256_of(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ------------------------------------------------------------------------------------------------------ release notes

SUPPORTED = re.compile(r"^- Supported: any Apple Silicon Mac, macOS (?P<minimum>\d+(?:\.\d+){0,2}) or later\.$")
PLAYED_ON = re.compile(r"^- Played on: (?P<mac>\S[^\n]*?), macOS (?P<macos>\d+(?:\.\d+){0,2})\.$")


def supported_line(minimum):
    """What the project checks itself, in the words the glossary gives *supported*."""
    return f"- Supported: any Apple Silicon Mac, macOS {minimum} or later."


def played_on_line(mac, macos):
    """The Mac and macOS version the owner ran the real games on. Both come from the owner at release time."""
    mac, macos = (mac or "").strip().rstrip("."), (macos or "").strip()
    if not mac or not MACOS.fullmatch(macos):
        raise StatementError("the played-on line needs the Mac (model and chip) and the macOS version, as the owner gives them")
    return f"- Played on: {mac}, macOS {macos}."


def notes_problems(body, minimum):
    """What the release notes lack: the supported line (with the statement's minimum) and the played-on line."""
    lines = [line.strip() for line in (body or "").splitlines()]
    problems = []
    supported = [found for found in map(SUPPORTED.match, lines) if found]
    if not supported:
        problems.append("the release notes lack the line '- Supported: any Apple Silicon Mac, macOS <version> or later.'")
    elif not any(same_macos(found["minimum"], minimum) for found in supported):
        problems.append(f"the Supported line of the release notes does not say macOS {minimum}, the statement's minimum")
    if not any(PLAYED_ON.match(line) for line in lines):
        problems.append("the release notes lack the line '- Played on: <Mac and chip>, macOS <version>.' "
                        "(the owner adds it: engine_release.py notes --played-on-mac ... --played-on-macos ...)")
    return problems


# --------------------------------------------------------------------------------------------------------- the gate

def uploaded_assets(release):
    return {asset["name"]: asset for asset in release.get("assets", [])
            if asset.get("size", 0) > 0 and asset.get("state") == "uploaded"}


def verify_release(release, commit, fetch):
    """The part of the release gate that concerns the engine release's statement. `commit` is the commit of the tag,
    `fetch(asset)` returns the bytes of an uploaded asset. Raises Refusal."""
    assets = uploaded_assets(release)
    asset = assets.get(STATEMENT_NAME)
    if asset is None:
        raise Refusal(f"Release is missing its {STATEMENT_NAME}")
    try:
        statement = json.loads(fetch(asset))
    except ValueError:
        raise Refusal(f"{STATEMENT_NAME} is not JSON") from None
    problems = statement_problems(statement)
    if problems:
        raise Refusal(f"{STATEMENT_NAME} is not a statement of an engine release: " + "; ".join(problems))
    image = assets.get(statement["disk_image"])
    if image is None:
        raise Refusal(f"{STATEMENT_NAME} names {statement['disk_image']}, which the release does not hold")
    digest = re.fullmatch(r"sha256:([0-9a-f]{64})", image.get("digest") or "")
    if not digest:
        raise Refusal(f"GitHub reports no sha256 digest for {image['name']}, so the statement cannot be compared with it")
    if statement["sha256"] != digest.group(1):
        raise Refusal(f"{STATEMENT_NAME} says sha256 {statement['sha256']} for {image['name']}, but the uploaded image "
                      f"has {digest.group(1)}")
    if statement["source_commit"] != commit:
        raise Refusal(f"{STATEMENT_NAME} says the image was built from {statement['source_commit']}, but the tag "
                      f"{release.get('tag_name')} is {commit}")
    if statement["tag"] != release.get("tag_name"):
        raise Refusal(f"{STATEMENT_NAME} says tag {statement['tag']}, but the release is {release.get('tag_name')}")
    problems = notes_problems(release.get("body"), statement["minimum_macos"])
    if problems:
        raise Refusal("; ".join(problems))


# ------------------------------------------------------------------------------------------------------------- main

def cmake_expected():
    try:
        return cmake_version(CMAKE_LISTS.read_text(encoding="utf-8"))
    except OSError as error:
        raise StatementError(f"{CMAKE_LISTS} cannot be read ({error})") from None


def check_package(dmg, statement, cmake, tag, commit):
    """(the statement, every problem between it and the disk image `dmg`); the image is mounted for it. Without a
    statement one is made from the package (the minimum macOS from the outer app's property list)."""
    digest = sha256_of(dmg)
    with mounted(dmg) as volume:
        app = volume / "OpenFallout.app"
        if statement is None:
            statement = build_statement(cmake, tag, commit, dmg.name, digest, plist_minimum(app) or "", ARCHITECTURE)
        problems = scheme_problems(statement, cmake, dmg.name, digest, tag, commit)
        if not app.is_dir():
            return statement, problems + [f"{dmg.name} holds no OpenFallout.app"]
        if all(field in statement for field in FIELDS):
            files = [path for path in app.rglob("*") if is_macho(path)]
            problems += package_problems(app, statement, files, minos_of, archs_of)
        return statement, problems


def command_write(options):
    statement, problems = check_package(options.dmg.absolute(), None, cmake_expected(), options.tag, options.commit)
    return finish(problems, options.out, statement)


def command_verify(options):
    statement = read_statement(options.statement)
    _, problems = check_package(options.dmg.absolute(), statement, cmake_expected(), options.tag, options.commit)
    return finish(problems, None, statement)


def finish(problems, out, statement):
    if problems:
        for problem in problems:
            print(f"FAIL engine-release: {problem}")
        return 1
    if out is not None:
        write_statement(out, statement)
        print(f"PASS engine-release: wrote {out} (version {statement['version']}, tag {statement['tag']}, "
              f"minimum macOS {statement['minimum_macos']}, {statement['architecture']}, sha256 {statement['sha256']})")
    else:
        print("PASS engine-release: the statement, the property lists, the binaries and the tag agree")
    return 0


def command_notes(options, parser):
    if bool(options.played_on_mac) != bool(options.played_on_macos):
        parser.error("--played-on-mac and --played-on-macos go together")
    statement = read_statement(options.statement)
    problems = statement_problems(statement)
    if problems:
        raise StatementError("; ".join(problems))
    lines = [supported_line(statement["minimum_macos"])]
    if options.played_on_mac:
        lines.append(played_on_line(options.played_on_mac, options.played_on_macos))
    else:
        print("no Played on line: the owner adds it (--played-on-mac and --played-on-macos); the release gate refuses to "
              "publish without it")
    if options.append:
        with options.append.open("a", encoding="utf-8") as file:
            file.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog="See the top of this file.")
    commands = parser.add_subparsers(dest="command", required=True)
    write = commands.add_parser("write", help="write the statement from a disk image, if everything agrees")
    write.add_argument("dmg", type=Path)
    write.add_argument("--tag", required=True, help="the release tag, openfallout-<version>[-suffix]")
    write.add_argument("--commit", required=True, help="the commit the image was built from")
    write.add_argument("--out", type=Path, required=True, help=f"where to write {STATEMENT_NAME}")
    verify = commands.add_parser("verify", help="check a statement against a disk image")
    verify.add_argument("dmg", type=Path)
    verify.add_argument("statement", type=Path)
    verify.add_argument("--tag", help="the tag the statement must name")
    verify.add_argument("--commit", help="the commit the statement must name")
    notes = commands.add_parser("notes", help="print (and append) the release-notes lines the gate asks for")
    notes.add_argument("statement", type=Path)
    notes.add_argument("--played-on-mac", help="the Mac the owner played on, model and chip")
    notes.add_argument("--played-on-macos", help="the macOS version the owner played on")
    notes.add_argument("--append", type=Path, help="append the lines to this file")
    options = parser.parse_args(argv)
    try:
        if options.command == "write":
            return command_write(options)
        if options.command == "verify":
            return command_verify(options)
        return command_notes(options, notes)
    except StatementError as error:
        print(f"FAIL engine-release: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
