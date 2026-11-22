"""The configure stops when CMake found Apple's OpenAL framework instead of OpenAL Soft (the block after
find_package(OpenAL REQUIRED) in the top-level CMakeLists.txt). The framework has no HRTF, none of OpenAL Soft's
extensions and no way to send sound to a file, and the packaged app never ships it (CI/check_mac_package.py), so a
developer build that links it plays differently from what players get."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HAS_CMAKE = shutil.which("cmake") is not None


def openal_block(top):
    start = top.index("find_package(OpenAL REQUIRED)")
    end = top.index("find_package(ZLIB REQUIRED)", start)
    return top[start:end]


@unittest.skipUnless(HAS_CMAKE, "needs cmake")
class OpenALGuardTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="openal guard ")
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.source = self.folder / "source"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.19)\nproject(OpenALGuardFixture NONE)\n"
            + openal_block((ROOT / "CMakeLists.txt").read_text()))
        self.include = self.folder / "include" / "AL"
        self.include.mkdir(parents=True)
        (self.include / "al.h").write_text("")

    def configure(self, library, build):
        return subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.folder / build),
                               f"-DOPENAL_LIBRARY={library}", f"-DOPENAL_INCLUDE_DIR={self.include}"],
                              text=True, capture_output=True)

    def test_apples_framework_stops_the_configure_and_says_what_to_pass(self):
        result = self.configure("/System/Library/Frameworks/OpenAL.framework", "system")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        said = " ".join((result.stdout + result.stderr).split())
        self.assertIn("OpenAL Soft", said)
        self.assertIn("-DOPENAL_LIBRARY=", said)
        self.assertIn("openal-soft", said)

    def test_openal_soft_configures(self):
        library = self.folder / "lib" / "libopenal.1.25.0.dylib"
        library.parent.mkdir()
        library.write_text("")
        result = self.configure(library, "soft")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
