"""Verify per-suite isolation and faithful JUnit reporting, including failures."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location("reports", Path(__file__).with_name("test_reports.py"))
reports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reports)


class TestReportsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        for binary in reports.SUITES.values():
            (self.build / binary).touch()
        self.xml = '''<testsuites time="1.25"><testsuite name="Sample" time="1.25">
          <testcase name="passes" classname="Sample" time="0.25"/>
          <testcase name="fails" classname="Sample" time="1"><failure message="failed">details</failure></testcase>
          <testcase name="skips" classname="Sample" time="0"><skipped message="unavailable"/></testcase>
          <testcase name="DISABLED_case" classname="Sample" time="0" status="notrun" result="suppressed"/>
        </testsuite></testsuites>'''

    def test_junit_preserves_failures_skips_names_and_durations(self):
        source = self.root / "gtest.xml"
        source.write_text(self.xml)
        destination = self.root / "junit.xml"
        reports.write_junit(source, destination, self.root)
        root = ET.parse(destination).getroot()
        self.assertEqual({key: root.get(key) for key in ("tests", "failures", "errors", "skipped")},
                         {"tests": "4", "failures": "1", "errors": "0", "skipped": "2"})
        cases = root.findall("./testsuite/testcase")
        self.assertEqual(cases[0].get("time"), "0.25")
        self.assertEqual(cases[1].find("failure").text, "details")
        self.assertEqual(cases[2].find("skipped").get("message"), "unavailable")
        self.assertIsNotNone(cases[3].find("skipped"))
        self.assertIsNone(cases[3].get("status"))

    def test_junit_uses_repository_relative_source_paths(self):
        source = self.root / "gtest.xml"
        source.write_text(f'''<testsuites><testsuite name="Sample">
            <testcase name="test" file="{self.root}/apps/components_tests/sample.cpp"/>
            </testsuite></testsuites>''')
        destination = self.root / "junit.xml"
        reports.write_junit(source, destination, self.root)
        case = ET.parse(destination).find("./testsuite/testcase")
        self.assertEqual(case.get("file"), "apps/components_tests/sample.cpp")
        self.assertEqual(case.get("classname"), "Sample")

    def test_junit_rejects_empty_reports(self):
        source = self.root / "gtest.xml"
        source.write_text("<testsuites/>")
        with self.assertRaises(ValueError):
            reports.write_junit(source, self.root / "junit.xml", self.root)

    def fake_run(self, command, cwd, test_exit=0, coverage_exit=0, xml=None):
        if command[0] == "gcovr":
            self.assertEqual((self.build / "counter.gcda").read_text(), "current suite")
            Path(command[command.index("--xml") + 1]).write_text("coverage")
            Path(command[command.index("--json") + 1]).write_text("trace")
            return subprocess.CompletedProcess(command, coverage_exit)
        self.assertFalse((self.build / "counter.gcda").exists(), "stale counters were not cleared")
        (self.build / "counter.gcda").write_text("current suite")
        output = Path(command[1].split("xml:", 1)[1])
        output.write_text(self.xml if xml is None else xml)
        return subprocess.CompletedProcess(command, test_exit)

    def test_suites_have_isolated_counters_reports_and_reusable_objects(self):
        (self.build / "counter.gcda").write_text("old run")
        for extension in ("gcno", "o"):
            (self.build / f"keep.{extension}").write_text("keep")
        with patch.object(reports.subprocess, "run", side_effect=self.fake_run):
            for suite in reports.SUITES:
                self.assertEqual(reports.collect_suite(self.root, suite), 0)
                self.assertFalse(list(self.build.rglob("*.gcda")))
                self.assertTrue((self.root / "test-results" / f"{suite}.junit.xml").is_file())
                self.assertTrue((self.root / "coverage" / f"{suite}.json").is_file())
        for extension in ("gcno", "o"):
            self.assertEqual((self.build / f"keep.{extension}").read_text(), "keep")

    def test_suite_coverage_leaves_out_the_exception_branches_of_the_compiler(self):
        commands = []

        def record(command, cwd):
            commands.append(command)
            return self.fake_run(command, cwd)
        with patch.object(reports.subprocess, "run", side_effect=record):
            self.assertEqual(reports.collect_suite(self.root, "components"), 0)
        gcovr = [command for command in commands if command[0] == "gcovr"]
        self.assertEqual(len(gcovr), 1)
        # gcovr applies the flag while it reads gcov data; a merge of saved traces cannot add it later
        self.assertIn("--exclude-throw-branches", gcovr[0])

    def test_failing_tests_still_produce_junit_and_coverage_and_fail_the_job(self):
        with patch.object(reports.subprocess, "run", side_effect=lambda command, cwd:
                          self.fake_run(command, cwd, test_exit=7)):
            self.assertEqual(reports.collect_suite(self.root, "components"), 7)
        self.assertTrue((self.root / "test-results/components.junit.xml").is_file())
        self.assertTrue((self.root / "coverage/components.xml").is_file())
        self.assertFalse(list(self.build.rglob("*.gcda")))

    def test_malformed_junit_keeps_raw_xml_and_still_collects_coverage(self):
        with patch.object(reports.subprocess, "run", side_effect=lambda command, cwd:
                          self.fake_run(command, cwd, xml="broken")):
            self.assertEqual(reports.collect_suite(self.root, "openfallout"), 1)
        self.assertTrue((self.root / "test-results/openfallout.gtest.xml").is_file())
        self.assertFalse((self.root / "test-results/openfallout.junit.xml").exists())
        self.assertTrue((self.root / "coverage/openfallout.xml").is_file())

    def test_coverage_failure_discards_partial_reports_but_keeps_junit(self):
        with patch.object(reports.subprocess, "run", side_effect=lambda command, cwd:
                          self.fake_run(command, cwd, coverage_exit=3)):
            self.assertEqual(reports.collect_suite(self.root, "components"), 3)
        self.assertFalse((self.root / "coverage/components.xml").exists())
        self.assertFalse((self.root / "coverage/components.json").exists())
        self.assertTrue((self.root / "test-results/components.junit.xml").is_file())
        self.assertFalse(list(self.build.rglob("*.gcda")))

    def test_collection_exception_still_clears_counters(self):
        def failed_collection(command, cwd):
            if command[0] == "gcovr":
                raise OSError("gcovr unavailable")
            return self.fake_run(command, cwd)
        with patch.object(reports.subprocess, "run", side_effect=failed_collection):
            with self.assertRaises(OSError):
                reports.collect_suite(self.root, "components")
        self.assertFalse(list(self.build.rglob("*.gcda")))

    def test_combined_report_requires_every_suite_trace(self):
        with self.assertRaises(FileNotFoundError):
            reports.merge_coverage(self.root)
        (self.root / "coverage").mkdir()
        for suite in reports.SUITES:
            (self.root / "coverage" / f"{suite}.json").write_text("trace")
        with patch.object(reports.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertEqual(reports.merge_coverage(self.root), 0)
        command = run.call_args.args[0]
        self.assertEqual(command.count("--add-tracefile"), 2)
        self.assertNotIn("build", command, "merge must use saved traces, not cleared live counters")


    def test_merge_of_named_suites_needs_only_their_traces(self):
        (self.root / "coverage").mkdir()
        (self.root / "coverage" / "components.json").write_text("trace")
        with patch.object(reports.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertEqual(reports.merge_coverage(self.root, suites=("components",)), 0)
        self.assertEqual(run.call_args.args[0].count("--add-tracefile"), 1)
        with self.assertRaises(FileNotFoundError):
            reports.merge_coverage(self.root)

    def test_suites_run_from_another_build_folder_and_from_the_app_bundle(self):
        # The unit-test job keeps the instrumented test build in its own folder, and CMake puts executables
        # inside the app bundle on the Mac.
        tests = self.root / "build-tests"
        bundle = tests / "OpenFallout.app" / "Contents" / "MacOS"
        bundle.mkdir(parents=True)
        (bundle / "components-tests").touch()
        (tests / "counter.gcda").write_text("old run")
        commands = []

        def run(command, cwd):
            commands.append(command)
            if command[0] == "gcovr":
                Path(command[command.index("--xml") + 1]).write_text("coverage")
                Path(command[command.index("--json") + 1]).write_text("trace")
                return subprocess.CompletedProcess(command, 0)
            self.assertFalse((tests / "counter.gcda").exists(), "stale counters were not cleared")
            Path(command[1].split("xml:", 1)[1]).write_text(self.xml)
            return subprocess.CompletedProcess(command, 0)
        with patch.object(reports.subprocess, "run", side_effect=run):
            self.assertEqual(reports.collect_suite(self.root, "components", build_dir="build-tests"), 0)
        self.assertEqual(commands[0][0], str(bundle / "components-tests"))
        gcovr = commands[1]
        self.assertIn("build-tests", gcovr)
        self.assertIn("build-tests/", gcovr[gcovr.index("--exclude") + 1:])

    def test_missing_test_binary_is_an_error(self):
        (self.build / "components-tests").unlink()
        with self.assertRaises(FileNotFoundError):
            reports.collect_suite(self.root, "components")

    def test_clang_builds_use_source_based_coverage(self):
        # Apple's llvm-cov gcov does not mark exception exits, so gcovr cannot leave them out (see ci-reporting.md).
        self.assertEqual(reports.default_coverage_tool("darwin"), "llvm")
        self.assertEqual(reports.default_coverage_tool("linux"), "gcov")


    def test_command_line_names_the_build_folder_tool_and_suites(self):
        calls = []

        def record(name):
            return lambda root, *args, **kwargs: calls.append((name, args, kwargs)) or 0
        for argv in (["merge"], ["--build-dir", "build-tests", "--tool", "llvm", "merge", "components", "openfallout"],
                     ["--tool", "gcov", "run", "openfallout"]):
            with patch.object(reports.sys, "argv", ["test_reports.py", *argv]), \
                    patch.object(reports, "merge_coverage", side_effect=record("merge")), \
                    patch.object(reports, "collect_suite", side_effect=record("run")):
                self.assertEqual(reports.main(), 0)
        self.assertEqual(calls[0], ("merge", (), {"suites": tuple(reports.SUITES), "build_dir": "build",
                                                  "tool": reports.default_coverage_tool()}))
        self.assertEqual(calls[1], ("merge", (), {"suites": ("components", "openfallout"),
                                                  "build_dir": "build-tests", "tool": "llvm"}))
        self.assertEqual(calls[2], ("run", ("openfallout",), {"build_dir": "build", "tool": "gcov"}))
        for argv in (["merge", "bogus"], ["run", "editor"], ["merge", "components", "editor"]):
            with self.subTest(argv=argv), patch.object(reports.sys, "argv", ["test_reports.py", *argv]), \
                    patch.object(reports.sys, "stderr"), self.assertRaises(SystemExit):
                reports.main()

    def test_the_suites_are_the_two_the_mac_job_builds(self):
        # the editor is not built, so it has no suite and no coverage flag (codecov.yml)
        self.assertEqual(reports.SUITES, {"components": "components-tests", "openfallout": "openfallout-tests"})

LCOV = """SF:{root}/components/esm4/reader.cpp
FN:3,_ZN4ESM46Reader4readEv
DA:3,4
DA:4,4
DA:5,0
BRDA:4,0,0,4
BRDA:4,0,1,0
BRDA:5,1,0,-
BRDA:5,1,1,-
LF:3
LH:2
end_of_record
SF:{root}/components/esm4/../esm4/reader.hpp
DA:7,2
end_of_record
SF:{root}/extern/sol/sol.hpp
DA:1,1
end_of_record
SF:{root}/build-tests/_deps/googletest/gtest.h
DA:1,1
end_of_record
SF:{root}/apps/components_tests/esm4/test.cpp
DA:1,1
end_of_record
SF:/usr/include/c++/v1/vector
DA:1,1
end_of_record
"""


class LlvmCoverageTests(unittest.TestCase):
    """Clang's source-based coverage: raw profiles per suite, merged with llvm-profdata, read with llvm-cov."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.build = self.root / "build-tests"
        self.bundle = self.build / "OpenFallout.app" / "Contents" / "MacOS"
        self.bundle.mkdir(parents=True)
        for binary in ("components-tests", "openfallout-tests"):
            (self.bundle / binary).touch()
        self.commands = []
        self.lcov = LCOV.format(root=self.root)
        self.junit = '<testsuites><testsuite name="S"><testcase name="t" classname="S"/></testsuite></testsuites>'

    def fake_run(self, command, cwd, env=None, stdout=None, test_exit=0, merge_exit=0, export_exit=0):
        self.commands.append(command)
        if command[:2] == ["xcrun", "llvm-profdata"]:
            output = Path(command[command.index("-o") + 1])
            self.assertTrue(command[command.index("-o") + 2:], "no raw profiles to merge")
            output.write_text("profdata")
            return subprocess.CompletedProcess(command, merge_exit)
        if command[:2] == ["xcrun", "llvm-cov"]:
            stdout.write(self.lcov)
            return subprocess.CompletedProcess(command, export_exit)
        profiles = Path(env["LLVM_PROFILE_FILE"]).parent
        self.assertFalse(list(profiles.glob("*.profraw")), "stale raw profiles were not cleared")
        Path(env["LLVM_PROFILE_FILE"].replace("%p", "123")).write_text("raw")
        Path(command[1].split("xml:", 1)[1]).write_text(self.junit)
        return subprocess.CompletedProcess(command, test_exit)

    def collect(self, suite="components", **outcome):
        with patch.object(reports.subprocess, "run", side_effect=lambda command, cwd, **kwargs:
                          self.fake_run(command, cwd, **kwargs, **outcome)):
            return reports.collect_suite(self.root, suite, build_dir="build-tests", tool="llvm")

    def lines(self, report):
        return {(cls.get("filename"), int(line.get("number"))): line
                for cls in ET.parse(report).getroot().iter("class") for line in cls.iter("line")}

    def test_suite_writes_cobertura_of_project_sources_only(self):
        stale = self.build / "profiles"
        stale.mkdir()
        (stale / "old.profraw").write_text("old run")
        self.assertEqual(self.collect(), 0)
        lines = self.lines(self.root / "coverage" / "components.xml")
        self.assertEqual({filename for filename, _ in lines},
                         {"components/esm4/reader.cpp", "components/esm4/reader.hpp"})
        self.assertEqual(lines["components/esm4/reader.cpp", 3].attrib, {"number": "3", "hits": "4", "branch": "false"})
        self.assertEqual(lines["components/esm4/reader.cpp", 4].get("condition-coverage"), "50% (1/2)")
        self.assertEqual(lines["components/esm4/reader.cpp", 5].get("hits"), "0")
        self.assertEqual(lines["components/esm4/reader.cpp", 5].get("condition-coverage"), "0% (0/2)")
        root = ET.parse(self.root / "coverage" / "components.xml").getroot()
        self.assertEqual((root.get("lines-valid"), root.get("lines-covered")), ("4", "3"))
        self.assertEqual((root.get("branches-valid"), root.get("branches-covered")), ("4", "1"))
        self.assertTrue((self.root / "coverage" / "components.profdata").is_file())
        self.assertTrue((self.root / "test-results" / "components.junit.xml").is_file())
        self.assertFalse(list(self.build.rglob("*.profraw")))
        export = self.commands[-1]
        self.assertEqual(export[:3], ["xcrun", "llvm-cov", "export"])
        self.assertIn(str(self.bundle / "components-tests"), export)

    def test_failing_tests_still_produce_reports_and_fail_the_job(self):
        self.assertEqual(self.collect(test_exit=5), 5)
        self.assertTrue((self.root / "coverage" / "components.xml").is_file())
        self.assertTrue((self.root / "test-results" / "components.junit.xml").is_file())

    def test_coverage_failure_discards_partial_reports(self):
        self.assertEqual(self.collect(export_exit=2), 2)
        self.assertFalse((self.root / "coverage" / "components.xml").exists())
        self.assertFalse((self.root / "coverage" / "components.profdata").exists())
        self.assertTrue((self.root / "test-results" / "components.junit.xml").is_file())
        self.assertFalse(list(self.build.rglob("*.profraw")))

    def test_merge_combines_the_suite_profiles_and_reads_every_test_binary(self):
        coverage = self.root / "coverage"
        coverage.mkdir()
        with patch.object(reports.subprocess, "run", side_effect=self.fake_run):
            with self.assertRaises(FileNotFoundError):
                reports.merge_coverage(self.root, suites=("components", "openfallout"), build_dir="build-tests",
                                       tool="llvm")
            for suite in ("components", "openfallout"):
                (coverage / f"{suite}.profdata").write_text("profdata")
            self.assertEqual(reports.merge_coverage(self.root, suites=("components", "openfallout"),
                                                    build_dir="build-tests", tool="llvm"), 0)
        merge, export = self.commands
        self.assertIn(str(coverage / "components.profdata"), merge)
        self.assertIn(str(coverage / "openfallout.profdata"), merge)
        self.assertIn(str(self.bundle / "components-tests"), export)
        self.assertEqual(export[export.index("-object") + 1], str(self.bundle / "openfallout-tests"))
        self.assertIn(("components/esm4/reader.cpp", 4), self.lines(self.root / "coverage.xml"))


if __name__ == "__main__":
    unittest.main()
