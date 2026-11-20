#!/usr/bin/env python3
"""Fail when a change leaves C or C++ lines that clang-format would lay out differently.

Only the lines the change touches are checked (git clang-format against the base commit). The whole tree is not clean:
on 20 November 2026, 267 of the 2,955 files under apps/ and components/ differed under clang-format 14.0.6, the
version that differs least (newer versions differ in more files), all of them edited since the fork. A check of the
changed lines holds new code to .clang-format without a mass reformat first.

The version is pinned (CI/clang-format-version.txt): another version lays out some lines differently, and a check
that changes with the machine it runs on would lie. Install it with
`pip install "clang-format==$(cat CI/clang-format-version.txt)"`.

Usage: check_format_changes.py BASE [--clang-format PATH]
Exit status 0 when the changed lines are formatted, 1 with the diff that would format them, 2 on a usage problem.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "CI" / "clang-format-version.txt").read_text().strip()
# The folders that hold the engine's own C and C++; extern/ is third-party code and keeps its own layout.
FOLDERS = ("apps", "components")
# .clang-format has no Objective-C section, so .mm and .m files are left out.
EXTENSIONS = "c,cc,cpp,cxx,h,hpp"
CLEAN = ("no modified files to format", "clang-format did not modify any files")


def binary_version(binary):
    out = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True).stdout
    words = out.split()
    return words[words.index("version") + 1]


def git_clang_format(binary):
    """The git-clang-format script installed next to the clang-format binary (the PyPI wheel ships both)."""
    beside = Path(binary).resolve().parent / "git-clang-format"
    return str(beside) if beside.exists() else shutil.which("git-clang-format")


def check(base, binary):
    """Return (ok, text): the diff git clang-format proposes for the lines changed since base."""
    found = binary_version(binary)
    if found != VERSION:
        return False, f"clang-format {found} at {binary}; the check needs {VERSION} (CI/clang-format-version.txt)"
    script = git_clang_format(binary)
    if not script:
        return False, "git-clang-format was not found next to clang-format or on PATH"
    result = subprocess.run(
        [sys.executable, script, "--binary", binary, "--diff", "--extensions", EXTENSIONS, base, "--", *FOLDERS],
        cwd=ROOT, capture_output=True, text=True)
    text = (result.stdout + result.stderr).strip()
    if result.returncode != 0 and not text.startswith("diff"):
        return False, f"git clang-format failed ({result.returncode}): {text}"
    if text in CLEAN or not text:
        return True, f"the lines changed since {base[:12]} are formatted (clang-format {VERSION})"
    return False, text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("base", help="the commit the change is compared with")
    parser.add_argument("--clang-format", default=shutil.which("clang-format") or "clang-format")
    args = parser.parse_args(argv)
    if not args.base or set(args.base) == {"0"}:
        print("No base commit to compare with: nothing to check", file=sys.stderr)
        return 2
    ok, text = check(args.base, args.clang_format)
    print(text)
    if not ok:
        print(f"\nFAIL format: run `git clang-format --binary <clang-format {VERSION}> {args.base[:12]}` and commit "
              "the result.", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
