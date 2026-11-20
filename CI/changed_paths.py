#!/usr/bin/env python3
"""Decide whether a change needs the Mac build and its tests.

Only a change that touches nothing but the notes and trackers under docs/openfallout,
the helper scripts under scripts/openfallout (their Python tests run in the Changes job
of push.yml, whatever else changed) and the top-level README, CHANGELOG and AUTHORS may
skip them.  The round runner is the exception among the helper scripts: the tests that
run a whole round work on macOS only and run in the Mac jobs, so a change to it builds.  Each Mac job of macos.yml
runs this script itself, as its first step.
A skipped build reports green, so every
doubt runs everything: an empty or unreadable list of changed files, a base commit that
cannot be fetched, a manual run, a deleted file outside the two folders, and any change
to CI itself.
"""

import argparse
import os
import subprocess
import sys

# No build reads these folders, and the tests of the Python helpers run in the Changes job.
DOCUMENTATION_TREES = ("docs/openfallout/", "scripts/openfallout/")
# CMakeLists.txt installs these files, so changing one is harmless but removing one is not.
DOCUMENTATION_FILES = ("README.md", "CHANGELOG.md", "AUTHORS.md")
# The Changes job runs on a Linux runner, where the tests that run a whole round skip themselves (round.py run works
# on macOS only).  They run in the Mac job "Python tests on macOS", so a change to what they test must not skip it.
MAC_TESTED_FILES = ("scripts/openfallout/round/round.py", "scripts/openfallout/round/roundguard.py",
                    "scripts/openfallout/test_round.py")
# The scripts the Mac jobs run against the built app: the packaged-app check starts the app through
# scripts/openfallout/mac_launch.py and builds its smoke plugin with synthetic_fallout_plugin.py, which imports more
# helpers. A change to any of them can break that check without failing a unit test (on 20 November 2026 a launcher
# change that dropped all but the last --content passed every test and skipped the Mac build: pull request 257).
# They are read from the imports, so a new import is covered without a list to keep.
MAC_RUNNERS = ("CI/check_mac_package.py",)
HELPERS = "scripts/openfallout"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _helper_references(source):
    """The helper modules a Python file names: `import x`/`from x import` of a module in HELPERS, and string literals
    that spell out a path in HELPERS."""
    import ast
    import re
    tree = ast.parse(source)
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
            names.add(node.module.split(".")[0])
        elif isinstance(node, ast.Constant) and isinstance(node.value, str):
            names.update(re.findall(HELPERS + r"/([A-Za-z_][A-Za-z0-9_]*)\.py", node.value))
    return names


def mac_checked_helpers(root=ROOT):
    """Paths (relative to root) of every helper module the MAC_RUNNERS reach, following imports between helpers."""
    found, todo = set(), [os.path.join(root, runner) for runner in MAC_RUNNERS]
    while todo:
        with open(todo.pop(), encoding="utf-8") as source:
            for name in _helper_references(source.read()):
                path = "%s/%s.py" % (HELPERS, name)
                if path not in found and os.path.isfile(os.path.join(root, path)):
                    found.add(path)
                    todo.append(os.path.join(root, path))
    return frozenset(found)


def skippable(status, path, mac_checked=None):
    """Whether a changed file can be left out of the build, from git's one-letter status."""
    if path in MAC_TESTED_FILES:
        return False
    if path in (mac_checked_helpers() if mac_checked is None else mac_checked):
        return False
    if path.startswith(DOCUMENTATION_TREES):
        return True
    return status in ("A", "M") and path in DOCUMENTATION_FILES


def needs_build(changes):
    """changes is a list of (status, path) pairs, as git diff --name-status --no-renames gives."""
    if not changes:
        return True
    mac_checked = mac_checked_helpers()
    return not all(skippable(status, path, mac_checked) for status, path in changes)


def diff_changes(base, head="HEAD", cwd=None):
    out = subprocess.check_output(
        ["git", "diff", "--name-status", "--no-renames", "-z", base, head], cwd=cwd
    )
    parts = out.split(b"\0")
    if parts and parts[-1] == b"":
        parts.pop()
    if len(parts) % 2:
        raise ValueError("unexpected output of git diff")
    return [
        (parts[i].decode(), os.fsdecode(parts[i + 1])) for i in range(0, len(parts), 2)
    ]


def decide(event, base, cwd=None):
    """Return (needs_build, reason).  Fails closed: any problem means build."""
    try:
        if event not in ("push", "pull_request"):
            return True, "a %s run always builds" % (event or "manual")
        if not base or set(base) == {"0"}:
            return True, "there is no base commit to compare with"
        subprocess.run(
            ["git", "fetch", "--no-tags", "--depth=1", "origin", base], check=True, cwd=cwd
        )
        changes = diff_changes(base, "HEAD", cwd)
        if not changes:
            return True, "the list of changed files is empty"
        mac_checked = mac_checked_helpers(cwd or ROOT)
        other = [path for status, path in changes if not skippable(status, path, mac_checked)]
        if other:
            return True, "%d of %d changed files are not documentation, first %s" % (
                len(other), len(changes), other[0])
        return False, "only documentation and notes changed (%d files)" % len(changes)
    except Exception as error:  # noqa: BLE001 - a wrong skip is worse than a slow run
        return True, "could not list the changed files (%s)" % error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", default="")
    parser.add_argument("--base", default="")
    parser.add_argument("--github-output", default="")
    args = parser.parse_args()
    build, reason = decide(args.event, args.base)
    print("builds needed: %s, because %s" % ("yes" if build else "no", reason))
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as output:
            output.write("code=%s\n" % ("true" if build else "false"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
