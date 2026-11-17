"""The Metal build reads the headers of the OpenSceneGraph for OpenGL ES 3 first (the "Metal include order" block of the
top-level CMakeLists.txt), also when the folder of the other libraries' headers holds a desktop OpenSceneGraph, as the
dependency set of the Mac job does."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HAS_CMAKE = shutil.which("cmake") is not None


def order_block(top):
    return top.split("# Metal include order\n", 1)[1].split("# End of Metal include order", 1)[0]


@unittest.skipUnless(HAS_CMAKE, "needs cmake")
class MetalIncludeOrderTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="metal include order ")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.source, self.build = self.work / "source", self.work / "build"
        self.source.mkdir()
        # the folder of a dependency set: every library's headers, a desktop OpenSceneGraph among them
        self.dependencies = self.header_folder("dependencies", "#define OSG_GL2_AVAILABLE\n")
        self.gles3 = self.header_folder("gles3", "#define OSG_GLES3_AVAILABLE\n")
        # the two calls of the top-level file that come before the block, then the block
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.19)\nproject(MetalIncludeOrderFixture NONE)\n"
            'option(OPENFALLOUT_METAL "" OFF)\n'
            "include_directories(BEFORE SYSTEM ${OPENSCENEGRAPH_INCLUDE_DIRS})\n"
            "include_directories(BEFORE SYSTEM ${OTHER_INCLUDE_DIRS})\n"
            + order_block((ROOT / "CMakeLists.txt").read_text())
            + "get_property(_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)\n"
            'file(WRITE "${CMAKE_BINARY_DIR}/order.txt" "${_dirs}")\n')

    def header_folder(self, name, profile):
        folder = self.work / name
        (folder / "osg").mkdir(parents=True)
        (folder / "osg" / "GL").write_text(profile)
        return folder

    def configure(self, osg, *arguments):
        return subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build),
                               f"-DOPENSCENEGRAPH_INCLUDE_DIRS={osg}", f"-DOTHER_INCLUDE_DIRS={self.dependencies}",
                               *arguments], text=True, capture_output=True)

    def order(self):
        """The folders in the order the compiler reads them (CMake drops the later copies of a folder named twice)."""
        names = [Path(item).name for item in (self.build / "order.txt").read_text().split(";")]
        return list(dict.fromkeys(names))

    def test_the_metal_build_reads_the_opengl_es_3_headers_first(self):
        result = self.configure(self.gles3, "-DOPENFALLOUT_METAL=ON")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.order()[0], "gles3")

    def test_the_opengl_build_keeps_its_order(self):
        result = self.configure(self.dependencies)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.order(), ["dependencies"])
        other = self.header_folder("desktop", "#define OSG_GL2_AVAILABLE\n")
        shutil.rmtree(self.build)
        self.assertEqual(self.configure(other).returncode, 0)
        self.assertEqual(self.order(), ["dependencies", "desktop"])

    def test_a_metal_build_that_would_read_desktop_headers_does_not_configure(self):
        result = self.configure(self.dependencies, "-DOPENFALLOUT_METAL=ON")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("desktop OpenSceneGraph headers", " ".join((result.stdout + result.stderr).split()))


if __name__ == "__main__":
    unittest.main()
