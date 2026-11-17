"""The OpenGL ES 3 OpenSceneGraph build (CI/macos/osg/): what it builds, what its cache key follows, how it is recorded.

The build itself runs on a Mac and takes minutes; these tests hold what a reader of the CI log can observe: the
libraries and plugins it asks for (they come from the engine's own lists), the cache key that decides when the library
is built again, and the files that fix the version.
"""

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "CI" / "macos" / "osg"

spec = importlib.util.spec_from_file_location("osg_gles3", FOLDER / "osg_gles3.py")
osg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(osg)

CMAKELISTS = """\
set(USED_OSG_COMPONENTS
    osgAnimation
    osgDB
    osgGA
    osgFX
    osgParticle
    osgText
    osgUtil
    osgShadow
    osgSim
    osgViewer
    )
set(USED_OSG_PLUGINS
    osgdb_bmp
    osgdb_dae
    osgdb_dds
    osgdb_freetype
    osgdb_jpeg
    osgdb_osg
    osgdb_png
    osgdb_serializers_osg
    osgdb_tga)
"""


def write_tree(root, cmakelists=CMAKELISTS, patches=("0001-a.patch",)):
    """A small stand-in for the repository: the files the key reads."""
    folder = root / "CI" / "macos" / "osg"
    (folder / "patches").mkdir(parents=True)
    (root / "CMakeLists.txt").write_text(cmakelists)
    (folder / "osg_version.sh").write_text("OSG_COMMIT='" + "a" * 40 + "'\n")
    (folder / "build_osg_gles3.sh").write_text("#!/bin/bash -e\necho build\n")
    (folder / "osg_gles3.py").write_text("# the helper\n")
    for name in patches:
        (folder / "patches" / name).write_text("Why\n\ndiff --git a/x b/x\n")
    return root


class ListsFromTheEngine(unittest.TestCase):
    def test_reads_the_components_and_plugins_the_engine_names(self):
        lists = osg.engine_lists(CMAKELISTS)
        self.assertEqual(lists["components"][0], "osgAnimation")
        self.assertIn("osgViewer", lists["components"])
        self.assertIn("osgdb_png", lists["plugins"])
        self.assertEqual(len(lists["plugins"]), 9)

    def test_the_real_root_file_has_both_lists(self):
        lists = osg.engine_lists((ROOT / "CMakeLists.txt").read_text())
        self.assertTrue(lists["components"])
        self.assertTrue(lists["plugins"])

    def test_a_missing_list_is_an_error(self):
        with self.assertRaises(ValueError):
            osg.engine_lists("set(USED_OSG_COMPONENTS osgDB)")

    def test_libraries_the_engine_does_not_use_are_skipped(self):
        skipped = osg.skipped_libraries(osg.engine_lists(CMAKELISTS))
        self.assertEqual(
            skipped, ["osgManipulator", "osgPresentation", "osgTerrain", "osgUI", "osgVolume", "osgWidget"])

    def test_a_library_the_engine_needs_keeps_the_ones_it_links(self):
        # osgAnimation links osgViewer, osgText and osgGA; the dae plugin links osgSim and osgAnimation
        lists = {"components": ["osgDB", "osgAnimation"], "plugins": ["osgdb_png"]}
        skipped = osg.skipped_libraries(lists)
        for kept in ("osgAnimation", "osgViewer", "osgText", "osgGA", "osgUtil"):
            self.assertNotIn(kept, skipped)
        self.assertIn("osgSim", skipped)
        lists["plugins"].append("osgdb_dae")
        self.assertNotIn("osgSim", osg.skipped_libraries(lists))

    def test_an_unknown_component_is_an_error(self):
        with self.assertRaises(ValueError):
            osg.skipped_libraries({"components": ["osgDB", "osgNope"], "plugins": []})

    def test_plugins_become_cmake_options(self):
        options = osg.plugin_options(osg.engine_lists(CMAKELISTS))
        self.assertIn("-DBUILD_OSG_PLUGIN_PNG=ON", options)
        self.assertIn("-DBUILD_OSG_PLUGIN_DAE=ON", options)
        self.assertIn("-DBUILD_OSG_PLUGIN_OSG=ON", options)
        # the serializers are built with the libraries, they have no option of their own
        self.assertFalse([o for o in options if "SERIALIZERS" in o])
        self.assertEqual(len(options), 8)

    def test_an_unknown_plugin_is_an_error(self):
        with self.assertRaises(ValueError):
            osg.plugin_options({"components": [], "plugins": ["not_a_plugin"]})


