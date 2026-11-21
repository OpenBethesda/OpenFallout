"""What the Mac workflow (macos.yml) must keep promising, read from its text.

The workflow cannot run here, and these tests need no YAML library (the CI scripts use only the standard library), so
they read the jobs and steps by their indentation. They hold the rules that are easy to break by an edit that looks
harmless: the workflow is four jobs that run side by side (the packaged app, the unit tests, the Metal build, the Python
tests) with no `needs` between them; each decides for itself whether a documentation-only change skips it; the Metal
build (a second build configuration, spec "Mac Native", milestone two) must never be able to break the packaged app or a
release, its steps must say plainly when they run, each job that compiles has a compiler cache of its own, and every
script the workflow names must exist.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"
ACTIONS = ROOT / ".github" / "actions"

METAL = "Metal build: "
GATE = "steps.classify.outputs.code != 'false'"


def read_jobs(text):
    """The jobs of macos.yml, by id: a dict with the job's name, its job-level if and needs, and its steps."""
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if line == "jobs:")
    jobs, current = {}, None
    for line in lines[start + 1:]:
        header = re.match(r"^  ([A-Za-z][A-Za-z0-9_-]*):\s*$", line)
        if header:
            current = {"id": header.group(1), "lines": []}
            jobs[current["id"]] = current
        elif current is not None:
            current["lines"].append(line)
    for job in jobs.values():
        job["fields"] = job_fields(job["lines"])
        job["name"] = job["fields"].get("name", "").strip("'\"")
        job["if"] = job["fields"].get("if", "").strip()
        job["steps"] = read_steps(job["lines"])
    return jobs


def job_fields(lines):
    """Keys at the job's own indentation (4 spaces), before its steps."""
    fields, key = {}, None
    for line in lines:
        if line == "    steps:":
            break
        top = re.match(r"^    ([a-z-]+):\s*(.*)$", line)
        if top:
            key = top.group(1)
            fields[key] = top.group(2)
        elif key and line.startswith("      "):
            fields[key] += "\n" + line.strip()
    return fields


def read_steps(lines):
    """The steps of one job: a list of dicts with the step's lines and its name, id, if and run. Comment lines are left
    out: they describe the step that follows, and a word in one must not count as the step's own."""
    start = next(i for i, line in enumerate(lines) if line == "    steps:")
    blocks, current = [], None
    for line in lines[start + 1:]:
        if line.strip().startswith("#"):
            continue
        if line.startswith("    - "):
            current = [line]
            blocks.append(current)
        elif current is not None:
            current.append(line)
    return [parse_step(block) for block in blocks]


def parse_step(block):
    """Keys at the step's own indentation (6 spaces; the first line also starts with '- '). A value can continue on
    deeper-indented lines (the folded `if: >-` form)."""
    first = "      " + block[0][6:]
    lines = [first] + block[1:]
    fields, key = {}, None
    for line in lines:
        top = re.match(r"^      ([a-z-]+):\s*(.*)$", line)
        if top:
            key = top.group(1)
            fields[key] = top.group(2)
        elif key and line.startswith("        "):
            fields[key] += "\n" + line.strip()
    return {"text": "\n".join(block), "name": fields.get("name", "").strip("'\""), "id": fields.get("id", ""),
            "if": " ".join(fields.get("if", "").split()), "run": fields.get("run", ""), "uses": fields.get("uses", ""),
            "fields": fields}


def gated(steps, step):
    """Whether a documentation-only change skips the step: it names the classification, or it needs the success of a
    step that is gated (a skipped step has the outcome 'skipped', so the rest of its chain is skipped with it)."""
    if GATE in step["if"]:
        return True
    by_id = {other["id"]: other for other in steps if other["id"]}
    for needed in re.findall(r"steps\.([a-z-]+)\.outcome == 'success'", step["if"]):
        if needed in by_id and gated(steps, by_id[needed]):
            return True
    return False


class MacWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = (WORKFLOWS / "macos.yml").read_text()
        cls.jobs = read_jobs(cls.text)
        cls.steps = cls.jobs["MacOS"]["steps"]  # the packaged app
        cls.tests = cls.jobs["UnitTests"]["steps"]
        cls.metal = [step for step in cls.jobs["Metal"]["steps"] if step["name"].startswith(METAL)]
        cls.metal_job = cls.jobs["Metal"]["steps"]
        cls.python = cls.jobs["PythonMac"]["steps"]

    def index(self, name):
        names = [step["name"] for step in self.steps]
        self.assertIn(name, names)
        return names.index(name)

    # ----------------------------------------------------------------------------------------------- the four jobs

    def test_four_jobs_run_side_by_side_with_the_names_their_checks_have(self):
        self.assertEqual({job_id: job["name"] for job_id, job in self.jobs.items()}, {
            "MacOS": "Build, test and package",
            "UnitTests": "Unit tests and coverage",
            "Metal": "Metal build",
            "PythonMac": "Python tests on macOS",
        })
        # no job waits for another: a slow or failed Metal build must not delay the package, nor the package the tests
        self.assertNotIn("needs", {key for job in self.jobs.values() for key in job["fields"]})
        self.assertNotIn("needs:", self.text)

    def test_the_unit_tests_and_the_metal_build_never_run_on_a_release_tag_the_app_and_the_python_tests_always_may(self):
        for job_id in ("UnitTests", "Metal"):
            with self.subTest(job=job_id):
                self.assertEqual(self.jobs[job_id]["if"], "${{ inputs.build-type != 'Release' }}")
        for job_id in ("MacOS", "PythonMac"):
            with self.subTest(job=job_id):
                self.assertEqual(self.jobs[job_id]["if"], "")

    def test_the_environment_and_the_signing_secrets_belong_to_the_app_job_only(self):
        self.assertEqual(self.jobs["MacOS"]["fields"]["environment"], "${{ inputs.signing-environment }}")
        for job_id in ("UnitTests", "Metal", "PythonMac"):
            with self.subTest(job=job_id):
                self.assertNotIn("environment", self.jobs[job_id]["fields"])
                self.assertNotIn("MACOS_DEVELOPER_ID", "\n".join(step["text"] for step in self.jobs[job_id]["steps"]))

    def test_every_job_decides_for_itself_whether_a_documentation_only_change_skips_it(self):
        for job_id, job in self.jobs.items():
            steps = job["steps"]
            with self.subTest(job=job_id):
                self.assertEqual(steps[0]["uses"], "actions/checkout@v7")
                classify = steps[1]
                self.assertEqual(classify["id"], "classify")
                # a release tag always builds: the step is skipped there and its empty output is not 'false'
                self.assertEqual(classify["if"], "${{ inputs.build-type != 'Release' }}")
                self.assertIn('python3 CI/changed_paths.py --event "$EVENT" --base "$BASE" --github-output "$GITHUB_OUTPUT"',
                              classify["run"])
                self.assertIn("EVENT: ${{ github.event_name }}", classify["text"])
                self.assertIn("BASE: ${{ github.event.pull_request.base.sha || github.event.before }}", classify["text"])
                for step in steps[2:]:
                    if step["name"] == "Remove the signing keychain":
                        # always runs: setup can fail before it sets its output, and cleanup does nothing without a keychain
                        self.assertEqual(step["if"], "${{ always() }}")
                    elif step["id"] in ("compiler-cache",) or step["name"] == "Limit stored compiler cache":
                        self.assertTrue(GATE in step["if"] or "steps.compiler-cache.outcome == 'success'" in step["if"],
                                        step["name"])
                    else:
                        self.assertTrue(gated(steps, step), f"{step['name']!r} is not gated by the classification")

    def test_the_later_steps_of_a_job_that_start_from_a_chain_never_skip_the_chain(self):
        # a step with `!cancelled()` or `always()` runs after a failure, so each names the step it needs
        for steps in (self.tests, self.metal_job):
            for step in steps[2:]:
                if "!cancelled()" in step["if"] and GATE not in step["if"]:
                    with self.subTest(step=step["name"]):
                        self.assertRegex(step["if"], r"steps\.[a-z-]+\.outcome == 'success'")

    def test_the_jobs_that_compile_share_one_setup_written_once_and_each_has_a_compiler_cache_of_its_own(self):
        keys = {}
        for job_id, key in (("MacOS", "macos-26-app-${{ inputs.build-type }}"), ("UnitTests", "macos-26-coverage"),
                            ("Metal", "macos-26-metal")):
            steps = self.jobs[job_id]["steps"]
            with self.subTest(job=job_id):
                setup = next(step for step in steps if step["id"] == "setup")
                self.assertEqual(setup["uses"], "./.github/actions/mac-build-setup")
                cache = next(step for step in steps if step["id"] == "compiler-cache")
                self.assertEqual(cache["uses"], "hendrikmuhs/ccache-action@v1.2.25")
                self.assertIn("install: 'no'", cache["text"])
                self.assertIn(f"key: {key}", cache["text"])
                self.assertIn("max-size: 1000M", cache["text"])
                self.assertIn("save: ${{ inputs.build-type == 'Release' || github.ref == 'refs/heads/master' }}",
                              cache["text"])
                limit = steps[-1] if job_id != "MacOS" else next(s for s in steps if s["name"] == "Limit stored compiler cache")
                self.assertEqual(limit["name"], "Limit stored compiler cache")
                self.assertEqual(limit["if"], "always() && steps.compiler-cache.outcome == 'success'")
                self.assertEqual(limit["run"].split("\n")[1:], ["ccache --max-size=1000M", "ccache --cleanup"])
                keys[job_id] = key
        self.assertEqual(len(set(keys.values())), 3)
        # the pinned tools are named in the one action, not in the jobs
        action = (ACTIONS / "mac-build-setup" / "action.yml").read_text()
        for pin in ("CCACHE_VERSION: 4.14.1", "c279fa81e2e806b4d9e64d4a06cb569a84ef54151feef796e5d4f4e95a464563",
                    "CI/macos/fetch_deps_manifest.sh", "CI/before_install.macos.sh", "actions/cache@v6"):
            self.assertIn(pin, action)
        for pin in ("CCACHE_VERSION", "fetch_deps_manifest", "before_install.macos.sh", "actions/cache@"):
            self.assertNotIn(pin, self.text)
        self.assertNotIn("setup", {step["id"] for step in self.python})

    def test_the_python_tests_need_nothing_that_the_other_jobs_build(self):
        names = [step["name"] for step in self.python[2:]]
        self.assertEqual(names, ["Name the Python version for the wheel cache", "Restore the wheels of the Python test packages",
                                 "Install the Python test packages", "Keep the wheels of the Python test packages",
                                 "Run the Python tests on macOS"])
        # every test file of both folders runs, in parallel, through the runner that counts them
        run = self.python[6]["run"]
        self.assertIn("python3 CI/run_python_tests.py CI \"scripts/openfallout=$RUNNER_TEMP/script-tests/bin/python\"", run)
        self.assertNotIn("build", run.replace("unittest", ""))
        self.assertNotIn("--workers", run)          # the cores of the runner, not a number written here
        # the wheels of lupa and pillow are cached, and built from source when the cache misses
        install = self.python[4]["run"]
        self.assertIn("pip\" wheel --wheel-dir", install)
        self.assertIn("lupa==2.8 pillow==12.0.0", install)

    def test_the_package_step_runs_cpack_on_the_tree_the_build_step_finished(self):
        # `make package` builds `all` again before CPack; the Build step has built it, so the walk is skipped
        build = self.steps[self.index("Build")]
        package = self.steps[self.index("Package disk image")]
        self.assertEqual(build["run"], "CI/macos/build.sh all")
        self.assertEqual(package["run"], "cpack --config ./CPackConfig.cmake")
        self.assertNotIn("make ", package["text"])
        self.assertIn("working-directory: build\n", package["text"])

    def test_the_app_job_keeps_everything_that_makes_checks_or_publishes_the_app(self):
        names = [step["name"] for step in self.steps]
        for name in ("Configure", "Build", "Record source timestamps", "Save incremental build",
                     "Check that the core has no Apple code", "Release signing", "Package disk image",
                     "Notarize and staple", "Remove the signing keychain", "Check the packaged app",
                     "Test the engine-release tooling", "Write the engine-release statement",
                     "Store the packaged-app check logs", "Store dmg", "Limit stored compiler cache", "Create prerelease"):
            self.assertIn(name, names)
        # nothing of the unit-test tree, the coverage reports or the Metal build is left in it
        for step in self.steps:
            with self.subTest(step=step["name"]):
                self.assertNotIn("UNIT_TESTS", step["text"])
                self.assertNotIn("METAL_BUILD", step["text"])
                self.assertNotIn("build-tests", step["text"])
                self.assertNotIn("codecov", step["text"].lower())

    def test_the_unit_test_job_keeps_the_tests_the_coverage_and_every_upload(self):
        names = [step["name"] for step in self.tests]
        for name in ("Test coverage and JUnit report collection", "Fingerprint unit-test build environment",
                     "Restore incremental unit-test build", "Configure unit tests", "Build unit tests",
                     "Run components tests", "Run OpenFallout tests", "Merge suite coverage reports",
                     "Save incremental unit-test build", "Keep coverage reports", "Keep test results",
                     "Upload test results to Codecov Test Analytics", "Upload components coverage to Codecov",
                     "Upload OpenFallout coverage to Codecov", "Upload combined coverage to Codecov"):
            self.assertIn(name, names)
        # it no longer waits for the app build: nothing names the app's steps or the switches of the single job
        text = "\n".join(step["text"] for step in self.tests)
        for old in ("steps.build.outcome", "UNIT_TESTS", "METAL_BUILD"):
            self.assertNotIn(old, text)
        # a fork or Dependabot pull request has no secrets: the four uploads keep their rule
        uploads = [step for step in self.tests if step["uses"].startswith("codecov/codecov-action@")]
        self.assertEqual(len(uploads), 4)
        for step in uploads:
            with self.subTest(step=step["name"]):
                self.assertIn("github.event.pull_request.head.repo.full_name == github.repository", step["if"])
                self.assertIn("github.actor != 'dependabot[bot]'", step["if"])
                self.assertIn("!cancelled()", step["if"])
                self.assertIn("secrets.CODECOV_TOKEN", step["text"])

    # ------------------------------------------------------------------------------------------------ the Metal build

    def test_the_metal_steps_exist_in_the_order_the_build_needs(self):
        names = [step["name"][len(METAL):] for step in self.metal]
        self.assertEqual(names, [
            "fingerprint OpenSceneGraph for OpenGL ES 3",
            "restore OpenSceneGraph for OpenGL ES 3",
            "fetch the ANGLE libraries",
            "build OpenSceneGraph for OpenGL ES 3",
            "keep OpenSceneGraph for OpenGL ES 3",
            "configure",
            "compile",
            "run components tests",
            "run OpenFallout tests",
        ])

    def test_a_release_tag_run_cannot_run_a_metal_step(self):
        """Release.yml calls the workflow with build-type Release; the Metal job's own `if` is off there, so none of its
        steps starts, and the app job has no Metal step at all, so the release steps in it still run."""
        self.assertEqual(self.jobs["Metal"]["if"], "${{ inputs.build-type != 'Release' }}")
        for job_id, job in self.jobs.items():
            if job_id != "Metal":
                for step in job["steps"]:
                    with self.subTest(job=job_id, step=step["name"]):
                        self.assertFalse(step["name"].startswith(METAL))
                        self.assertNotIn("build-metal", step["text"])
                        self.assertNotIn("METAL", step["text"])
        self.assertNotIn("metal", (WORKFLOWS / "release.yml").read_text().lower())
        # the old switches are gone: the job's own condition replaces them
        self.assertNotIn("METAL_BUILD", self.text)
        self.assertNotIn("UNIT_TESTS", self.text)

    def test_a_metal_step_never_runs_after_a_cancel_and_never_hides_its_own_failure(self):
        for step in self.metal:
            with self.subTest(step=step["name"]):
                self.assertTrue(step["if"].startswith("${{ !cancelled() &&"), step["if"])
                self.assertNotIn("always()", step["if"])
                self.assertNotIn("continue-on-error", step["text"])

    def test_every_metal_step_waits_for_the_step_it_needs(self):
        needs = {
            "fingerprint OpenSceneGraph for OpenGL ES 3": ["steps.setup.outcome == 'success'"],
            "restore OpenSceneGraph for OpenGL ES 3": ["steps.metal-key.outcome == 'success'"],
            "fetch the ANGLE libraries": ["steps.metal-key.outcome == 'success'"],
            "build OpenSceneGraph for OpenGL ES 3": ["steps.metal-osg-cache.outputs.cache-hit != 'true'",
                                                     "steps.metal-angle.outcome == 'success'"],
            "keep OpenSceneGraph for OpenGL ES 3": ["steps.metal-osg-build.outcome == 'success'"],
            "configure": ["steps.metal-angle.outcome == 'success'", "steps.metal-osg-cache.outputs.cache-hit == 'true'",
                          "steps.metal-osg-build.outcome == 'success'"],
            "compile": ["steps.metal-configure.outcome == 'success'"],
            "run components tests": ["steps.metal-build.outcome == 'success'"],
            "run OpenFallout tests": ["steps.metal-build.outcome == 'success'"],
        }
        for step in self.metal:
            short = step["name"][len(METAL):]
            for condition in needs[short]:
                with self.subTest(step=short, condition=condition):
                    self.assertIn(condition, step["if"])
        # the Metal build does not wait for the OpenGL build, which is another job now
        self.assertNotIn("steps.build.outcome", "\n".join(step["text"] for step in self.metal_job))

    def test_the_metal_job_needs_the_dependency_set_but_nothing_the_opengl_build_makes(self):
        # `before_script.macos.sh -M` reads /tmp/openmw-deps and /tmp/Qt (the dependency cache), ANGLE and the OpenGL ES 3
        # OpenSceneGraph (both made by steps of this job); it never reads build/
        names = [step["name"] for step in self.metal_job]
        self.assertIn("Set up the Mac build", names)
        for step in self.metal_job:
            with self.subTest(step=step["name"]):
                self.assertNotIn("build/", step["run"].replace("build-metal/", ""))
        self.assertLess(names.index("Set up the Mac build"), names.index(METAL + "configure"))

    def test_nothing_that_signs_packages_or_releases_looks_at_the_metal_build(self):
        names = ("Release signing", "Package disk image", "Notarize and staple", "Remove the signing keychain",
                 "Check the packaged app", "Write the engine-release statement", "Store dmg", "Create prerelease")
        for name in names:
            with self.subTest(step=name):
                step = self.steps[self.index(name)]
                self.assertNotIn("metal", step["text"].lower())

    def test_the_metal_tree_is_neither_packaged_nor_uploaded_nor_cached(self):
        for job in self.jobs.values():
            for step in job["steps"]:
                if step["uses"].startswith(("actions/cache", "actions/upload-artifact", "codecov/", "softprops/")):
                    with self.subTest(step=step["name"]):
                        self.assertNotIn("build-metal", step["text"])
        for step in self.metal:
            self.assertNotIn("package", step["run"].split("build.sh")[-1] if "build.sh" in step["run"] else "")
            self.assertNotIn("coverage", step["run"].lower())

    def test_the_metal_tests_start_the_two_suites_of_the_metal_tree_and_nothing_else(self):
        runs = {step["name"][len(METAL):]: step["run"] for step in self.metal}
        self.assertEqual(runs["run components tests"], "build-metal/OpenFallout.app/Contents/MacOS/components-tests")
        self.assertEqual(runs["run OpenFallout tests"], "build-metal/OpenFallout.app/Contents/MacOS/openfallout-tests")
        self.assertIn("-M", runs["configure"].split())

    def test_each_job_that_needs_the_asset_fetches_it_into_the_one_named_folder(self):
        fetches = [(job_id, step) for job_id, job in self.jobs.items() for step in job["steps"]
                   if "angle_asset.py fetch" in step["run"]]
        self.assertEqual(sorted(job_id for job_id, _ in fetches), ["MacOS", "Metal"])
        bundle = next(step for job_id, step in fetches if job_id == "MacOS")
        metal = next(step for job_id, step in fetches if job_id == "Metal")
        self.assertIn("inputs.bundle-angle", bundle["if"])
        # the two jobs run on runners of their own, so the folder is empty in each (angle_asset.py refuses a folder that
        # is not empty); the folder is named once per job
        self.assertIn('fetch "$ANGLE_ROOT_PATH"', bundle["run"])
        self.assertIn('fetch "$ANGLE_ROOT_PATH"', metal["run"])
        configure = next(step for step in self.steps if step["name"] == "Configure")
        self.assertIn('-A "$ANGLE_ROOT_PATH"', configure["run"])
        self.assertNotIn("RUNNER_TEMP/angle", self.text)
        self.assertEqual(self.text.count("ANGLE_ROOT_PATH: /tmp/openfallout-angle"), 2)

    def test_the_opengl_ES_3_cache_follows_the_inputs_of_the_script_that_names_them(self):
        key = next(step for step in self.metal if step["name"].endswith("fingerprint OpenSceneGraph for OpenGL ES 3"))
        self.assertIn("CI/macos/osg/osg_gles3.py key", key["run"])
        self.assertIn("--input CI/macos/angle/angle.json", key["run"])
        self.assertIn('--input "${{ steps.setup.outputs.manifest-path }}"', key["run"])
        # the script that chooses the build's folders and arguments, and the compiler that builds it
        self.assertIn("--input CI/macos/before_install_osg.sh", key["run"])
        self.assertIn('clang --version > "$RUNNER_TEMP/clang-version.txt"', key["run"])
        self.assertIn('--input "$RUNNER_TEMP/clang-version.txt"', key["run"])
        # a helper that fails must fail the step, not leave an empty key: `echo "key=$(...)"` would hide it
        self.assertNotIn('echo "key=$(', key["run"])
        restore = next(step for step in self.metal if step["name"].endswith("restore OpenSceneGraph for OpenGL ES 3"))
        self.assertTrue(restore["uses"].startswith("actions/cache/restore@"))
        self.assertIn("steps.metal-key.outputs.key", restore["fields"]["with"] + restore["text"])
        # the install is saved as soon as it is built: a later failure must not throw the ten minutes away
        keep = next(step for step in self.metal if step["name"].endswith("keep OpenSceneGraph for OpenGL ES 3"))
        self.assertTrue(keep["uses"].startswith("actions/cache/save@"))
        self.assertIn("steps.metal-osg-cache.outputs.cache-primary-key", keep["text"])

    def test_the_manifest_path_the_action_gives_is_where_it_writes_the_manifest(self):
        action = (ACTIONS / "mac-build-setup" / "action.yml").read_text()
        self.assertIn('CI/macos/fetch_deps_manifest.sh "$RUNNER_TEMP/openmw-manifest.txt"', action)
        self.assertIn('echo "path=$RUNNER_TEMP/openmw-manifest.txt" >> "$GITHUB_OUTPUT"', action)

    # ------------------------------------------------------------------------------------------- the callers

    def test_no_job_of_push_yml_waits_for_the_changes_job_and_the_draft_rule_stays_on_the_mac_job(self):
        push = (WORKFLOWS / "push.yml").read_text()
        self.assertNotIn("needs:", push)
        self.assertNotIn("outputs:", push)
        caller = push.split("\n  MacOS:\n", 1)[1]
        self.assertIn("    if: ${{ !github.event.pull_request.draft }}\n", caller)
        self.assertIn("    uses: ./.github/workflows/macos.yml\n", caller)
        # the concurrency rule is unchanged
        self.assertIn("cancel-in-progress: ${{ github.event_name == 'pull_request' }}", push)

    def test_every_script_the_workflows_name_exists(self):
        for workflow in sorted([*WORKFLOWS.glob("*.yml"), *ACTIONS.glob("*/action.yml")]):
            for name in sorted(set(re.findall(r"\bCI/[A-Za-z0-9_./-]+\.(?:py|sh|json|patch)\b", workflow.read_text()))):
                with self.subTest(workflow=workflow.name, script=name):
                    self.assertTrue((ROOT / name).is_file(), f"{workflow.name} names {name}, which is not in the repository")

    def test_the_old_names_of_the_angle_scripts_are_gone(self):
        for path in [*WORKFLOWS.glob("*.yml"), *(ROOT / "CI").rglob("*.sh"), *(ROOT / "docs" / "openfallout").glob("*.md")]:
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertNotIn("fetch_angle", text)
                self.assertNotIn("angle_version.sh", text)

    def test_no_workflow_lets_a_step_fail_quietly(self):
        for workflow in sorted([*WORKFLOWS.glob("*.yml"), *ACTIONS.glob("*/action.yml")]):
            with self.subTest(workflow=workflow.name):
                self.assertNotIn("continue-on-error", workflow.read_text())


if __name__ == "__main__":
    unittest.main()


class MergeQueueTest(unittest.TestCase):
    """Every workflow that checks a pull request also runs for the merge queue, so the full checks run on the commit
    that lands on master, not only on the pull request's own head."""

    def test_pull_request_workflows_listen_to_the_merge_queue(self):
        for name in ("push.yml", "documentation.yml", "docs.yml"):
            with self.subTest(workflow=name):
                text = (WORKFLOWS / name).read_text()
                on = text[text.index("\non:\n"):text.index("\npermissions:")]
                self.assertIn("\n  pull_request:", on)
                self.assertRegex(on, r"\n  merge_group:\n(  [a-z_]+:|$)")

    def test_the_change_classifier_builds_everything_in_the_queue(self):
        import sys
        sys.path.insert(0, str(ROOT / "CI"))
        import changed_paths
        self.assertEqual(changed_paths.decide("merge_group", "0" * 40)[0], True)
