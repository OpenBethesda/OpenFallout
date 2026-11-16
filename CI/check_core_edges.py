#!/usr/bin/env python3
"""Fail if code outside the edges of the engine uses Apple headers, macros or Objective-C.

    CI/check_core_edges.py [--root DIR] [--list-edges]

OpenFallout is Mac only, but its core (records, scripts, world, gameplay rules) stays plain C++. Mac-only code is
allowed at the edges only: window, sound, input, drawing, packaging and every native bridge. Every source file
under apps/ and components/ that is not on the edge list below must have none of these:

  * an #include or #import of an Apple header (the list is APPLE_HEADERS),
  * an Apple macro (APPLE_MACROS),
  * a run-time name that reaches AppKit through the Objective-C runtime (RUNTIME_NAMES),
  * an Objective-C file (.m or .mm).

extern/ is skipped. One line per violation is printed as `path:line: what`, then a count; the exit status is 1
when there is any violation. See docs/openfallout/core-and-edges.md.

What the check does not see: names built by the preprocessor or by string joins, raw string literals (a `//` inside
one is read as a comment), line continuations inside comments, dead code written in any other way than a bare `#if 0` (`#if (0)`,
`#ifdef NEVER`; a condition such as `#if 0 || 1` is read as live code), files with other extensions (CMake files, shaders, scripts), and anything under extern/.
A name in a comment is not a violation. A name inside `#if 0 ... #endif` is not a violation.
A header is an Apple header when it is on the lists below or has the shape `<Name/Name.h>` with a capital first letter.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCANNED_TREES = ("apps", "components")
SKIPPED_TREES = ("extern",)
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".inc", ".in", ".mm", ".m")
OBJECTIVE_C_SUFFIXES = (".mm", ".m")

# The edge list. A file is an edge when its path (relative to the repository root, with forward slashes) starts
# with one of these prefixes. Everything else under apps/ and components/ is core. Adding an edge is a reviewed
# one-line change: give the prefix and say why the code touches the machine.
EDGES = (
    # window, sound, input, drawing
    ("components/sdlutil/", "window and input: SDL is the one seam to the window and the controller"),
    ("apps/components_tests/sdlutil/", "tests of the window and input edge"),
    ("components/platform/", "the thin layer over the file and process calls of the machine"),
    ("apps/openfallout/mwinput/", "input: keys, pointer and controller"),
    ("apps/openfallout_tests/mwinput/", "tests of the input edge"),
    ("apps/openfallout/mwsound/", "sound: the OpenAL Soft seam"),
    ("apps/openfallout/mwrender/", "drawing: the OpenGL path (the Metal renderer goes here too)"),
    # packaging, native bridges, start-up
    ("apps/openfallout/macos/", "packaging: the nested engine app and its start-up shim"),
    ("apps/openfallout/main.cpp", "engine start-up: reaches AppKit to name the app and set its activation"),
    ("apps/launcher/", "launcher: the Qt front screen of the packaged app"),
    # crash handling, Mac paths, thread priority
    ("components/crashcatcher/", "crash handling: signals, the process list and the stack"),
    ("components/files/macospath", "Mac paths: bundle, Application Support and Library folders"),
    ("components/misc/thread.cpp", "thread priority: quality-of-service classes"),
    ("apps/components_tests/misc/testthread.cpp", "test of the thread priority edge"),
)

APPLE_HEADER_PREFIXES = (
    "CoreFoundation/", "Foundation/", "AppKit/", "Cocoa/", "mach/", "mach-o/", "dispatch/", "os/", "objc/",
    "IOKit/", "Metal/", "MetalKit/", "CoreMotion/", "CoreAudio/", "AudioToolbox/", "CoreHaptics/",
    "GameController/", "Security/", "QuartzCore/", "OpenGL/", "OpenAL/", "libkern/", "malloc/", "simd/",
    "CoreGraphics/", "CoreText/", "CoreServices/", "CoreVideo/", "CoreMedia/", "CoreImage/", "CoreData/",
    "CoreLocation/", "CoreBluetooth/", "CoreMIDI/", "CoreML/", "CoreSpotlight/", "CoreTelephony/", "CoreWLAN/",
    "ApplicationServices/", "Carbon/", "CFNetwork/", "AVFoundation/", "AVFAudio/", "AVKit/", "AudioUnit/",
    "Accelerate/", "vecLib/", "IOSurface/", "ImageIO/", "MetalFX/", "MetalPerformanceShaders/", "ModelIO/",
    "GameKit/", "GameplayKit/", "SceneKit/", "SpriteKit/", "StoreKit/", "SystemConfiguration/", "DiskArbitration/",
    "ForceFeedback/", "UserNotifications/", "UniformTypeIdentifiers/", "VideoToolbox/", "WebKit/", "Network/",
    "ServiceManagement/", "LocalAuthentication/", "CryptoKit/", "CommonCrypto/", "xpc/", "launch/", "notify/",
    "os_log/", "sandbox/", "EndpointSecurity/", "Spatial/", "PHASE/", "ScreenCaptureKit/", "Vision/",
)
# Every Apple framework is included as <Name/Name.h> with a capital first letter; the libraries the engine uses that
# have this shape are lower case (gtest/gtest.h). This catches a framework the list above does not name.
UMBRELLA_HEADER = re.compile(r"^([A-Z]\w+)/\1\.h$")
APPLE_HEADER_NAMES = (
    "pthread/qos.h", "libproc.h", "sys/sysctl.h", "copyfile.h", "TargetConditionals.h", "Availability.h",
    "AvailabilityMacros.h",
)
HEADER = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]\s*([^>"]+)[>"]')
APPLE_MACROS = re.compile(
    r"\b(__APPLE__|__MACH__|TARGET_OS_\w+|__OBJC__|Q_OS_MAC(?:OS)?|Q_OS_DARWIN|MAC_OS_X_VERSION_\w+"
    r"|__apple_build_version__)\b")
RUNTIME_NAMES = re.compile(r"\b(libobjc|objc_msgSend|objc_getClass|sel_registerName|NSApplication)")
# only a condition that is exactly 0; "#if 0 || 1" is live code (comments are blanked before this is matched)
IF_ZERO = re.compile(r"^\s*#\s*if\s+0\s*$")
IF_ANY = re.compile(r"^\s*#\s*if(?:def|ndef)?\b")
ELSE_OR_ELIF = re.compile(r"^\s*#\s*(?:else|elif)\b")
ENDIF = re.compile(r"^\s*#\s*endif\b")


def is_edge(path: str) -> bool:
    return path.startswith(tuple(prefix for prefix, _ in EDGES))


def strip_comments(text: str) -> list[str]:
    """Return the lines of text with comments blanked out. Strings and character literals are kept."""
    lines = []
    current = []
    state = "code"  # code, block, string, char
    i = 0
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if c == "\n":
            lines.append("".join(current))
            current = []
            if state in ("string", "char"):
                state = "code"  # an unterminated literal ends at the line
            i += 1
            continue
        if state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                i += 2
            else:
                i += 1
        elif state in ("string", "char"):
            current.append(c)
            if c == "\\" and nxt and nxt != "\n":
                current.append(nxt)
                i += 2
                continue
            if (state == "string" and c == '"') or (state == "char" and c == "'"):
                state = "code"
            i += 1
        elif c == "/" and nxt == "*":
            state = "block"
            current.append(" ")
            i += 2
        elif c == "/" and nxt == "/":
            while i < len(text) and text[i] != "\n":
                i += 1
        else:
            if c == '"':
                state = "string"
            elif c == "'" and not (current and (current[-1].isalnum() or current[-1] == "_")):
                state = "char"  # not a digit separator such as 1'000
            current.append(c)
            i += 1
    lines.append("".join(current))
    return lines


def live_lines(lines: list[str]):
    """Yield (number, text) for the lines that are not inside `#if 0`."""
    stack = []  # one entry per open #if: "zero" (inside #if 0), "dead" (inside one that is dead) or "live"
    for number, line in enumerate(lines, start=1):
        dead = any(entry in ("zero", "dead") for entry in stack)
        if IF_ZERO.match(line):
            stack.append("dead" if dead else "zero")
        elif IF_ANY.match(line):
            stack.append("dead" if dead else "live")
        elif ELSE_OR_ELIF.match(line) and stack and stack[-1] == "zero":
            stack[-1] = "live"
        elif ENDIF.match(line) and stack:
            stack.pop()
        if not dead:
            yield number, line


def what_is_wrong(line: str) -> str | None:
    header = HEADER.match(line)
    if header:
        name = header.group(1).strip()
        if (name.startswith(APPLE_HEADER_PREFIXES) or name in APPLE_HEADER_NAMES or name.startswith("mach_")
                or UMBRELLA_HEADER.match(name)):
            return f"Apple header {name}"
    macro = APPLE_MACROS.search(line)
    if macro:
        return f"Apple macro {macro.group(1)}"
    word = RUNTIME_NAMES.search(line)
    if word:
        return f"Objective-C run-time name {word.group(1)}"
    return None


def check_tree(root: Path) -> list[tuple[str, int, str]]:
    """Return (path, line, what) for every violation under root, sorted by path and line."""
    found = []
    for tree in SCANNED_TREES:
        base = root / tree
        if not base.is_dir():
            continue
        for file in sorted(base.rglob("*")):
            relative = file.relative_to(root).as_posix()
            if not file.is_file() or not relative.endswith(SOURCE_SUFFIXES):
                continue
            if relative.startswith(tuple(f"{skipped}/" for skipped in SKIPPED_TREES)) or is_edge(relative):
                continue
            if relative.endswith(OBJECTIVE_C_SUFFIXES):
                found.append((relative, 1, "Objective-C file outside the edges"))
                continue
            text = file.read_text(encoding="utf-8", errors="replace")
            for number, line in live_lines(strip_comments(text)):
                what = what_is_wrong(line)
                if what:
                    found.append((relative, number, what))
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: this repository)")
    parser.add_argument("--list-edges", action="store_true", help="print the edge list and exit")
    arguments = parser.parse_args(argv)
    if arguments.list_edges:
        for prefix, reason in EDGES:
            print(f"{prefix}  {reason}")
        return 0
    found = check_tree(arguments.root)
    for path, number, what in found:
        print(f"{path}:{number}: {what}")
    print(f"{len(found)} violation{'' if len(found) == 1 else 's'}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
