"""The ANGLE libraries in the packaged app (cmake/OpenFalloutAngle.cmake): configured with the real Apple bundling block
of the top-level CMakeLists.txt, and, on macOS, installed for real with stand-in libraries to show that the published
files reach Contents/Frameworks unchanged."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
ON_MAC = sys.platform == "darwin" and all(shutil.which(tool) for tool in ("clang", "otool", "install_name_tool"))
HAS_CMAKE = shutil.which("cmake") is not None


def fixture_lists(top):
    plists = top.split("# Mac app property lists\n", 1)[1].split("# End of Mac app property lists", 1)[0]
    packaging = top.split("# Apple bundling\n", 1)[1].split("# End of Apple bundling", 1)[0] + "endif()\n"
    include = next(line for line in top.splitlines() if line.startswith("include(OpenFalloutAngle)"))
    return plists, packaging, include


@unittest.skipUnless(HAS_CMAKE, "needs cmake")
class AngleBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="angle bundle ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source, self.build = self.root / "source", self.root / "build"
        self.angle = self.root / "angle-root"
        for name in ("lib/libEGL.dylib", "lib/libGLESv2.dylib", "NOTICES", "include/EGL/egl.h",
                     "include/GLES3/gl3.h"):
            (self.angle / name).parent.mkdir(parents=True, exist_ok=True)
            (self.angle / name).write_text(name)

    def write_fixture(self, osg_plugin=b""):
        source = self.source
        shutil.rmtree(source, ignore_errors=True)
        shutil.rmtree(self.build, ignore_errors=True)
        (source / "files/mac").mkdir(parents=True)
        for name in ("openfallout-Info.plist.in", "openfallout-engine-Info.plist.in", "openmw.icns"):
            shutil.copy(ROOT / "files/mac" / name, source / "files/mac")
        for name in ("README.md", "CHANGELOG.md", "LICENSE", "AUTHORS.md"):
            (source / name).touch()
        (source / "cmake").mkdir()
        (source / "cmake/osg.dylib").write_bytes(osg_plugin)
        (source / "cmake/FindOSGPlugins.cmake").write_text(
            'set(DUMMY_LIBRARY_RELEASE "${CMAKE_CURRENT_LIST_DIR}/osg.dylib")\n')
        shutil.copy(ROOT / "cmake/OpenFalloutAngle.cmake", source / "cmake")
        (source / "CI/macos/angle").mkdir(parents=True)
        shutil.copy(ROOT / "CI/macos/angle/angle_probe.c", source / "CI/macos/angle")
        plists, packaging, include = fixture_lists((ROOT / "CMakeLists.txt").read_text())
        (source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.19)
project(AngleBundleFixture C)
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
''' + plists + packaging + include + "\n")

    def configure(self, *arguments, launcher=True, expect_failure=False):
        self.write_fixture()
        result = subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build), "-DBUILD_OPENFALLOUT=ON",
                                 f"-DBUILD_LAUNCHER={'ON' if launcher else 'OFF'}", "-DUSE_QT=OFF", *arguments],
                                text=True, capture_output=True)
        # CMake breaks a long message into lines where it likes (it depends on the length of the temporary path)
        self.output = " ".join((result.stdout + result.stderr).split())
        self.assertEqual(result.returncode != 0, expect_failure, self.output)
        install = self.build / "cmake_install.cmake"
        return install.read_text() if install.exists() else ""

    def test_the_opengl_release_packages_no_angle(self):
        install = self.configure()
        self.assertNotIn("libEGL", install)
        self.assertNotIn("libGLESv2", install)
        self.assertNotIn("ANGLE-NOTICES", install)
        self.assertNotIn("angle-probe", install)

    def test_the_test_build_needs_the_unpacked_asset(self):
        self.configure("-DOPENFALLOUT_BUNDLE_ANGLE=ON", expect_failure=True)
        self.assertIn("OPENFALLOUT_ANGLE_ROOT is not set", self.output)
        (self.angle / "NOTICES").unlink()
        self.configure("-DOPENFALLOUT_BUNDLE_ANGLE=ON", f"-DOPENFALLOUT_ANGLE_ROOT={self.angle}", expect_failure=True)
        self.assertIn("has no NOTICES", self.output)

    def test_the_libraries_notices_and_probe_are_installed_after_fixup_bundle(self):
        install = self.configure("-DOPENFALLOUT_BUNDLE_ANGLE=ON", f"-DOPENFALLOUT_ANGLE_ROOT={self.angle}")
        fixup = install.rindex("fixup_bundle(")
        # the rule that copies the published library, not just any line that names the Frameworks folder
        self.assertGreater(install.index(f"{self.angle}/lib/libEGL.dylib"), fixup)
        self.assertGreater(install.index(f"{self.angle}/lib/libGLESv2.dylib"), fixup)
        self.assertIn(f"{self.angle}/lib/libEGL.dylib", install)
        self.assertIn(f"{self.angle}/lib/libGLESv2.dylib", install)
        self.assertIn('RENAME "ANGLE-NOTICES.txt"', install)
        probe = install.index("angle-probe/openfallout-angle-probe")
        self.assertGreater(probe, fixup)
        self.assertIn("@executable_path/../Frameworks", install[probe:])

    def test_the_metal_build_packages_the_libraries_without_the_probe(self):
        install = self.configure("-DOPENFALLOUT_METAL=ON", f"-DOPENFALLOUT_ANGLE_ROOT={self.angle}")
        self.assertIn(f"{self.angle}/lib/libGLESv2.dylib", install)
        self.assertNotIn("angle-probe", install)

    def test_an_engine_only_metal_build_needs_no_notices(self):
        (self.angle / "NOTICES").unlink()
        self.configure("-DOPENFALLOUT_METAL=ON", f"-DOPENFALLOUT_ANGLE_ROOT={self.angle}", launcher=False)

    @unittest.skipUnless(ON_MAC, "needs clang and Apple's binary tools (macOS)")
    def test_installed_libraries_are_the_published_bytes_and_the_engine_finds_them(self):
        """A stand-in engine linked to @rpath/libEGL.dylib and @rpath/libGLESv2.dylib with the absolute run path of the
        build machine, as the Metal build's is: the app is installed as a copy of the build folder, so
        CMAKE_INSTALL_RPATH_USE_LINK_PATH never reaches it; fixup_bundle copies and rewrites its own copy of each library
        and deletes every run path, then the published files replace the copies, byte for byte. The engine then loads
        them from the app, and the file it finds them in (what components/sdlutil/graphicscontext.cpp gives SDL) is
        the copy already loaded, not a second one."""
        tools = self.root / "tools"
        tools.mkdir()

        def compile_c(output, text, *arguments):
            source = output.with_name(output.name + ".c")
            source.write_text(text)
            subprocess.run(["clang", "-arch", "arm64", "-mmacosx-version-min=26.0", "-o", output, source, *arguments],
                           check=True, capture_output=True)

        for name, function in (("libEGL", "egl"), ("libGLESv2", "gles")):
            library = self.angle / "lib" / f"{name}.dylib"
            # as ANGLE's own: an @rpath install name and an rpath that fixup_bundle would delete
            compile_c(library, f"int {function}_answer(void) {{ return 42; }}\n", "-dynamiclib",
                      "-install_name", f"@rpath/{name}.dylib", "-Wl,-rpath,@executable_path/")
        published = {name: hashlib.sha256((self.angle / "lib" / name).read_bytes()).hexdigest()
                     for name in ("libEGL.dylib", "libGLESv2.dylib")}
        compile_c(tools / "osg.dylib", "int osg_plugin(void) { return 1; }\n", "-dynamiclib")
        self.write_fixture(osg_plugin=(tools / "osg.dylib").read_bytes())
        result = subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build), "-DBUILD_OPENFALLOUT=ON",
                                 "-DBUILD_LAUNCHER=ON", "-DUSE_QT=OFF", "-DOPENFALLOUT_METAL=ON",
                                 f"-DOPENFALLOUT_ANGLE_ROOT={self.angle}"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        macos = self.build / "OpenFallout.app/Contents/MacOS"
        macos.mkdir(parents=True, exist_ok=True)
        compile_c(macos / "openfallout-launcher", "int main(void) { return 0; }\n")
        compile_c(macos / "openfallout", """#include <dlfcn.h>
