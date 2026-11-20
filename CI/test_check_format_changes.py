"""The format check of changed lines (check_format_changes.py), with stand-ins for clang-format and git-clang-format so
that it runs without either installed. The real pair runs on every change in the Changes job (push.yml)."""

import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_format_changes as check_format  # noqa: E402


def stand_ins(folder, version, git_output, git_status=0):
    """A clang-format that reports `version` and a git-clang-format beside it that prints `git_output`."""
    clang = folder / "clang-format"
    clang.write_text(f"#!/bin/sh\necho 'clang-format version {version} (stand-in)'\n")
    script = folder / "git-clang-format"
    script.write_text(f"import sys\nsys.stdout.write({git_output!r})\nsys.exit({git_status})\n")
    for path in (clang, script):
        path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return str(clang)


class CheckFormatChangesTest(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp(prefix="format-check-"))

    def test_formatted_changes_pass(self):
        for clean in check_format.CLEAN:
            with self.subTest(output=clean):
                ok, text = check_format.check("a" * 40, stand_ins(self.folder, check_format.VERSION, clean + "\n"))
                self.assertTrue(ok)
                self.assertIn("are formatted", text)

    def test_a_proposed_diff_fails_and_is_shown(self):
        diff = "diff --git a/apps/x.cpp b/apps/x.cpp\n-int  x;\n+int x;\n"
        ok, text = check_format.check("a" * 40, stand_ins(self.folder, check_format.VERSION, diff))
        self.assertFalse(ok)
        self.assertIn("+int x;", text)

    def test_a_diff_with_a_nonzero_status_still_fails_as_a_diff(self):
        # newer git-clang-format versions exit 1 when they propose a diff
        ok, text = check_format.check("a" * 40, stand_ins(self.folder, check_format.VERSION, "diff --git a b\n", 1))
        self.assertFalse(ok)
        self.assertTrue(text.startswith("diff"))

    def test_another_clang_format_version_is_refused(self):
        ok, text = check_format.check("a" * 40, stand_ins(self.folder, "18.1.8", "no modified files to format\n"))
        self.assertFalse(ok)
        self.assertIn("needs " + check_format.VERSION, text)

    def test_a_failing_git_clang_format_fails(self):
        ok, text = check_format.check("a" * 40, stand_ins(self.folder, check_format.VERSION, "error: bad revision\n", 1))
        self.assertFalse(ok)
        self.assertIn("git clang-format failed", text)

    def test_no_base_is_a_usage_error(self):
        self.assertEqual(check_format.main(["0" * 40, "--clang-format", stand_ins(self.folder, "14.0.6", "")]), 2)

    def test_the_version_file_names_one_exact_version(self):
        self.assertRegex(check_format.VERSION, r"^\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main()
