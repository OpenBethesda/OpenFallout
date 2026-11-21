"""CI/run_python_tests.py: it runs every file, and says so when a file fails, breaks, or does not run its counted tests."""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_python_tests  # noqa: E402

PASSING = "import unittest\nclass T(unittest.TestCase):\n    def test_a(self): pass\n    def test_b(self): pass\n"
FAILING = "import unittest\nclass T(unittest.TestCase):\n    def test_a(self): self.fail('on purpose')\n"
BROKEN = "import module_that_does_not_exist\n"
NOTHING = "VALUE = 1\n"


class RunnerTests(unittest.TestCase):
    def run_folder(self, **files):
        folder = tempfile.TemporaryDirectory(prefix="python tests ")
        self.addCleanup(folder.cleanup)
        for name, text in files.items():
            (Path(folder.name) / name).write_text(text)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = run_python_tests.main([folder.name, "--workers", "2"])
        return status, out.getvalue()

    def test_every_file_runs_and_the_counts_are_proved(self):
        status, output = self.run_folder(**{"test_one.py": PASSING, "test_two.py": PASSING})
        self.assertEqual(status, 0, output)
        self.assertIn("tests counted by discovery: 4; tests run: 4; files run: 2 of 2", output)

    def test_a_failing_file_fails_the_run_and_its_output_is_printed_again(self):
        status, output = self.run_folder(**{"test_one.py": PASSING, "test_bad.py": FAILING})
        self.assertEqual(status, 1)
        self.assertIn("===== FAILED:", output)
        self.assertIn("on purpose", output)
        self.assertIn("test_bad.py: exit status 1", output)

    def test_a_file_that_cannot_be_loaded_fails_the_run(self):
        status, output = self.run_folder(**{"test_one.py": PASSING, "test_broken.py": BROKEN})
        self.assertEqual(status, 1)
        self.assertIn("module_that_does_not_exist", output)

    def test_a_file_without_tests_is_named_and_does_not_fail_the_run(self):
        status, output = self.run_folder(**{"test_one.py": PASSING, "test_script.py": NOTHING})
        self.assertEqual(status, 0, output)
        self.assertIn("test_script.py has no tests", output)


if __name__ == "__main__":
    unittest.main()
