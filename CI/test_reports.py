#!/usr/bin/env python3
"""Collect isolated coverage and JUnit results for each unit-test suite.

GCC builds write gcov counters, which gcovr reads. Clang builds (the Mac) use Clang's source-based coverage, which
llvm-profdata and llvm-cov read; the reports have the same Cobertura format either way (docs/openfallout/ci-reporting.md).
"""

import argparse
import copy
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


SUITES = {
    "components": "components-tests",
    "openfallout": "openfallout-tests",
}

# On the Mac, CMake writes every executable into the app bundle.
BINARY_FOLDERS = (Path("."), Path("OpenFallout.app") / "Contents" / "MacOS")

# Apple's command-line tools are reached through xcrun.
LLVM = ("xcrun",)


def default_coverage_tool(platform=sys.platform):
    # Apple's llvm-cov gcov does not mark the exception exit that Clang adds to every call that may throw, so gcovr
    # cannot leave those branches out; Clang's source-based coverage has no such branches.
    return "llvm" if platform == "darwin" else "gcov"


def find_binary(build, suite):
    for folder in BINARY_FOLDERS:
        binary = build / folder / SUITES[suite]
        if binary.is_file():
            return binary
    raise FileNotFoundError(f"Test binary does not exist: {build / SUITES[suite]}")


def excluded(path, build_dir):
    """Test sources, third-party code and generated build files are not production code."""
    return (path.startswith(("extern/", f"{build_dir}/"))
            or re.match(r"apps/[^/]*_tests/", path) is not None)


def clear_counters(build):
    # Preserve compiled objects and .gcno metadata for incremental builds.
    for path in build.rglob("*.gcda"):
        path.unlink()


def write_junit(source, destination, root):
    """Normalize GoogleTest's XML, including disabled tests, to JUnit XML."""
    original = ET.parse(source).getroot()
    report = ET.Element("testsuites")
    totals = {key: 0 for key in ("tests", "failures", "errors", "skipped")}
    for original_suite in original.findall("testsuite"):
        suite = ET.SubElement(report, "testsuite", {
            key: original_suite.get(key, default) for key, default in (
                ("name", ""), ("time", "0"), ("timestamp", ""))
        })
        counts = dict.fromkeys(totals, 0)
        for original_case in original_suite.findall("testcase"):
            attributes = {key: original_case.get(key, default) for key, default in (
                ("name", ""), ("classname", original_suite.get("name", "")), ("time", "0"))}
            if original_case.get("file"):
                path = Path(original_case.get("file"))
                if path.is_absolute() and path.is_relative_to(root):
                    path = path.relative_to(root)
                attributes["file"] = path.as_posix()
            case = ET.SubElement(suite, "testcase", attributes)
            for child in original_case:
                if child.tag in ("failure", "error", "skipped", "properties", "system-out", "system-err"):
                    case.append(copy.deepcopy(child))
            if (original_case.get("status") == "notrun"
                    or original_case.get("result") == "suppressed") and case.find("skipped") is None:
                ET.SubElement(case, "skipped", {"message": "Disabled in GoogleTest"})
            counts["tests"] += 1
            for tag, key in (("failure", "failures"), ("error", "errors"), ("skipped", "skipped")):
                counts[key] += int(case.find(tag) is not None)
        suite.attrib.update({key: str(value) for key, value in counts.items()})
        for key, value in counts.items():
            totals[key] += value
    if not totals["tests"]:
        raise ValueError("GoogleTest did not report any test cases")
    report.attrib.update({key: str(value) for key, value in totals.items()})
    report.set("time", original.get("time", "0"))
    ET.indent(report)
    ET.ElementTree(report).write(destination, encoding="utf-8", xml_declaration=True)


def read_lcov(source, root, build_dir):
    """Line hits and branch results per repository-relative file from an LCOV trace."""
    files = {}
    lines = branches = None
    for record in source:
        record = record.rstrip("\n")
        if record.startswith("SF:"):
            path = Path(os.path.normpath(record[3:]))
            lines = branches = None
            if path.is_absolute() and path.is_relative_to(root):
                relative = path.relative_to(root).as_posix()
                if not excluded(relative, build_dir):
                    lines, branches = files.setdefault(relative, ({}, {}))
        elif lines is None:
            continue
        elif record.startswith("DA:"):
            number, hits = record[3:].split(",")[:2]
            lines[int(number)] = lines.get(int(number), 0) + int(hits)
        elif record.startswith("BRDA:"):
            number, block, branch, taken = record[5:].split(",")
            key = (int(number), block, branch)
            branches[key] = branches.get(key, False) or (taken != "-" and int(taken) > 0)
    return files


