"""The Metal build finds ANGLE under OPENFALLOUT_ANGLE_ROOT (the OPENFALLOUT_METAL block of the top-level
CMakeLists.txt): configured again in one build folder with another root, it takes that root's files."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HAS_CMAKE = shutil.which("cmake") is not None


def discovery_block(top):
    start = top.index("    set(OPENFALLOUT_ANGLE_ROOT ")
    return top[start:top.index('    message(STATUS "Metal build: ANGLE', start)]


@unittest.skipUnless(HAS_CMAKE, "needs cmake")
class AngleRootTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="angle root ")
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.source, self.build = self.folder / "source", self.folder / "build"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.19)\nproject(AngleRootFixture NONE)\n"
            "set(CMAKE_FIND_LIBRARY_PREFIXES lib)\nset(CMAKE_FIND_LIBRARY_SUFFIXES .dylib)\n"
            + discovery_block((ROOT / "CMakeLists.txt").read_text()))

    def angle(self, name):
        root = self.folder / name
        (root / "lib").mkdir(parents=True)
        (root / "include/GLES3").mkdir(parents=True)
        for file in ("lib/libEGL.dylib", "lib/libGLESv2.dylib", "include/GLES3/gl3.h"):
            (root / file).write_text("")
        return root

    def configure(self, root):
        result = subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build),
                                 f"-DOPENFALLOUT_ANGLE_ROOT={root}"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cache = (self.build / "CMakeCache.txt").read_text()
        return {name: re.search(rf"^{name}:[A-Z]+=(.*)$", cache, re.MULTILINE).group(1)
                for name in ("ANGLE_INCLUDE_DIR", "ANGLE_EGL_LIBRARY", "ANGLE_GLESV2_LIBRARY")}

    def test_a_new_root_replaces_the_files_of_the_previous_one(self):
        first, second = self.angle("first"), self.angle("second")
        found = self.configure(first)
        self.assertTrue(all(Path(path).resolve().is_relative_to(first.resolve()) for path in found.values()), found)
        found = self.configure(second)
        self.assertTrue(all(Path(path).resolve().is_relative_to(second.resolve()) for path in found.values()), found)

    def test_the_same_root_keeps_its_files(self):
        first = self.angle("first")
        self.assertEqual(self.configure(first), self.configure(first))


if __name__ == "__main__":
    unittest.main()
