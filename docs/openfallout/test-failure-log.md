# Test failure log

Every test failure that someone fixes goes here, with what kind it was. The point is evidence for which tests earn
their keep:

- **Caught regression**: the code under test was wrong, and the fix changed that code.
- **Change detector**: the code was right; the test pinned a detail (an exact string, a step name, a layout, an order)
  and the fix only edited the test to match an intended change.
- **Test bug**: the test itself was wrong (a stand-in, an isolation leak, a platform gap) and the fix changed only the
  test.
- **Flaky**: it failed and then passed with no relevant change. A flaky test has a bug; record the cause once found.

A test file that only ever fails as a change detector is a candidate for deletion, and its pattern for a ban. Tests of
authorization (code signing, Team IDs, entitlements), privacy, save-game or data migration, and concurrency are never
deleted, whatever their record.

Add a row when you fix a failure: date, the test, the class, and the commit or pull request that fixed it.

## 7 to 21 November 2026

All 35 failed CI runs of those two weeks, read from the GitHub Actions logs, and Codecov's test results (it has the
C++ tests since their JUnit upload began; it does not have the Python tests).

| Class | Runs | What |
|---|---|---|
| Not a test (runner, network, cache, CodeQL set-up) | 24 | downloads and DNS 4, CodeQL set-up 4, runner lost 1, runs cancelled with no step run 11, runs with no failed job 4 |
| Caught regression | 4 | below; all builds or the smoke run, none a unit-test assertion |
| Test bug | 4 | below |
| Flaky | 2 | below; one cause found and fixed |
| Change detector | 1 | below; on a branch built to be thrown away |

### Caught regressions

| Date | Where it failed | What was wrong | Fixed by |
|---|---|---|---|
| 14 Nov | Linux and Windows builds, PRs #196 and #197 | `apps/openfallout/mwmechanics/falloutactors.hpp` lacked `#include <vector>` | 4712f41afd |
| 17 Nov | The first Metal job, PR #249 | the Metal build configured against the desktop OpenSceneGraph | c7e5433945, with `CI/test_metal_include_order.py` |
| 20 Nov | The packaged-app smoke run, PR #265 | a launcher bug planted on purpose to test the "can we skip the build?" check; the game did not start | experiment, closed |

### Test bugs

| Date | Test | Cause | Fixed by |
|---|---|---|---|
| 12 and 13 Nov | `CI/test_check_mac_package.py` (29 tests, Intel runner only, PR #189) | the stand-in signed the launcher before the nested app | 3b7e449c28 |
| 13 Nov | `test_check_mac_package.ReleaseModeTests.test_item_of_another_team_fails`, on master 514553cd | the check read the real Team ID that master's signing step had set (the log shows it masked as `***`), not the stand-in's | #194 (2d3f4f2546): the tests drop the signing variables |
| 14 Nov | `testworldcommands.cpp`, `testworldrules.cpp` (MSVC, PR #195) | `M_PI` is not standard | df35a555a4, `std::numbers::pi` |
| 21 Nov | 9 tests in `apps/components_tests/esm3/testdialogue.cpp`, one in `testsavegamegaps.cpp` | an id keeps the spelling it was first seen with in the process, so the result depended on the tests that ran earlier; found when the components tests first ran as three processes | PR #268 |

### Flaky

| Date | Test | Cause | Fixed by |
|---|---|---|---|
| 20 and 21 Nov | `DetourNavigatorAsyncNavMeshUpdaterTest.repeated_tile_updates_should_be_delayed` (concurrency) | the test waited on the real clock; a slow runner let the delay pass | #267: an injected clock |

### Change detectors

| Date | Test | What it pins |
|---|---|---|
| 20 Nov | `CI/test_workflows.py`, `test_the_jobs_that_compile_share_one_setup_written_once_and_each_has_a_compiler_cache_of_its_own` | the exact condition under which the Mac jobs store their compiler caches. It failed only on a measuring branch that changed that condition on purpose and was never merged; on any other branch the same failure would stop pull requests from pushing master's caches out of the 10 GB budget. |

**Outcome**: no test file failed only as a change detector, so none is deleted and no pattern is banned.
`CI/test_workflows.py` pins workflow structure and is edited whenever the workflows change, which is its cost; its
record so far is one guard working as intended.

Gaps: GitHub keeps only some runs (a re-run that passed replaces the failed one in the list); one job's log was gone
(the annotation says the runner lost contact); Codecov does not see the Python tests.