def write_cobertura(files, destination):
    """Write a Cobertura report shaped like gcovr's, which Codecov reads."""
    def rate(covered, valid):
        return str(round(covered / valid, 4) if valid else 1.0)

    report = ET.Element("coverage")
    ET.SubElement(ET.SubElement(report, "sources"), "source").text = "."
    packages = ET.SubElement(report, "packages")
    totals = [0, 0, 0, 0]
    by_folder = {}
    for filename in sorted(files):
        by_folder.setdefault(filename.rpartition("/")[0], []).append(filename)
    for folder, filenames in by_folder.items():
        package = ET.SubElement(packages, "package", {"name": folder.replace("/", "."), "complexity": "0.0"})
        classes = ET.SubElement(package, "classes")
        package_totals = [0, 0, 0, 0]
        for filename in filenames:
            lines, branches = files[filename]
            per_line = {}
            for (number, _, _), taken in branches.items():
                counts = per_line.setdefault(number, [0, 0])
                counts[0] += int(taken)
                counts[1] += 1
            class_ = ET.SubElement(classes, "class", {
                "name": filename.rpartition("/")[2].replace(".", "_"), "filename": filename, "complexity": "0.0"})
            ET.SubElement(class_, "methods")
            line_elements = ET.SubElement(class_, "lines")
            counts = [0, 0, 0, 0]
            for number in sorted(lines):
                attributes = {"number": str(number), "hits": str(lines[number]), "branch": "false"}
                counts[0] += int(lines[number] > 0)
                counts[1] += 1
                if number in per_line:
                    taken, valid = per_line[number]
                    attributes["branch"] = "true"
                    attributes["condition-coverage"] = f"{taken * 100 // valid}% ({taken}/{valid})"
                    counts[2] += taken
                    counts[3] += valid
                ET.SubElement(line_elements, "line", attributes)
            class_.set("line-rate", rate(counts[0], counts[1]))
            class_.set("branch-rate", rate(counts[2], counts[3]))
            package_totals = [a + b for a, b in zip(package_totals, counts)]
        package.set("line-rate", rate(package_totals[0], package_totals[1]))
        package.set("branch-rate", rate(package_totals[2], package_totals[3]))
        totals = [a + b for a, b in zip(totals, package_totals)]
    report.attrib.update({
        "line-rate": rate(totals[0], totals[1]), "branch-rate": rate(totals[2], totals[3]),
        "lines-covered": str(totals[0]), "lines-valid": str(totals[1]),
        "branches-covered": str(totals[2]), "branches-valid": str(totals[3]),
        "complexity": "0.0", "version": "CI/test_reports.py"})
    ET.indent(report)
    ET.ElementTree(report).write(destination, encoding="utf-8", xml_declaration=True)


def export_cobertura(root, build_dir, profile, binaries, destination):
    """Read an indexed profile with llvm-cov and write the Cobertura report; return llvm-cov's exit code."""
    command = [*LLVM, "llvm-cov", "export", "-format=lcov", "-instr-profile", str(profile), str(binaries[0])]
    for binary in binaries[1:]:
        command.extend(("-object", str(binary)))
    with tempfile.TemporaryFile("w+", encoding="utf-8") as lcov:
        exported = subprocess.run(command, cwd=root, stdout=lcov)
        if exported.returncode:
            return exported.returncode
        lcov.seek(0)
        files = read_lcov(lcov, root, build_dir)
    write_cobertura(files, destination)
    print(f"{destination.name}: {sum(len(lines) for lines, _ in files.values())} lines in {len(files)} files")
    return 0


def run_tests(root, suite, binary, raw, junit, env=None):
    tests = subprocess.run([str(binary), f"--gtest_output=xml:{raw}"], cwd=root,
                           **({"env": env} if env is not None else {}))
    try:
        write_junit(raw, junit, root)
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"Could not generate {suite} JUnit results: {error}", file=sys.stderr)
        return tests.returncode or 1
    return tests.returncode


def report_paths(root, suite, *extensions):
    results = root / "test-results"
    coverage = root / "coverage"
    results.mkdir(exist_ok=True)
    coverage.mkdir(exist_ok=True)
    paths = (results / f"{suite}.gtest.xml", results / f"{suite}.junit.xml",
             *(coverage / f"{suite}.{extension}" for extension in extensions))
    for path in paths:
        path.unlink(missing_ok=True)
    return paths


