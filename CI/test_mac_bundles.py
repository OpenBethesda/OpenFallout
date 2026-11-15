"""Configure the actual Apple packaging rules with stand-in Qt/OSG plugins."""

import os
import plistlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
MACHO = bytes.fromhex("cffaedfe") + bytes(28)
IDENTITY = "Developer ID Application: Example Studio (EXAMPLE123)"
TEAM = "EXAMPLE123"
ENGINE_APP = "OpenFallout.app/Contents/Helpers/OpenFallout Engine.app"
ENTITLEMENTS = {
    "OpenFallout.app": "openfallout.entitlements",
}
LOCAL_TEST_ENTITLEMENTS = ROOT / "files/mac/local-hardened-test.entitlements"


class MacBundleTests(unittest.TestCase):
    def configure(self, game, launcher, *extra):
        temporary = tempfile.TemporaryDirectory(prefix="mac bundles ")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        source, build = root / "source", root / "build"
        (source / "files/mac").mkdir(parents=True)
        (source / "files/mac/qt.conf").write_text("[Paths]\nPlugins=../PlugIns\n")
        for name in ("openfallout-Info.plist.in", "openfallout-engine-Info.plist.in", "openmw.icns"):
            shutil.copy(ROOT / "files/mac" / name, source / "files/mac")
        for name in ("README.md", "CHANGELOG.md", "LICENSE", "AUTHORS.md"):
            (source / name).touch()
        (source / "cmake").mkdir()
        (source / "cmake/osg.dylib").touch()
        (source / "cmake/FindOSGPlugins.cmake").write_text(
            'set(DUMMY_LIBRARY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/osg.dylib")\n')
        shutil.copy(ROOT / "cmake/SignMacApplications.cmake", source / "cmake")
        # Execute the production blocks; no game dependencies or compiler are needed.
        top = (ROOT / "CMakeLists.txt").read_text()
        plists = top.split("# Mac app property lists\n", 1)[1].split("# End of Mac app property lists", 1)[0]
        packaging = top.split("# Apple bundling\n", 1)[1].split("# End of Apple bundling", 1)[0] + "endif()\n"
        (source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.19)
