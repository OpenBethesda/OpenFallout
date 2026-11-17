"""What the Mac workflow (macos.yml) must keep promising, read from its text.

The workflow cannot run here, and these tests need no YAML library (the CI scripts use only the standard library), so
they read the steps by their indentation. They hold the rules that are easy to break by an edit that looks harmless:
the Metal build (a second build configuration, spec "Mac Native", milestone two) must never be able to break the
packaged app or a release, its steps must say plainly when they run, and every script the workflow names must exist.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"

METAL = "Metal build: "


def read_steps(text):
    """The steps of the one job of macos.yml: a list of dicts with the step's lines and its name, id, if and run."""
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if line == "    steps:")
    blocks, current = [], None
    for line in lines[start + 1:]:
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
            "if": fields.get("if", "").strip(), "run": fields.get("run", ""), "uses": fields.get("uses", ""),
            "fields": fields}


class MacWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = (WORKFLOWS / "macos.yml").read_text()
        cls.steps = read_steps(cls.text)
        cls.metal = [step for step in cls.steps if step["name"].startswith(METAL)]

    def index(self, name):
        names = [step["name"] for step in self.steps]
        self.assertIn(name, names)
        return names.index(name)

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
        """Release.yml calls the workflow with build-type Release; the one switch is off there, and every Metal step
        reads it, so each is skipped (a skipped step does not fail the job and the release steps after it still run)."""
        switch = [step for step in self.steps if "METAL_BUILD=true" in step["run"]]
        self.assertEqual(len(switch), 1)
        self.assertEqual(switch[0]["if"], "${{ inputs.build-type != 'Release' }}")
        for step in self.metal:
            with self.subTest(step=step["name"]):
                self.assertIn("env.METAL_BUILD == 'true'", step["if"])
        self.assertNotIn("metal", (WORKFLOWS / "release.yml").read_text().lower())

    def test_a_metal_step_never_runs_after_a_cancel_and_never_hides_its_own_failure(self):
        for step in self.metal:
            with self.subTest(step=step["name"]):
                self.assertTrue(step["if"].startswith("${{ !cancelled() &&"), step["if"])
                self.assertNotIn("always()", step["if"])
                self.assertNotIn("continue-on-error", step["text"])

    def test_every_metal_step_waits_for_the_step_it_needs(self):
        needs = {
            "fingerprint OpenSceneGraph for OpenGL ES 3": ["steps.build.outcome == 'success'"],
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

    def test_the_metal_steps_come_after_everything_that_makes_or_publishes_the_app(self):
        first = min(self.steps.index(step) for step in self.metal)
        before = ("Package disk image", "Notarize and staple", "Check the packaged app", "Store dmg",
                  "Upload components coverage to Codecov", "Upload combined coverage to Codecov",
                  "Upload test results to Codecov Test Analytics")
        for name in before:
            with self.subTest(step=name):
                self.assertLess(self.index(name), first)
        # the cache trim still runs after every tree, and the release is made after all of it
        self.assertLess(max(self.steps.index(step) for step in self.metal), self.index("Limit stored compiler cache"))
        self.assertLess(self.index("Limit stored compiler cache"), self.index("Create prerelease"))

    def test_nothing_that_signs_packages_or_releases_looks_at_the_metal_build(self):
        names = ("Release signing", "Package disk image", "Notarize and staple", "Remove the signing keychain",
                 "Check the packaged app", "Write the engine-release statement", "Store dmg", "Create prerelease")
        for name in names:
            with self.subTest(step=name):
                step = self.steps[self.index(name)]
                self.assertNotIn("metal", step["text"].lower())

    def test_the_metal_tree_is_neither_packaged_nor_uploaded_nor_cached(self):
        for step in self.steps:
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

    def test_one_fetch_of_the_asset_serves_the_manual_bundle_run_and_the_metal_build(self):
        fetches = [step for step in self.steps if "angle_asset.py fetch" in step["run"]]
        self.assertEqual(len(fetches), 2)
        bundle = next(step for step in fetches if not step["name"].startswith(METAL))
        metal = next(step for step in fetches if step["name"].startswith(METAL))
        self.assertEqual(bundle["if"], "${{ inputs.bundle-angle }}")
        # the two use one folder, named once for the job, and the Metal fetch does not fetch into a folder that the
        # bundle run's fetch filled (angle_asset.py refuses a folder that is not empty)
        self.assertIn('fetch "$ANGLE_ROOT_PATH"', bundle["run"])
        self.assertIn('fetch "$ANGLE_ROOT_PATH"', metal["run"])
        self.assertIn('[ -f "$ANGLE_ROOT_PATH/lib/libGLESv2.dylib" ]', metal["run"])
        configure = next(step for step in self.steps if step["name"] == "Configure")
        self.assertIn('-A "$ANGLE_ROOT_PATH"', configure["run"])
        self.assertNotIn("RUNNER_TEMP/angle", self.text)
        self.assertIn("ANGLE_ROOT_PATH: /tmp/openfallout-angle", self.text)

    def test_the_opengl_ES_3_cache_follows_the_inputs_of_the_script_that_names_them(self):
        key = next(step for step in self.metal if step["name"].endswith("fingerprint OpenSceneGraph for OpenGL ES 3"))
        self.assertIn("CI/macos/osg/osg_gles3.py key", key["run"])
        self.assertIn("--input CI/macos/angle/angle.json", key["run"])
        self.assertIn('--input "$RUNNER_TEMP/openmw-manifest.txt"', key["run"])
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

    def test_every_script_the_workflows_name_exists(self):
        for workflow in sorted(WORKFLOWS.glob("*.yml")):
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
        for workflow in sorted(WORKFLOWS.glob("*.yml")):
            with self.subTest(workflow=workflow.name):
                self.assertNotIn("continue-on-error", workflow.read_text())


if __name__ == "__main__":
    unittest.main()