def collect_suite_gcov(root, suite, build_dir, gcovr):
    build = root / build_dir
    binary = find_binary(build, suite)
    raw, junit, xml, trace = report_paths(root, suite, "xml", "json")
    clear_counters(build)
    try:
        test_status = run_tests(root, suite, binary, raw, junit)
        # Collect coverage even on test failure, so failure reports remain useful.
        collected = subprocess.run([
            gcovr, "--root", ".", build_dir,
            "--exclude", "extern/", "--exclude", f"{build_dir}/",
            "--exclude", "apps/[^/]*_tests/",
            "--merge-mode-functions", "merge-use-line-max",
            "--exclude-unreachable-branches",
            # The compiler adds a hidden exception exit to every call that may throw. Almost no test takes it, and
            # Codecov counts a line with an untaken branch as partial, not covered. gcovr drops these branches,
            # taken or not, only while it reads the gcov data, so the traces that the merge reads must be made
            # with this flag.
            "--exclude-throw-branches",
            "--gcov-ignore-parse-errors=negative_hits.warn_once_per_file",
            "-j", str(os.cpu_count() or 1),
            "--xml-pretty", "--xml", str(xml), "--json", str(trace), "--print-summary",
        ], cwd=root)
        if collected.returncode:
            # Never upload a partially written coverage report.
            xml.unlink(missing_ok=True)
            trace.unlink(missing_ok=True)
        return test_status or collected.returncode
    finally:
        clear_counters(build)


def clear_profiles(profiles):
    for path in profiles.glob("*.profraw"):
        path.unlink()


def collect_suite_llvm(root, suite, build_dir):
    build = root / build_dir
    binary = find_binary(build, suite)
    raw, junit, xml, profile = report_paths(root, suite, "xml", "profdata")
    # Raw profiles of one run; %p keeps the forks of death tests apart.
    profiles = build / "profiles"
    profiles.mkdir(exist_ok=True)
    clear_profiles(profiles)
    try:
        env = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / f"{suite}-%p.profraw"))
        test_status = run_tests(root, suite, binary, raw, junit, env)
        # Collect coverage even on test failure, so failure reports remain useful.
        status = subprocess.run([*LLVM, "llvm-profdata", "merge", "-sparse", "-o", str(profile),
                                 *map(str, sorted(profiles.glob("*.profraw")))], cwd=root).returncode
        if not status:
            status = export_cobertura(root, build_dir, profile, [binary], xml)
        if status:
            # Never upload a partially written coverage report.
            xml.unlink(missing_ok=True)
            profile.unlink(missing_ok=True)
        return test_status or status
    finally:
        clear_profiles(profiles)


def collect_suite(root, suite, gcovr="gcovr", build_dir="build", tool="gcov"):
    if tool == "llvm":
        return collect_suite_llvm(root, suite, build_dir)
    return collect_suite_gcov(root, suite, build_dir, gcovr)


def merge_coverage(root, gcovr="gcovr", suites=tuple(SUITES), build_dir="build", tool="gcov"):
    extension = "profdata" if tool == "llvm" else "json"
    traces = [root / "coverage" / f"{suite}.{extension}" for suite in suites]
    for trace in traces:
        if not trace.is_file():
            raise FileNotFoundError(f"Missing coverage trace: {trace}")
    if tool == "llvm":
        combined = root / "coverage" / "combined.profdata"
        status = subprocess.run([*LLVM, "llvm-profdata", "merge", "-sparse", "-o", str(combined),
                                 *map(str, traces)], cwd=root).returncode
        if not status:
            binaries = [find_binary(root / build_dir, suite) for suite in suites]
            status = export_cobertura(root, build_dir, combined, binaries, root / "coverage.xml")
        combined.unlink(missing_ok=True)
        return status
    command = [gcovr, "--root", ".", "--merge-mode-functions", "merge-use-line-max"]
    for trace in traces:
        command.extend(("--add-tracefile", str(trace)))
    command.extend(("--xml-pretty", "--output", "coverage.xml", "--print-summary"))
    return subprocess.run(command, cwd=root).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build", help="build folder of the test suites, inside the checkout")
    parser.add_argument("--tool", choices=("gcov", "llvm"), default=default_coverage_tool(),
                        help="gcov counters (GCC) or Clang source-based coverage; default by platform")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("run").add_argument("suite", choices=SUITES)
    # No choices= here: Python 3.11 rejects an empty list of positionals that have choices.
    merge = commands.add_parser("merge")
    merge.add_argument("suites", nargs="*", metavar="suite", help=f"suites to merge (default: all of {', '.join(SUITES)})")
    args = parser.parse_args()
    if args.command == "merge" and not set(args.suites) <= set(SUITES):
        merge.error(f"unknown suite: {', '.join(sorted(set(args.suites) - set(SUITES)))}")
    root = Path.cwd().resolve()
    try:
        if args.command == "run":
            return collect_suite(root, args.suite, build_dir=args.build_dir, tool=args.tool)
        return merge_coverage(root, suites=tuple(args.suites or SUITES), build_dir=args.build_dir, tool=args.tool)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
