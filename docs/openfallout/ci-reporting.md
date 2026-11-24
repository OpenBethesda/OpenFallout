# Codecov coverage flags and Test Analytics

The Mac jobs of `.github/workflows/macos.yml` build and test the engine: the packaged app (check name
`macOS / Build, test and package`), the unit tests (`macOS / Unit tests and coverage`), the Metal build
(`macOS / Metal build`) and the Python tests (`macOS / Python tests on macOS`). They run side by side, and no
other job builds or tests the engine. The unit-test job runs the components and OpenFallout test suites and uploads
their coverage and test results. Each suite produces its own coverage report and JUnit test
results. The existing `CODECOV_TOKEN` Actions secret authenticates both coverage and Test Analytics uploads;
`push.yml` passes it to the Mac jobs.

| Flag | Executable | Coverage report | Test results |
| --- | --- | --- | --- |
| `components` | `components-tests` | `coverage/components.xml` | `test-results/components.junit.xml` |
| `openfallout` | `openfallout-tests` | `coverage/openfallout.xml` | `test-results/openfallout.junit.xml` |

The flags measure which production code each test suite exercises. They are
not source-directory filters: shared production code can appear under several
flags. Test sources, third-party dependencies, and generated build files are
excluded from coverage.

The editor is not built, so it has no suite and the `editor` flag ended when
the uploads moved from the Ubuntu job, which no longer exists, to the Mac unit-test job.

## The unit-test job

The packaged app is built RelWithDebInfo, in its own job, and is never instrumented. The tests
get a second build tree in a job of their own (`UnitTests`, "Unit tests and coverage"), `build-tests`, configured by
`CI/before_script.macos.sh -T`: Debug without debug information (`-O0`, no
`-g`), `BUILD_WITH_CODE_COVERAGE=ON` and the two test suites. Only `components-tests` and
`openfallout-tests` are built in it. The launcher stays configured because it
turns on Qt, which the launcher-settings tests in `components-tests` need. The
tree has its own incremental build cache, `build-macos-coverage-v2-…`, and
its own ccache (see [CI build cache contract](ci-cache.md)). The job does not wait for
the app build, so a packaging or notarization failure does not hide the test results,
and a test failure does not hold up the package.

Every run of the Mac jobs for a branch or pull request runs them. A release tag
(`build-type: Release`) does not: a tag is made from a commit whose branch run
already tested it, and a second upload for the same commit would be counted
twice by Codecov.

## The instrumented app

A manual run of `Build and test` with the input `coverage-app` (`gh workflow run push.yml -f coverage-app=true --ref
<branch>`) also calls `coverage-app.yml`: the packaged app, RelWithDebInfo, configured by `CI/before_script.macos.sh -P`
(`BUILD_WITH_CODE_COVERAGE=ON`, tree `build-coverage-app`), as the artifact `openfallout-macos-coverage-dmg-arm64-<sha>`.
It is for measuring which engine code played rounds reach: run the rounds on it with `LLVM_PROFILE_FILE` pointing into
the round folder (for example `<folder>/%p-%m.profraw`), then `xcrun llvm-profdata merge` the profiles and
`xcrun llvm-cov report` them against `OpenFallout.app/Contents/MacOS/openfallout`. Only a clean exit writes a profile.
The image is ad hoc signed, never notarized, checked or released. Pull requests, pushes, the merge queue and release
tags never build it, and it neither restores nor stores a build cache.

## Merge queue

`Build and test` (push.yml), `Documentation` and `Documentation links` also run on `merge_group`, the event of
GitHub's merge queue. In the queue nothing is skipped by path: `CI/changed_paths.py` builds everything for any event
other than `push` and `pull_request`, and `Documentation` has no path filter for this event. The queue itself (a
ruleset on `master` with required checks) is a repository setting, switched on by the owner.

## The Metal build's tests