project(BundleFixture NONE)
set(APPLE TRUE)
set(OpenFallout_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
set(OpenFallout_BINARY_DIR "${CMAKE_BINARY_DIR}")
set(APP_BUNDLE_NAME "OpenFallout.app")
set(APP_BUNDLE_DIR "${CMAKE_BINARY_DIR}/${APP_BUNDLE_NAME}")
set(OPENFALLOUT_ENGINE_APP_DIR "${APP_BUNDLE_DIR}/Contents/Helpers/OpenFallout Engine.app")
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(OPENFALLOUT_VERSION "0.1.0")
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0")
set(OPENSCENEGRAPH_VERSION "3.6.5")
set(USED_OSG_PLUGINS dummy)
if(USE_QT)
    set(QT_VERSION_MAJOR 6)
    foreach(plugin QCocoaIntegrationPlugin QMacStylePlugin QSvgPlugin QSvgIconPlugin)
        file(WRITE "${CMAKE_BINARY_DIR}/qt/${plugin}/plugin.dylib" "plugin")
        add_library(Qt6::${plugin} MODULE IMPORTED)
        set_property(TARGET Qt6::${plugin} PROPERTY IMPORTED_LOCATION_RELEASE
            "${CMAKE_BINARY_DIR}/qt/${plugin}/plugin.dylib")
    endforeach()
endif()
''' + plists + packaging)
        result = subprocess.run([
            "cmake", "-S", str(source), "-B", str(build),
            f"-DBUILD_OPENFALLOUT={'ON' if game else 'OFF'}",
            f"-DBUILD_LAUNCHER={'ON' if launcher else 'OFF'}",
            # as in the top-level CMakeLists.txt: the launcher uses Qt
            f"-DUSE_QT={'ON' if launcher else 'OFF'}",
            *extra,
        ], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return root, build

    def check_no_package(self, game, launcher):
        _, build = self.configure(game, launcher)
        install = (build / "cmake_install.cmake").read_text()
        self.assertFalse((build / "CPackConfig.cmake").exists())
        self.assertNotIn("fixup_bundle(", install)
        self.assertEqual(list((build / "OpenFallout.app").rglob("plugin.dylib")), [])
        return build

    def check_bundles(self, *extra):
        # A package is made only when the engine and the launcher are both built, and holds OpenFallout.app only.
        root, build = self.configure(True, True, *extra)
        expected = ["OpenFallout.app"]
        install = (build / "cmake_install.cmake").read_text()
        config = (build / "CPackConfig.cmake").read_text()
        self.assertIn(f'set(CPACK_OPENFALLOUT_APPLICATIONS "{";".join(expected)}")', config)
        entitlements = [f"{root}/source/files/mac/{ENTITLEMENTS[bundle]}" for bundle in expected]
        self.assertIn(f'set(CPACK_OPENFALLOUT_ENTITLEMENTS "{";".join(entitlements)}")', config)
        self.assertIn('fixup_bundle("${CMAKE_INSTALL_PREFIX}/OpenFallout.app"', install)
        self.assertEqual(install.count("fixup_bundle("), 1)
        self.assertNotIn("OpenFallout-CS", install + config)
        self.assertEqual(len(list((build / "OpenFallout.app").rglob("plugin.dylib"))), 4)

        # CPack must sign only the selected bundles.
        signed = self.sign(root, expected)
        self.assertEqual(len(signed), len(expected))
        for call, bundle in zip(signed, expected):
            # With no identity the bundle keeps today's ad hoc signature.
            self.assertEqual(call, ["--force", "--deep", "-s", "-", str(self.app_root / bundle)])

    def stage(self, root, bundles):
        """Lay out bundles as CPack stages them, with stand-in Mach-O files."""
        stage = root / "package"
        for bundle in bundles:
            contents = stage / "ALL_IN_ONE" / bundle / "Contents"
            executable = bundle[:-len(".app")].lower()
            (contents / "PlugIns/osgPlugins-3.6.5").mkdir(parents=True)
            (contents / "Info.plist").write_text(
                "<plist><dict><key>CFBundleExecutable</key>"
                f"<string>{executable}</string></dict></plist>\n")
            framework = contents / "Frameworks/QtCore.framework"
            nested = {
                f"MacOS/{executable}": MACHO,
                "MacOS/helper": MACHO,
                "MacOS/launch.sh": b"#!/bin/sh\n",
                "Frameworks/libSDL2-2.0.0.dylib": MACHO,
                "Frameworks/QtCore.framework/Versions/A/QtCore": MACHO,
                "PlugIns/osgPlugins-3.6.5/osgdb_dds.so": MACHO,
                "PlugIns/platforms/libqcocoa.dylib": MACHO,
                "Resources/qt.conf": b"[Paths]\n",
            }
            for relative, data in nested.items():
                (contents / relative).parent.mkdir(parents=True, exist_ok=True)
                (contents / relative).write_bytes(data)
            (framework / "Versions/Current").symlink_to("A")
            (framework / "QtCore").symlink_to("Versions/Current/QtCore")
            if bundle == "OpenFallout.app":
                engine = contents / "Helpers/OpenFallout Engine.app/Contents"
                (engine / "MacOS").mkdir(parents=True)
                (engine / "Info.plist").write_text(
                    "<plist><dict><key>CFBundleExecutable</key>"
                    "<string>OpenFallout Engine</string></dict></plist>\n")
                (engine / "MacOS/OpenFallout Engine").write_bytes(MACHO)
        return stage

    def sign(self, root, bundles, expect_status=0, **environment):
        """Run the CPack signing script and return every recorded codesign call."""
        stage = self.stage(root, bundles)
        binary = root / "bin"
        binary.mkdir(exist_ok=True)
        signing_log = root / "signed.txt"
        codesign = binary / "codesign"
        codesign.write_text(
            "#!/bin/sh\nIFS=$(printf '\\t')\nprintf '%s\\n' \"$*\" >> \"$SIGNING_LOG\"\n")
        codesign.chmod(0o755)
        env = {k: v for k, v in os.environ.items() if not k.startswith("OPENFALLOUT_CODESIGN")}
        env.update(environment, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                   SIGNING_LOG=str(signing_log))
        entitlements = [str(ROOT / "files/mac" / ENTITLEMENTS[bundle]) for bundle in bundles]
        result = subprocess.run([
            "cmake", "-DAPPLE=TRUE", f"-DCPACK_OPENFALLOUT_APPLICATIONS={';'.join(bundles)}",
            f"-DCPACK_OPENFALLOUT_ENTITLEMENTS={';'.join(entitlements)}",
            f"-DCPACK_TEMPORARY_INSTALL_DIRECTORY={stage}",
            "-P", str(ROOT / "cmake/SignMacApplications.cmake"),
        ], env=env, text=True, capture_output=True)
        self.output = result.stdout + result.stderr
        self.assertEqual(result.returncode != 0, expect_status != 0, self.output)
        self.app_root = stage / "ALL_IN_ONE"
        if not signing_log.exists():
            return []
        return [line.split("\t") for line in signing_log.read_text().splitlines()]

    def sign_with_identity(self, bundles, **environment):
        temporary = tempfile.TemporaryDirectory(prefix="mac signing ")
        self.addCleanup(temporary.cleanup)
        return self.sign(Path(temporary.name), bundles,
                         OPENFALLOUT_CODESIGN_IDENTITY=IDENTITY, **environment)

    def test_identity_signs_each_nested_binary_before_its_bundle(self):
        bundles = ["OpenFallout.app"]
        calls = [call for call in self.sign_with_identity(bundles) if "--sign" in call]
        targets = [call[-1] for call in calls]
        for bundle in bundles:
            app = str(self.app_root / bundle)
            nested = [app + "/Contents/" + relative for relative in (
                "MacOS/helper",
                "Frameworks/libSDL2-2.0.0.dylib",
                "Frameworks/QtCore.framework",
                "PlugIns/osgPlugins/osgdb_dds.so",
                "PlugIns/platforms/libqcocoa.dylib",
            )]
            if bundle == "OpenFallout.app":
                # The engine app is signed as a bundle; its shim is sealed by that signature.
                nested.append(app + "/Contents/Helpers/OpenFallout Engine.app")
            self.assertIn(app, targets)
            for item in nested:
                self.assertEqual(targets.count(item), 1, item)
                self.assertLess(targets.index(item), targets.index(app), item)
            # The main executable is sealed by the bundle signature; scripts,
            # resources and symlinks are not code.
            self.assertEqual(
                sorted(t for t in targets if t.startswith(app + "/")), sorted(nested))

    def test_identity_uses_hardened_runtime_timestamp_and_entitlements(self):
        bundles = ["OpenFallout.app"]
        calls = [call for call in self.sign_with_identity(bundles) if "--sign" in call]
        self.assertTrue(calls)
        for call in calls:
            self.assertEqual(call[call.index("--sign") + 1], IDENTITY)
            self.assertEqual(call[call.index("--options") + 1], "runtime")
            self.assertIn("--timestamp", call)
        for bundle in bundles:
            app = str(self.app_root / bundle)
            entitlements = str(ROOT / "files/mac" / ENTITLEMENTS[bundle])
            # Executables carry their app's entitlements; libraries and plug-ins
            # take those of the process that loads them.
            executables = [app, app + "/Contents/MacOS/helper"]
            if bundle == "OpenFallout.app":
                executables.append(app + "/Contents/Helpers/OpenFallout Engine.app")
            for target in executables:
                call = next(call for call in calls if call[-1] == target)
                self.assertEqual(call[call.index("--entitlements") + 1], entitlements)
            for call in calls:
                if call[-1].startswith(app + "/Contents/") and call[-1] not in executables:
                    self.assertNotIn("--entitlements", call)

    def test_entitlements_files_are_release_safe(self):
        for name in ENTITLEMENTS.values():
            with open(ROOT / "files/mac" / name, "rb") as entitlements:
                values = plistlib.load(entitlements)
            self.assertNotIn("com.apple.security.get-task-allow", values, name)
            # turning library validation off is for local hardened tests only (LOCAL_TEST_ENTITLEMENTS)
            self.assertNotIn("com.apple.security.cs.disable-library-validation", values, name)
        game = (ROOT / "files/mac/openfallout.entitlements").read_text()
        self.assertIn("LUAJIT ENTITLEMENT", game)

    def test_game_entitlements_allow_jit_for_the_hardened_runtime_luajit(self):
        # the decision recorded in docs/openfallout/mac-luajit.md: a LUAJIT_ENABLE_OSX_HRT build with allow-jit;
        # writable-and-executable memory is not granted
        with open(ROOT / "files/mac/openfallout.entitlements", "rb") as entitlements:
            values = plistlib.load(entitlements)
        self.assertIs(values.get("com.apple.security.cs.allow-jit"), True)
        self.assertNotIn("com.apple.security.cs.allow-unsigned-executable-memory", values)

    def test_local_test_entitlements_only_turn_library_validation_off(self):
        with open(LOCAL_TEST_ENTITLEMENTS, "rb") as entitlements:
            values = plistlib.load(entitlements)
        self.assertEqual(values, {"com.apple.security.cs.disable-library-validation": True})

    def test_local_test_adds_the_overlay_to_hardened_ad_hoc_executables_only(self):
        temporary = tempfile.TemporaryDirectory(prefix="mac signing ")
        self.addCleanup(temporary.cleanup)
        calls = self.sign(Path(temporary.name), ["OpenFallout.app"], OPENFALLOUT_CODESIGN_IDENTITY="-",
                          OPENFALLOUT_CODESIGN_LOCAL_TEST="1")
        app = str(self.app_root / "OpenFallout.app")
        executables = [app, app + "/Contents/MacOS/helper", app + "/Contents/Helpers/OpenFallout Engine.app"]
        with open(ROOT / "files/mac/openfallout.entitlements", "rb") as release:
            expected = dict(plistlib.load(release), **{"com.apple.security.cs.disable-library-validation": True})
        for call in (call for call in calls if "--sign" in call):
            self.assertEqual(call[call.index("--options") + 1], "runtime")
            if call[-1] in executables:
                used = call[call.index("--entitlements") + 1]
                self.assertNotEqual(Path(used).parent, ROOT / "files/mac")
                with open(used, "rb") as entitlements:
                    self.assertEqual(plistlib.load(entitlements), expected)
            else:
                self.assertNotIn("--entitlements", call)
        self.assertIn("LOCAL TEST", self.output)

    def test_local_test_is_refused_with_a_real_identity(self):
        temporary = tempfile.TemporaryDirectory(prefix="mac signing ")
        self.addCleanup(temporary.cleanup)
        calls = self.sign(Path(temporary.name), ["OpenFallout.app"], expect_status=1,
                          OPENFALLOUT_CODESIGN_IDENTITY=IDENTITY, OPENFALLOUT_CODESIGN_LOCAL_TEST="1")
        self.assertEqual([call for call in calls if "--sign" in call], [])
        self.assertIn("OPENFALLOUT_CODESIGN_LOCAL_TEST", self.output)
        self.assertNotIn(IDENTITY, self.output)

    def test_identity_never_uses_deep_or_prints_secrets(self):
        calls = self.sign_with_identity(["OpenFallout.app"],
                                        OPENFALLOUT_CODESIGN_TEAM_ID=TEAM)
        for call in calls:
            if "--sign" in call or "-s" in call:
                self.assertNotIn("--deep", call)
        self.assertNotIn(IDENTITY, self.output)
        self.assertNotIn(TEAM, self.output)

    def test_empty_identity_takes_ad_hoc_path(self):
        temporary = tempfile.TemporaryDirectory(prefix="mac signing ")
        self.addCleanup(temporary.cleanup)
        calls = self.sign(Path(temporary.name), ["OpenFallout.app"], OPENFALLOUT_CODESIGN_IDENTITY="")
        self.assertEqual(calls, [["--force", "--deep", "-s", "-", str(self.app_root / "OpenFallout.app")]])

    def test_identity_verifies_each_bundle_against_the_team(self):
        calls = self.sign_with_identity(["OpenFallout.app"], OPENFALLOUT_CODESIGN_TEAM_ID=TEAM)
        app = str(self.app_root / "OpenFallout.app")
        self.assertEqual(calls[-1], [
            "--verify", "--deep", "--strict",
            f'-R=anchor apple generic and certificate leaf[subject.OU] = "{TEAM}"', app])
        self.assertEqual(calls[-2][-1], app)

    def test_identity_without_team_verifies_strictly(self):
        calls = self.sign_with_identity(["OpenFallout.app"])
        self.assertEqual(calls[-1], ["--verify", "--deep", "--strict", str(self.app_root / "OpenFallout.app")])

    def check_engine_app(self, build):
        engine = build / ENGINE_APP
        with (engine / "Contents/Info.plist").open("rb") as file:
            plist = plistlib.load(file)
        self.assertEqual(plist["CFBundleExecutable"], "OpenFallout Engine")
        self.assertEqual(plist["CFBundleIdentifier"], "org.openfallout.openfallout.engine")
        self.assertEqual(plist["CFBundlePackageType"], "APPL")
        self.assertEqual(plist["CFBundleShortVersionString"], "0.1.0")
        self.assertIs(plist.get("LSSupportsGameMode"), True)
        self.assertEqual(plist.get("LSApplicationCategoryType"), "public.app-category.role-playing-games")
        self.assertIs(plist.get("NSHighResolutionCapable"), True)
        self.assertNotIn("LSRequiresCarbon", plist)
        self.assertEqual(plist.get("LSMinimumSystemVersion"), "26.0")
        self.assertTrue((engine / "Contents/Resources" / plist["CFBundleIconFile"]).is_file())
        with (build / "OpenFallout.app/Contents/Info.plist").open("rb") as file:
            self.assertEqual(plistlib.load(file).get("LSMinimumSystemVersion"), "26.0")

    def test_game_bundle_holds_the_engine_app(self):
        _, build = self.configure(game=True, launcher=True)
        self.check_engine_app(build)

    def test_engine_only_build_has_the_engine_app_but_no_package(self):
        # the developer build: engine without the launcher still gets the nested app's property list and icon
        build = self.check_no_package(game=True, launcher=False)
        self.check_engine_app(build)
        self.assertTrue((build / "OpenFallout.app/Contents/Info.plist").is_file())

    def test_tools_only_has_no_package_and_no_engine_app(self):
        build = self.check_no_package(game=False, launcher=False)
        self.assertFalse((build / ENGINE_APP).exists())

    def test_launcher_without_game_has_no_package_and_no_engine_app(self):
        build = self.check_no_package(game=False, launcher=True)
        self.assertFalse((build / ENGINE_APP).exists())

    def test_game_with_launcher(self):
        self.check_bundles()

    def test_old_editor_and_wizard_options_add_nothing(self):
        # CI passed BUILD_OPENCS=TRUE before the editor left; the option no longer exists
        self.check_bundles("-DBUILD_OPENCS=ON", "-DBUILD_WIZARD=ON")


class MacInfoPlistTests(unittest.TestCase):
    """Render the Info.plist templates with CMake's configure_file, as the build does."""

    def render(self, template):
        temporary = tempfile.TemporaryDirectory(prefix="mac plist ")
        self.addCleanup(temporary.cleanup)
        output = Path(temporary.name) / "Info.plist"
        script = Path(temporary.name) / "render.cmake"
        script.write_text(f'set(OPENFALLOUT_VERSION "0.1.0")\nset(CMAKE_OSX_DEPLOYMENT_TARGET "26.1")\n'
                          f'configure_file("{ROOT / "files/mac" / template}" "{output}")\n')
        result = subprocess.run(["cmake", "-P", str(script)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with output.open("rb") as file:
            return plistlib.load(file)

    def test_game_plist_supports_game_mode(self):
        plist = self.render("openfallout-Info.plist.in")
        self.assertIs(plist.get("LSSupportsGameMode"), True)
        self.assertEqual(plist.get("LSApplicationCategoryType"), "public.app-category.role-playing-games")
        self.assertIs(plist.get("NSHighResolutionCapable"), True)
        self.assertEqual(plist.get("CFBundleShortVersionString"), "0.1.0")
        self.assertNotIn("LSRequiresCarbon", plist)

    def test_both_plists_take_the_minimum_macos_from_the_deployment_target(self):
        # the shell reads LSMinimumSystemVersion of the nested engine app; both apps refuse older systems
        for template in ("openfallout-Info.plist.in", "openfallout-engine-Info.plist.in"):
            self.assertEqual(self.render(template).get("LSMinimumSystemVersion"), "26.1", template)


class MacFloorTests(unittest.TestCase):
    """Configure the head of the top-level CMakeLists.txt (up to the supported-Macs checks) without a compiler."""

    def configure(self, *arguments):
        temporary = tempfile.TemporaryDirectory(prefix="mac floor ")
        self.addCleanup(temporary.cleanup)
        source, build = Path(temporary.name) / "source", Path(temporary.name) / "build"
        source.mkdir()
        head = (ROOT / "CMakeLists.txt").read_text().split("# End of supported Macs\n", 1)[0]
        self.assertIn("project(OpenFallout)\n", head)
        (source / "CMakeLists.txt").write_text(head.replace("project(OpenFallout)\n", "project(OpenFallout NONE)\n"))
        result = subprocess.run(["cmake", "-S", str(source), "-B", str(build), *arguments],
                                text=True, capture_output=True)
        return result, build

    def cached(self, build, name):
        for line in (build / "CMakeCache.txt").read_text().splitlines():
            if line.startswith(name + ":"):
                return line.split("=", 1)[1]
        return None

    @unittest.skipUnless(sys.platform == "darwin", "the default applies on macOS")
    def test_default_deployment_target_is_26(self):
        result, build = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.cached(build, "CMAKE_OSX_DEPLOYMENT_TARGET"), "26.0")

    @unittest.skipUnless(sys.platform == "darwin", "the default applies on macOS")
    def test_an_empty_deployment_target_takes_the_default(self):
        # a build folder configured before the floor existed has the value cached as an empty string
        result, build = self.configure("-DCMAKE_OSX_DEPLOYMENT_TARGET=")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.cached(build, "CMAKE_OSX_DEPLOYMENT_TARGET"), "26.0")

    @unittest.skipUnless(sys.platform == "darwin", "the default applies on macOS")
    def test_a_higher_deployment_target_is_kept(self):
        result, build = self.configure("-DCMAKE_OSX_DEPLOYMENT_TARGET=27.0")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.cached(build, "CMAKE_OSX_DEPLOYMENT_TARGET"), "27.0")

    def test_a_lower_deployment_target_is_refused(self):
        result, _ = self.configure("-DCMAKE_OSX_DEPLOYMENT_TARGET=14.8")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("macOS 26.0 or later", result.stderr)

    def test_intel_is_refused(self):
        for architectures in ("x86_64", "arm64;x86_64"):
            result, _ = self.configure(f"-DCMAKE_OSX_ARCHITECTURES={architectures}")
            self.assertNotEqual(result.returncode, 0, architectures)
            self.assertIn("Apple Silicon only", result.stderr)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("c++"), "needs macOS and a C++ compiler")
class MacEngineShimTests(unittest.TestCase):
    """Build the engine app's executable and run it inside a stand-in OpenFallout.app."""

    def test_shim_runs_the_engine_of_the_outer_app(self):
        temporary = tempfile.TemporaryDirectory(prefix="mac shim ")
        self.addCleanup(temporary.cleanup)
        app = Path(temporary.name).resolve() / "OpenFallout.app"
        shim = app / "Contents/Helpers/OpenFallout Engine.app/Contents/MacOS/OpenFallout Engine"
        shim.parent.mkdir(parents=True)
        engine = app / "Contents/MacOS/openfallout"
        engine.parent.mkdir(parents=True)
        engine.write_text('#!/bin/sh\nprintf "%s\\n" "$0" "$@"\nexit 7\n')
        engine.chmod(0o755)
        result = subprocess.run(["c++", "-std=c++17", "-o", str(shim),
                                 str(ROOT / "apps/openfallout/macos/engineappshim.cpp")],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

        result = subprocess.run([str(shim), "--config", "/a folder", "--skip-menu"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [str(engine), "--config", "/a folder", "--skip-menu"])


if __name__ == "__main__":
    unittest.main()