class CacheKey(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = write_tree(Path(self.temporary.name))
        self.angle = self.root / "angle.json"
        self.angle.write_text("ANGLE_COMMIT='1f6526a566c27c7c33aa73a78517450a4a2a1714'\n")

    def key(self):
        return osg.cache_key(self.root, [self.angle])

    def test_the_same_inputs_give_the_same_key(self):
        self.assertEqual(self.key(), self.key())
        self.assertRegex(self.key(), r"^[0-9a-f]{64}$")

    def test_the_key_follows_each_input(self):
        base = self.key()
        folder = self.root / "CI" / "macos" / "osg"
        changes = {
            "commit": lambda: (folder / "osg_version.sh").write_text("OSG_COMMIT='" + "b" * 40 + "'\n"),
            "patch text": lambda: (folder / "patches" / "0001-a.patch").write_text("changed\n"),
            "new patch": lambda: (folder / "patches" / "0002-b.patch").write_text("new\n"),
            "script": lambda: (folder / "build_osg_gles3.sh").write_text("#!/bin/bash -e\necho other\n"),
            "helper": lambda: (folder / "osg_gles3.py").write_text("# other helper\n"),
            "ANGLE version": lambda: self.angle.write_text("ANGLE_COMMIT='2222222222222222222222222222222222222222'\n"),
            "engine plugin list": lambda: (self.root / "CMakeLists.txt").write_text(
                CMAKELISTS.replace("osgdb_tga)", "osgdb_tga\n    osgdb_ktx)")),
        }
        for name, change in changes.items():
            with self.subTest(name):
                change()
                changed = self.key()
                self.assertNotEqual(changed, base)
                base = changed

    def test_the_key_ignores_other_engine_files(self):
        base = self.key()
        (self.root / "CMakeLists.txt").write_text(CMAKELISTS + "\nmessage(STATUS other)\n")
        self.assertEqual(self.key(), base)

    def test_a_missing_input_is_an_error(self):
        with self.assertRaises(FileNotFoundError):
            osg.cache_key(self.root, [self.root / "missing.sh"])


class RecordedVersion(unittest.TestCase):
    def test_the_version_file_fixes_one_commit_and_one_archive(self):
        values = osg.read_version(FOLDER / "osg_version.sh")
        self.assertRegex(values["OSG_COMMIT"], r"^[0-9a-f]{40}$")
        self.assertRegex(values["OSG_ARCHIVE_SHA256"], r"^[0-9a-f]{64}$")
        self.assertEqual(values["OSG_REPOSITORY"], "https://github.com/OpenMW/osg")
        self.assertEqual(values["OSG_BRANCH"], "3.6")

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_shell_reads_the_same_values(self):
        values = osg.read_version(FOLDER / "osg_version.sh")
        shell = subprocess.run(
            ["bash", "-c", f'source "{FOLDER / "osg_version.sh"}"; echo "$OSG_COMMIT $OSG_ARCHIVE_SHA256"'],
            text=True, stdout=subprocess.PIPE, check=True).stdout.split()
        self.assertEqual(shell, [values["OSG_COMMIT"], values["OSG_ARCHIVE_SHA256"]])

    def test_every_patch_says_why_it_is_needed(self):
        patches = sorted((FOLDER / "patches").glob("*.patch"))
        self.assertTrue(patches)
        for patch in patches:
            with self.subTest(patch.name):
                self.assertRegex(patch.name, r"^\d{4}-[a-z0-9-]+\.patch$")
                text = patch.read_text()
                comment, separator, _ = text.partition("diff --git ")
                self.assertTrue(separator, "no diff in the patch")
                self.assertIn("Why this patch is needed", comment)
                self.assertGreater(len(comment.split()), 30, "the reason is too short to be one")

    def test_the_patch_numbers_are_in_order_without_gaps(self):
        numbers = [int(p.name[:4]) for p in sorted((FOLDER / "patches").glob("*.patch"))]
        self.assertEqual(numbers, list(range(1, len(numbers) + 1)))


class Scripts(unittest.TestCase):
    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_shell_scripts_parse(self):
        scripts = [FOLDER / "build_osg_gles3.sh", ROOT / "CI" / "before_script.macos.sh",
                   ROOT / "CI" / "before_install.macos.sh", ROOT / "CI" / "macos" / "before_install_osg.sh"]
        for script in scripts:
            with self.subTest(script.name):
                subprocess.run(["bash", "-n", str(script)], check=True)

    def test_the_build_script_pins_what_the_package_is_built_for(self):
        text = (FOLDER / "build_osg_gles3.sh").read_text()
        self.assertIn("26.0", text)
        self.assertIn("arm64", text)
        self.assertIn("GLES3", text)
        self.assertIn("OSG_SKIP_LIBRARIES", text)
        self.assertIn("-DOSG_KEEP_OPENGL_LIBRARY=ON", text)
        # and it checks its own result: nothing it installs may link Apple's OpenGL framework
        self.assertIn('grep -q "OpenGL.framework"', text)

    def test_the_build_script_wants_its_four_inputs(self):
        if not shutil.which("bash"):
            self.skipTest("needs bash")
        result = subprocess.run(["bash", str(FOLDER / "build_osg_gles3.sh")], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(result.returncode, 0)
        for name in ("--angle-root", "--prefix-path", "--work", "--install"):
            self.assertIn(name, result.stdout)

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_a_plugin_the_helper_does_not_know_stops_the_build_before_anything_is_fetched(self):
        # Without this the build went on with no plugin at all, and the install that came out of it was cached.
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            shutil.copytree(FOLDER, work / "repo" / "CI" / "macos" / "osg")
            text = (ROOT / "CMakeLists.txt").read_text()
            self.assertIn("osgdb_png", text)
            (work / "repo" / "CMakeLists.txt").write_text(text.replace("osgdb_png", "osgdb_png osgdb_unheard_of", 1))
            angle = work / "angle"
            for name in ("lib/libEGL.dylib", "lib/libGLESv2.dylib", "include/GLES3/gl3.h", "include/EGL/egl.h"):
                (angle / name).parent.mkdir(parents=True, exist_ok=True)
                (angle / name).write_text("")
            stand_ins = work / "bin"
            stand_ins.mkdir()
            for name in ("curl", "cmake"):
                (stand_ins / name).write_text(f'#!/bin/bash\necho "{name} was started" >&2\nexit 1\n')
                (stand_ins / name).chmod(0o755)
            result = subprocess.run(
                ["bash", str(work / "repo" / "CI" / "macos" / "osg" / "build_osg_gles3.sh"), "--angle-root", str(angle),
                 "--prefix-path", str(work), "--work", str(work / "work"), "--install", str(work / "install")],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env={**os.environ, "PATH": f"{stand_ins}{os.pathsep}{os.environ['PATH']}"})
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("unknown OpenSceneGraph plugin osgdb_unheard_of", result.stdout)
            self.assertNotIn("was started", result.stdout)

    def test_the_build_script_checks_that_what_it_was_asked_for_is_installed(self):
        text = (FOLDER / "build_osg_gles3.sh").read_text()
        self.assertIn("is not installed", text)
        self.assertNotIn("< <(python3", text, "a failed helper inside a process substitution is not seen by the shell")


class BeforeScript(unittest.TestCase):
    """CI/before_script.macos.sh with a stand-in for cmake that records its arguments."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        (self.work / "CI" / "macos").mkdir(parents=True)
        shutil.copy(ROOT / "CI" / "macos" / "deps_versions.sh", self.work / "CI" / "macos")
        self.bin = self.work / "bin"
        self.bin.mkdir()
        fake = self.bin / "cmake"
        fake.write_text('#!/bin/bash\nprintf \'%s\\n\' "$@" > "$PWD/cmake-arguments.txt"\n')
        fake.chmod(0o755)

    def configure(self, *arguments):
        env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}")
        env.pop("CMAKE_BUILD_TYPE", None)
        result = subprocess.run(["bash", str(ROOT / "CI" / "before_script.macos.sh"), *arguments], cwd=self.work,
                                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return result

    def arguments(self, tree):
        text = (self.work / tree / "cmake-arguments.txt").read_text().splitlines()
        # "-D NAME=value" and "-DNAME=value" are the same thing
        joined = []
        skip = False
        for index, item in enumerate(text):
            if skip:
                skip = False
            elif item == "-D":
                joined.append("-D" + text[index + 1])
                skip = True
            else:
                joined.append(item)
        return joined

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_default_run_configures_the_desktop_opengl_build_as_before(self):
        self.assertEqual(self.configure().returncode, 0)
        arguments = self.arguments("build")
        self.assertIn("-DCMAKE_PREFIX_PATH=/tmp/openmw-deps/installed/arm64-osx-dynamic;/tmp/Qt/6.11.2/macos", arguments)
        self.assertIn("-DCMAKE_BUILD_TYPE=RelWithDebInfo", arguments)
        self.assertIn("-DBUILD_ESMTOOL=TRUE", arguments)
        # ANGLE is named only to keep it out of the OpenGL app (the early notarized test switches it on with -A)
        self.assertIn("-DOPENFALLOUT_BUNDLE_ANGLE=OFF", arguments)
        arguments.remove("-DOPENFALLOUT_BUNDLE_ANGLE=OFF")
        self.assertFalse([a for a in arguments if "METAL" in a or "ANGLE" in a or "OSG_DIR" in a])

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_unit_test_tree_is_unchanged(self):
        self.assertEqual(self.configure("-T").returncode, 0)
        arguments = self.arguments("build-tests")
        self.assertIn("-DBUILD_WITH_CODE_COVERAGE=ON", arguments)
        self.assertIn("-DCMAKE_BUILD_TYPE=Debug", arguments)
        self.assertFalse([a for a in arguments if "METAL" in a])

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_metal_switch_makes_its_own_tree_with_the_gles3_library_in_front(self):
        result = self.configure("-M", "-a", "/angle", "-o", "/osg-gles3")
        self.assertEqual(result.returncode, 0, result.stdout)
        arguments = self.arguments("build-metal")
        self.assertIn("-DOPENFALLOUT_METAL=ON", arguments)
        self.assertIn("-DOPENFALLOUT_ANGLE_ROOT=/angle", arguments)
        self.assertIn("-DOSG_DIR=/osg-gles3", arguments)
        # the OpenGL ES 3 OpenSceneGraph is named before the dependency set (which holds the desktop OpenGL one)
        prefix = [a for a in arguments if a.startswith("-DCMAKE_PREFIX_PATH=")]
        self.assertEqual(len(prefix), 1)
        self.assertTrue(prefix[0].startswith("-DCMAKE_PREFIX_PATH=/osg-gles3;/tmp/openmw-deps/"), prefix)
        # the engine and the two test suites, no coverage, no tools
        self.assertIn("-DCMAKE_BUILD_TYPE=Debug", arguments)
        self.assertIn("-DBUILD_OPENFALLOUT_TESTS=ON", arguments)
        self.assertNotIn("-DBUILD_WITH_CODE_COVERAGE=ON", arguments)
        last = {a.split("=")[0]: a.split("=")[1] for a in arguments if a.startswith("-DBUILD_")}
        self.assertEqual(last["-DBUILD_ESMTOOL"], "FALSE")
        self.assertEqual(last["-DBUILD_OPENFALLOUT"], "TRUE")

    @unittest.skipUnless(shutil.which("bash"), "needs bash")
    def test_the_metal_tree_and_the_coverage_tree_cannot_be_combined(self):
        result = self.configure("-M", "-T")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot be combined", result.stdout)


if __name__ == "__main__":
    unittest.main()