A third job, `Metal` ("Metal build"), builds a third tree, `build-metal` (the Metal build, see
[Two build configurations](architecture.md#two-build-configurations)), and runs
the same two test suites in it, from the repository root. They are not
instrumented and their results are **not uploaded**: the steps are pass or fail
and the log has the details. The reasons: the test sources are the same, so a
second coverage report would count every line twice, and a second set of test
results would give every test case two series in Test Analytics, with the
failure rate and flakiness history of one build mixed into the other's. The
coverage and the test history stay those of the desktop OpenGL build. If the
suites of the two builds ever differ (a test that only the Metal build runs),
that test needs its own report and its own flag, not a shared one.

These steps are one block named "Metal build: ..." in a job of their own, so a
failure there fails that job's check but never hides a report or delays the package.
A release tag does not run it.

## How coverage is measured

`BUILD_WITH_CODE_COVERAGE` instruments the targets through
`openfallout_enable_coverage` in `cmake/OpenFalloutMacros.cmake`, which chooses
the method by compiler:

- **Clang (the Mac)** uses source-based coverage (`-fprofile-instr-generate
  -fcoverage-mapping`). Each suite writes raw profiles into
  `build-tests/profiles/`, `llvm-profdata` merges them into
  `coverage/<suite>.profdata`, and `llvm-cov export` reads that with the test
  executable. `CI/test_reports.py` turns the result into a Cobertura report in
  the same shape gcovr writes. A branch here is a condition in the source, so
  the reports count no exception exits.
- **GCC** writes gcov counters, which gcovr 8.3 reads into
  `coverage/<suite>.xml` and a JSON trace, `coverage/<suite>.json`. No CI job
  builds with GCC since the Ubuntu job was removed; `CI/test_reports.py` keeps
  the path for a local GCC build.

Clang's gcov-style counters (`--coverage`) were not used: they count the exit
that the compiler adds to every call that may throw as a branch, and Apple's
`llvm-cov gcov` does not mark those exits, so gcovr cannot leave them out.
Almost no test takes them, and Codecov counts a line with an untaken branch as
partial, which is not covered.

`CI/test_reports.py` clears the raw counters (`.profraw` or `.gcda`) before and
after each suite and keeps the compiled objects, which separates the reports
even when the build was restored from a cache. It finds a test executable in the
build folder or, on the Mac, in the app bundle inside it. `merge` joins the
saved per-suite traces into `coverage.xml`, which is uploaded without a flag and
kept as an artifact. This combined upload preserves complete branch data:
two suites may exercise opposite sides of a branch, while each separate report
only describes partial coverage. The combined upload does not affect the
individual flags.

On GCC, gcovr runs with `--exclude-throw-branches`. GCC adds the same hidden
exception exit to every call that may throw, and gcov reports it as a branch.
gcovr drops these branches whether or not a test took them, so the reports no
longer count the exception exits of any call; the lines of a `catch` block still
count as lines. gcovr applies the flag while it reads the gcov data, so it
belongs on the per-suite run; the merge of the saved traces cannot add it later.

The two compilers do not count lines the same way, so the Codecov numbers moved
when the uploads moved from Ubuntu to the Mac. Compare numbers from the same
job only.

GoogleTest XML is normalized to JUnit, preserving test names, durations,
failure details, and skipped/disabled tests. Source paths inside the checkout
become repository-relative paths. Raw GoogleTest reports are retained alongside
the JUnit files in the `macos-test-results` artifact. Coverage reports and the
per-suite profiles are retained in `macos-coverage`.

A failed test still fails the job. The remaining test suites run, and available
coverage and test results are uploaded even after a test failure. Cancelled runs
stop reporting. Fork and Dependabot PRs keep their artifacts without using the
repository upload secret.

`codecov.yml` lists the two flags, disables carryforward, and waits for all
three coverage uploads before producing the coverage notification and PR comment.
There are no per-flag coverage thresholds. Every status it defines is informational,
so Codecov shows the numbers and cannot turn a PR red: the project total, a patch
status for new code under `components/` (target 80%, which the 5% threshold lets
pass down to 75%), a patch status for `apps/` and a project status for each component. It also ignores the Fallout 4 archive
readers (`components/bsa/ba2*`). The targets
behind this, and how to turn the statuses into gates, are in
[Test coverage targets](coverage-targets.md). Test Analytics needs successive
uploads to establish timing, failure-rate, and flakiness history.

## Source components

The Components view groups production source files independently of the test
suite flags. The component IDs in `codecov.yml` are stable so coverage history
can continue across display-name changes. `shared_libraries` was split into
`rules`, `loaders` and `render_helpers`, so the history of that one ID ends at
the split.

| Component | Source paths |
| --- | --- |
| Fallout rules (`rules`) | `components/falloutscript/`, `components/falloutmenu/` |
| Formats and loaders (`loaders`) | ESM, NIF, BSA, VFS, file, navigation, script and other data code under `components/` |
| Rendering, GUI and audio helpers (`render_helpers`) | scene graph, shaders, terrain, widgets, SDL and other helpers under `components/` |
| OpenFallout engine (`engine`) | `apps/openfallout/` |
| Launcher (`desktop_ui`) | `apps/launcher/` |
| Data and navigation tools (`tools`) | BSA, Bullet, ESM, navigation and NIF tools under `apps/` |

These groups cover the production files in the coverage report without
overlapping one another, except for the ignored files named above. They are
included in PR comments. To see a change to this configuration in the
default-branch view, merge the PR and allow its `master` coverage run to
finish. Codecov may offer **Enable Component Analytics** to backfill
historical component trends.

## Python tests

The `Changes` job of `push.yml` runs on a Linux runner and runs every Python test
on every pull request and every push to `master`, including the documentation-only
changes that skip the Mac builds: the tests of the CI scripts (`CI/test_*.py`),
of the helper scripts (`scripts/openfallout/test_*.py`) and of the Lua
documentation generator (`docs/test_luadoc.py`). The tests that need macOS
skip themselves there: those of the packaged-app check and the bundle selection
(`codesign`, `hdiutil`, `vtool`), the top CMake file's refusal of Intel, and the
whole runs of the round runner (`round.py run` works on macOS only). The Mac job `PythonMac`
("Python tests on macOS") runs the
tests of the CI scripts and of the helper scripts again, on macOS, so those run
too (`macos.yml`, "Run the Python tests on macOS"). None of these tests reads the built app or the disk image, so the
job needs no build. A documentation-only change
skips every step of the Mac jobs, and it changes nothing those tests cover: a change to the round
runner (`round.py`, `roundguard.py`, `test_round.py`) always gets the Mac jobs
(`CI/changed_paths.py`), and so does any change to `CI/`. Each Mac job runs `CI/changed_paths.py` itself as its
first step; none waits for the `Changes` job.

The `Changes` job and the two documentation jobs (`links`, `HTML`) were on Mac
runners from the Mac-only cut until 16 November 2026. That day GitHub's hosted
Mac runners were short and the jobs of pull requests waited for hours, so the
jobs that need no Mac went back to Linux runners. The engine is built and
tested on a Mac only.

The helper-script tests read the Lua scripts and the coverage tracker, so they
check that those still agree. The `Changes` job installs a `luajit` executable
with apt, and `lupa` and Pillow in a virtual environment (pinned in the workflow,
so Dependabot does not update them); without them the Lua tests and one picture
test of the round runner skip. The `PythonMac` job installs `lupa` and Pillow the same
way and no `luajit` executable, so the two tests that need one run in the
`Changes` job only. lupa's wheels
for Apple Silicon have no LuaJIT runtime, so the tests use its Lua 5.2 runtime
there, which accepts what LuaJIT accepts in these scripts. To run them locally:

```sh
python3 -m unittest discover -s CI -p 'test_*.py'
python3 -m venv .venv-tests && .venv-tests/bin/pip install lupa==2.8 pillow==12.0.0
.venv-tests/bin/python -m unittest discover -s scripts/openfallout -p 'test_*.py'
python3 -m unittest discover -s docs -p test_luadoc.py
```

The Mac job runs the same two folders through `CI/run_python_tests.py`, one process per test file and as many at a time as
the computer has cores. It counts the tests of each file by loading them first and fails if a file did not run, ran another
number of tests, or failed (the output of a failing file is printed again at the end):

```sh
python3 CI/run_python_tests.py CI "scripts/openfallout=.venv-tests/bin/python"
```

On the Mac, with an instrumented test tree in `build-tests/` (as the unit-test job
configures it), the same reporting commands can be run locally. They use
`xcrun llvm-profdata` and `xcrun llvm-cov` from Xcode and need no gcovr:

```sh
python3 CI/test_reports.py --build-dir build-tests run components --shards 3
python3 CI/test_reports.py --build-dir build-tests run openfallout
python3 CI/test_reports.py --build-dir build-tests merge components openfallout
```

`--shards N` (Clang coverage only) runs the suite as N parallel processes with GoogleTest's own
sharding (`GTEST_TOTAL_SHARDS`, `GTEST_SHARD_INDEX`); the CI job uses 3, one per core of the runner.
Each shard has its own `TMPDIR` and its own `.profraw` files, which are all merged. The shard
reports are merged into one `components.gtest.xml` and one JUnit file. The step fails if any shard
fails (each failing shard is named with its exit code), or if the tests in the shard reports are
not exactly the tests `--gtest_list_tests` names, each once.

With an instrumented GCC build in `build/` and gcovr 8.3 on `PATH`, `run` and
`merge` without `--build-dir` read gcov counters. `--tool gcov` or `--tool llvm`
overrides the choice by platform.

References: [Codecov flags](https://docs.codecov.com/docs/flags),
[Test Analytics](https://docs.codecov.com/docs/test-analytics),
[gcovr trace merging](https://gcovr.com/en/8.3/guide/merging.html), and
[Clang source-based coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html).

## Dependabot

`.github/dependabot.yml` checks GitHub Actions, `docs/requirements.txt`, and the
Dockerfile under `docs/` (a local documentation build; CI builds the documentation
without Docker) every Monday at 09:00 America/New_York.
GitHub Actions updates are grouped into one PR, including major versions.
Documentation Python updates are also grouped so related Sphinx dependencies
are validated together. Sphinx 9 and Docutils 0.22 and later are held back until
the OpenMW theme supports them. Docker minor and patch updates remain grouped.
Each ecosystem permits up to five open version-update PRs. Dependabot starts
using this configuration after it reaches the default branch.

CMake FetchContent pins and packages installed by shell commands, such as the
inline gcovr version, still need manual updates. The current Docker base images
use floating tags, so they may not produce version-update PRs until pinned.

Every test failure that is fixed is recorded, with its kind, in the [test failure log](test-failure-log.md).
