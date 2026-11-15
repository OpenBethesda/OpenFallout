# Core and edges

OpenFallout is Mac only. Its code is split in two kinds.

- The **core** holds the game itself: records, scripts, the world and the gameplay rules. It makes no Apple calls and stays plain C++.
- The **edges** touch the machine: the window, sound, input, drawing, packaging, and every native bridge. Mac-only code is allowed at the edges and nowhere else.

A **native bridge** is a small Mac-only part that reaches one Apple framework directly for a feature a kept library cannot give. It sits beside that library and does not replace it. SDL and OpenAL Soft stay.

## Languages

- Objective-C++ (`.mm`) is allowed only inside a native bridge, which is an edge.
- There is no Swift in the engine.
- Objective-C files outside the edges fail the check below.

## The rule

In every source file under `apps/` and `components/` that is not on the edge list, the Mac job fails on:

- an `#include` or `#import` of an Apple header, such as `CoreFoundation/`, `AppKit/`, `Metal/`, `mach-o/`, `pthread/qos.h`, `sys/sysctl.h` or `TargetConditionals.h`;
- an Apple macro, such as `__APPLE__`, `__MACH__`, `TARGET_OS_*`, `Q_OS_MACOS` or `MAC_OS_X_VERSION_*`;
- a run-time name that reaches AppKit, such as `libobjc`, `objc_msgSend`, `objc_getClass`, `sel_registerName` or `NSApplication`;
- any `.m` or `.mm` file.

`extern/` (third-party code kept in the repository) is skipped. A name that appears only in a comment, or inside `#if 0`, does not count. The full lists are in `CI/check_core_edges.py`.

## Running the check

```
python3 CI/check_core_edges.py
```

It prints one line per violation as `path:line: what`, then a count. The exit status is 1 when there is any violation. `--list-edges` prints the edge list with the reason for each entry. The tests are in `CI/test_check_core_edges.py`; they build small temporary trees and never read the real one.

What the check does not see: names built by the preprocessor, raw string literals, `#if 0` written in other ways, files that are not C or C++ sources (CMake files, shaders, scripts), and `extern/`.

## Adding an edge

The edge list is the table `EDGES` at the top of `CI/check_core_edges.py`. Each entry is a path prefix and the reason the code touches the machine. Adding an edge is a one-line change, and the reviewer decides whether the reason is true. Prefer a new native bridge in an existing edge folder to widening the list. Do not add a core folder.

