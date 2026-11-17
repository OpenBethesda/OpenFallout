"""The MyGUI render system follows OPENFALLOUT_METAL (the "MyGUI render system" block of the top-level CMakeLists.txt):
configured again and again in one build folder, as a person switches the option on and off."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
HAS_CMAKE = shutil.which("cmake") is not None


def render_system_block(top):
    return top.split("# MyGUI render system\n", 1)[1].split("# End of MyGUI render system", 1)[0]


@unittest.skipUnless(HAS_CMAKE, "needs cmake")
class MyGUIRenderSystemTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="mygui render system ")
        self.addCleanup(temporary.cleanup)
        self.source, self.build = Path(temporary.name) / "source", Path(temporary.name) / "build"
        self.source.mkdir()
        block = render_system_block((ROOT / "CMakeLists.txt").read_text())
        # extern/CMakeLists.txt gives the default when MyGUI is built here
        default = re.search(r"^\s*(set\(MYGUI_RENDERSYSTEM \d+ CACHE STRING \"\"\))", (ROOT / "extern/CMakeLists.txt")
                            .read_text(), re.MULTILINE).group(1)
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.19)\nproject(MyGUIRenderSystemFixture NONE)\n"
            'option(OPENFALLOUT_METAL "" OFF)\n' + block + default + "\n")

    def configure(self, *arguments):
        result = subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build), *arguments], text=True,
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cache = (self.build / "CMakeCache.txt").read_text()
        return re.search(r"^MYGUI_RENDERSYSTEM:STRING=(.*)$", cache, re.MULTILINE).group(1)

    def test_the_opengl_build_takes_the_default(self):
        self.assertEqual(self.configure(), "4")

    def test_the_metal_build_takes_the_dummy_render_system(self):
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=ON"), "1")

    def test_switching_the_option_off_gives_the_opengl_value_back(self):
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=ON"), "1")
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=OFF"), "4")
        self.assertEqual(self.configure(), "4")
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=ON"), "1")
        self.assertEqual(self.configure(), "1")

    def test_a_value_set_by_hand_for_the_opengl_build_is_kept(self):
        self.assertEqual(self.configure("-DMYGUI_RENDERSYSTEM=7"), "7")
        self.assertEqual(self.configure(), "7")
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=ON"), "1")
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=OFF"), "7")

    def test_a_value_set_by_hand_while_switching_off_wins(self):
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=ON"), "1")
        self.assertEqual(self.configure("-DOPENFALLOUT_METAL=OFF", "-DMYGUI_RENDERSYSTEM=7"), "7")


if __name__ == "__main__":
    unittest.main()