#include <stdio.h>
int egl_answer(void);
int gles_answer(void);
int main(void)
{
    Dl_info info;
    if (dladdr((const void*)&egl_answer, &info) == 0 || info.dli_fname == NULL)
        return 1;
    printf("EGL from %s\\n", info.dli_fname);
    if (dlopen(info.dli_fname, RTLD_NOLOAD) == NULL)
        return 2;
    return egl_answer() == 42 ? gles_answer() : 3;
}
""", "-L" + str(self.angle / "lib"), "-lEGL", "-lGLESv2", "-Wl,-rpath," + str(self.angle / "lib"))
        prefix = self.root / "installed"
        result = subprocess.run(["cmake", "--install", str(self.build), "--prefix", str(prefix)], text=True,
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        app = prefix / "OpenFallout.app"
        for name, digest in published.items():
            self.assertEqual(hashlib.sha256((app / "Contents/Frameworks" / name).read_bytes()).hexdigest(), digest,
                             name)
        self.assertEqual((app / "Contents/Resources/ANGLE-NOTICES.txt").read_text(), "NOTICES")
        links = subprocess.run(["otool", "-L", app / "Contents/MacOS/openfallout"], capture_output=True,
                               text=True).stdout
        self.assertIn("@executable_path/../Frameworks/libGLESv2.dylib", links)
        self.assertIn("@executable_path/../Frameworks/libEGL.dylib", links)
        self.assertNotIn("@rpath/", links)
        # no run path and no other load command names a folder of the build machine
        commands = subprocess.run(["otool", "-l", app / "Contents/MacOS/openfallout"], capture_output=True,
                                  text=True).stdout.split("\n", 1)[1]  # past otool's line naming the file
        self.assertNotIn("LC_RPATH", commands)
        self.assertNotIn(str(self.root), commands)
        self.assertNotIn(str(self.root.resolve()), commands)
        # the ANGLE root is gone: the engine can only find the copy in the app
        shutil.rmtree(self.angle)
        engine = subprocess.run([app / "Contents/MacOS/openfallout"], capture_output=True, text=True,
                                env={**os.environ, "DYLD_PRINT_LIBRARIES": "1"})
        self.assertEqual(engine.returncode, 42, engine.stdout + engine.stderr)
        self.assertIn("/installed/OpenFallout.app/Contents/Frameworks/libGLESv2.dylib", engine.stderr)
        self.assertIn("/installed/OpenFallout.app/Contents/Frameworks/libEGL.dylib", engine.stderr)
        self.assertRegex(engine.stdout, r"EGL from .*/installed/OpenFallout\.app/Contents/(MacOS/\.\./)?Frameworks/libEGL\.dylib")


if __name__ == "__main__":
    unittest.main()
