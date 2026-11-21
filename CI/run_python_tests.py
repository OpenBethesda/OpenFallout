#!/usr/bin/env python3
"""Runs the Python test files of one or more folders as parallel processes, one process per file, and proves that every test ran.

`python3 -m unittest discover -s DIR -p 'test_*.py'` runs the files one after the other on one core. This runs each file
with its own `unittest` process (the same command, narrowed to one file), as many at a time as the computer has cores, and
prints the output of each file whole when it ends, so the lines of two files never mix.

Nothing is hidden by the split:
  * the tests of each folder are counted first by loading them (the same discovery the plain command does), per file;
  * a file whose process ran another number of tests than were counted, a file that did not run, and a process that failed
    all fail the run, each by name;
  * every failing file's complete output is printed again at the end, and the exit status is 1 if anything failed.

Each folder is `DIR` (run with this Python) or `DIR=PYTHON` (run with another, such as a virtual environment's).
    python3 CI/run_python_tests.py CI "scripts/openfallout=$VENV/bin/python"
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time
from pathlib import Path

PATTERN = "test_*.py"
COUNT_PROGRAM = """
import collections, sys, unittest
counts = collections.Counter()
def walk(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            walk(item)
        else:
            counts[item.__class__.__module__] += 1
            if item.__class__.__name__ in ("_FailedTest", "ModuleImportFailure", "LoadTestsFailure"):
                counts[item.__class__.__module__ + " (FAILED TO LOAD)"] += 1
walk(unittest.defaultTestLoader.discover(sys.argv[1], pattern=sys.argv[2], top_level_dir=sys.argv[1]))
for module, count in sorted(counts.items()):
    print(f"{count}\\t{module}")
"""
RAN = re.compile(r"^Ran (\d+) tests? in ", re.MULTILINE)
SKIPPED = re.compile(r"\bskipped=(\d+)")


def parse_folder(text):
    folder, _, python = text.partition("=")
    return folder, python or sys.executable


def expected_counts(folder, python):
    """{module: number of tests} as unittest discovery finds them, run with the Python that will run the tests."""
    result = subprocess.run([python, "-c", COUNT_PROGRAM, folder, PATTERN], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"could not count the tests of {folder}:\n{result.stdout}{result.stderr}")
    counts = {}
    for line in result.stdout.splitlines():
        number, _, module = line.partition("\t")
        counts[module] = int(number)
    return counts


def run_file(folder, python, name):
    started = time.monotonic()
    command = [python, "-m", "unittest", "discover", "-s", folder, "-t", folder, "-p", name, "-v"]
    process = subprocess.run(command, capture_output=True, text=True, errors="replace")
    output = process.stdout + process.stderr
    ran = RAN.search(output)
    skipped = SKIPPED.search(output)
    return {"folder": folder, "file": name, "module": name[:-3], "status": process.returncode, "output": output,
            "ran": int(ran.group(1)) if ran else None, "skipped": int(skipped.group(1)) if skipped else 0,
            "seconds": time.monotonic() - started}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folders", nargs="+", metavar="DIR[=PYTHON]")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 1, help="processes at a time (default: the cores)")
    args = parser.parse_args(argv)

    started = time.monotonic()
    jobs, expected, problems = [], {}, []
    for text in args.folders:
        folder, python = parse_folder(text)
        counts = expected_counts(folder, python)
        for name in sorted(path.name for path in Path(folder).glob(PATTERN)):
            module = name[:-3]
            expected[(folder, module)] = counts.pop(module, 0)
            jobs.append((folder, python, name))
        for module in counts:                                   # tests that discovery found in no file of the folder
            problems.append(f"{folder}: discovery found tests in {module}, which is not a {PATTERN} file of the folder")
    # the biggest files first, so the last ones to start are the small ones
    jobs.sort(key=lambda job: -expected[(job[0], job[2][:-3])])
    print(f"{len(jobs)} test files, {sum(expected.values())} tests, {args.workers} at a time", flush=True)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        for future in concurrent.futures.as_completed([pool.submit(run_file, *job) for job in jobs]):
            result = future.result()
            results.append(result)
            print(f"::group::{result['folder']}/{result['file']}: {result['ran']} tests, {result['seconds']:.1f} s, "
                  f"exit {result['status']}")
            print(result["output"], end="" if result["output"].endswith("\n") else "\n")
            print("::endgroup::", flush=True)

    results.sort(key=lambda result: (result["folder"], result["file"]))
    failed = []
    for result in results:
        want = expected[(result["folder"], result["module"])]
        label = f"{result['folder']}/{result['file']}"
        if result["ran"] is None:
            problems.append(f"{label}: the process printed no test count (exit {result['status']})")
        elif result["ran"] != want:
            problems.append(f"{label}: ran {result['ran']} tests, discovery counted {want}")
        if want == 0 and result["ran"] == 0 and result["status"] in (0, 5):
            # no tests ran: a script that only has a test_ name (CI/test_reports.py), which the plain command imports and
            # counts for nothing. unittest of Python 3.12 and later exits 5 for it, Python 3.11 and earlier exit 0 ("Ran 0
            # tests ... OK"), so both are this. Said out loud, below; a file that fails to load is not this
            print(f"note: {label} has no tests (discovery counted 0, the process ran 0)")
        elif result["status"] != 0:
            problems.append(f"{label}: exit status {result['status']}")
            failed.append(result)

    print("\n" + "file".ljust(58) + "tests  skipped  seconds")
    for result in results:
        print(f"{result['folder'] + '/' + result['file']:58}{result['ran'] if result['ran'] is not None else '-':>5}"
              f"{result['skipped']:>9}{result['seconds']:>9.1f}")
    ran = sum(result["ran"] or 0 for result in results)
    skipped = sum(result["skipped"] for result in results)
    print(f"{'total':58}{ran:>5}{skipped:>9}{time.monotonic() - started:>9.1f} (wall)")
    print(f"tests counted by discovery: {sum(expected.values())}; tests run: {ran}; files run: {len(results)} of {len(jobs)}")

    if problems or failed:
        for result in failed:
            print(f"\n===== FAILED: {result['folder']}/{result['file']} =====\n{result['output']}")
        print("\nFAILED:")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print("OK: every test file ran, every counted test ran, none failed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
