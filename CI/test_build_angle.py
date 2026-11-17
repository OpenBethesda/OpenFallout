"""Tests of CI/macos/angle/build_angle.py: the commands that build ANGLE at the recorded version, the notices made from
what is linked into the two libraries, and the asset it assembles."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "CI"))
sys.path.insert(0, str(ROOT / "CI/macos/angle"))
import angle_asset  # noqa: E402
import build_angle  # noqa: E402

ON_MAC = sys.platform == "darwin" and all(
    shutil.which(tool) for tool in ("clang", "codesign", "install_name_tool", "otool", "lipo", "vtool"))

QUERY = {
    "libGLESv2.dylib": ("solink", ["obj/libGLESv2/entry_points.o", "obj/third_party/zlib/inflate.o",
                                   "obj/libtranslator.a"]),
    "libEGL.dylib": ("solink", ["obj/libEGL/libEGL.o", "obj/xxhash/xxhash.o"]),
    "obj/libtranslator.a": ("alink", ["obj/translator/Compiler.o", "obj/third_party/abseil-cpp/absl/hash/hash.o"]),
    "obj/libGLESv2/entry_points.o": ("cxx", ["../../src/libGLESv2/entry_points.cpp"]),
    "obj/third_party/zlib/inflate.o": ("cc", ["../../third_party/zlib/inflate.c"]),
    "obj/translator/Compiler.o": ("cxx", ["../../src/compiler/translator/Compiler.cpp"]),
    "obj/third_party/abseil-cpp/absl/hash/hash.o": ("cxx", ["../../third_party/abseil-cpp/absl/hash/internal/hash.cc"]),
    "obj/libEGL/libEGL.o": ("cxx", ["gen/angle/libEGL_autogen.cpp"]),
    "obj/xxhash/xxhash.o": ("cc", ["../../src/common/third_party/xxhash/xxhash.c"]),
}


def fake_query(targets):
    return {target: QUERY[target] for target in targets}


def write(path, text=""):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def fake_checkout(root):
    write(root / "LICENSE", "ANGLE licence (BSD 3-Clause)\n")
    write(root / "third_party/zlib/LICENSE", "zlib licence\n")
    write(root / "third_party/zlib/README.chromium", "Name: zlib\nLicense: Zlib\nLicense File: LICENSE\n")
    write(root / "third_party/abseil-cpp/README.chromium",
          "Name: Abseil\nLicense: Apache-2.0\nLicense File: LICENSE\n")
    write(root / "third_party/abseil-cpp/LICENSE", "Apache License 2.0 text\n")
    write(root / "src/common/third_party/xxhash/LICENSE", "xxHash BSD 2-Clause\n")
    for folder in ("EGL", "GLES2", "GLES3", "KHR", "GLES"):
        write(root / "include" / folder / f"{folder.lower()}.h", f"/* {folder} */\n")
    write(root / "include/EGL/README.md", "not a header\n")
    return root


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.version = angle_asset.load_version()

    def test_gn_and_ninja_are_angles_own_with_the_recorded_arguments(self):
        checkout = Path("/work/angle")
        gn, ninja = build_angle.build_commands(self.version, checkout, "OpenFallout", jobs=6)
        self.assertEqual(gn[:3], [str(checkout / "buildtools/mac/gn"), "gen", str(checkout / "out/OpenFallout")])
        self.assertEqual(gn[3], "--args=" + " ".join(self.version["gn_args"]))
        self.assertIn("angle_enable_wgpu=false", gn[3])
        self.assertEqual(ninja, [str(checkout / "third_party/ninja/ninja"), "-C", str(checkout / "out/OpenFallout"),
                                 "-j", "6", "libEGL", "libGLESv2"])

    def test_sources_are_fetched_at_the_recorded_commits_without_updating_depot_tools(self):
        steps = build_angle.fetch_commands(self.version, Path("/work"))
        commands = [" ".join(argv) for _, argv in steps]
        depot_tools = self.version["depot_tools"]
        self.assertIn(f"git -C /work/depot_tools checkout --detach {depot_tools['commit']}", commands)
        self.assertIn(f"git -C /work/angle fetch --depth 1 {self.version['angle']['repository']} "
                      f"{self.version['angle']['commit']}", commands)
        self.assertIn("gclient sync --no-history -j 4", commands)
        environment = build_angle.depot_tools_environment(Path("/work/depot_tools"))
        self.assertEqual(environment["DEPOT_TOOLS_UPDATE"], "0")
        self.assertTrue(environment["PATH"].startswith("/work/depot_tools" + os.pathsep))

    def test_a_checkout_at_another_commit_is_refused(self):
        with tempfile.TemporaryDirectory(prefix="angle checkout ") as checkout:
            subprocess.run(["git", "init", "-q", checkout], check=True)
            subprocess.run(["git", "-C", checkout, "-c", "user.name=t", "-c", "user.email=t@example.invalid",
                            "commit", "-q", "--allow-empty", "-m", "other"], check=True)
            with self.assertRaisesRegex(build_angle.BuildError, "not the recorded"):
                build_angle.check_checkout(Path(checkout), self.version)


    def test_a_checkout_with_edited_sources_is_refused(self):
        """The asset names the recorded commit, so it is never built from sources that differ from it."""
        with tempfile.TemporaryDirectory(prefix="angle checkout ") as checkout:
            git = ["git", "-C", checkout, "-c", "user.name=t", "-c", "user.email=t@example.invalid"]
            subprocess.run(["git", "init", "-q", checkout], check=True)
            for tool in ("buildtools/mac/gn", "third_party/ninja/ninja", "src/source.cpp"):
                (Path(checkout) / tool).parent.mkdir(parents=True, exist_ok=True)
                (Path(checkout) / tool).write_text("as committed\n")
            subprocess.run([*git, "add", "."], check=True)
            subprocess.run([*git, "commit", "-q", "-m", "recorded"], check=True)
            head = subprocess.run([*git, "rev-parse", "HEAD"], check=True, capture_output=True, text=True).stdout.strip()
            version = json.loads(json.dumps(self.version))
            version["angle"]["commit"] = head
            (Path(checkout) / "out").mkdir()
            (Path(checkout) / "out/untracked.o").write_text("build output is not a change\n")
            build_angle.check_checkout(Path(checkout), version)
            (Path(checkout) / "src/source.cpp").write_text("edited\n")
            with self.assertRaisesRegex(build_angle.BuildError, "changed files.*src/source.cpp"):
                build_angle.check_checkout(Path(checkout), version)


class NoticesTests(unittest.TestCase):
    def test_the_linked_sources_come_from_the_libraries_and_their_archives(self):
        sources = build_angle.linked_sources(fake_query, ["libEGL.dylib", "libGLESv2.dylib"])
        self.assertEqual(sources, sorted([
            "../../src/libGLESv2/entry_points.cpp", "../../third_party/zlib/inflate.c",
            "../../src/compiler/translator/Compiler.cpp", "../../third_party/abseil-cpp/absl/hash/internal/hash.cc",
            "gen/angle/libEGL_autogen.cpp", "../../src/common/third_party/xxhash/xxhash.c"]))

    def test_each_source_belongs_to_the_innermost_third_party_folder_or_to_angle(self):
        component = build_angle.component_of
        self.assertEqual(component("../../src/compiler/translator/Compiler.cpp"), "")
        self.assertEqual(component("gen/angle/libEGL_autogen.cpp"), "")
        self.assertEqual(component("../../third_party/abseil-cpp/absl/hash/internal/hash.cc"),
                         "third_party/abseil-cpp")
        self.assertEqual(component("../../src/common/third_party/xxhash/xxhash.c"), "src/common/third_party/xxhash")
        self.assertEqual(component("gen/third_party/perfetto/protos/a.pb.cc"), "third_party/perfetto")
        self.assertEqual(component("../../third_party/libc++/src/src/string.cpp"), "third_party/libc++")

    def test_the_notices_hold_every_linked_component_and_its_licence(self):
        with tempfile.TemporaryDirectory(prefix="angle notices ") as checkout:
            checkout = fake_checkout(Path(checkout))
            sources = build_angle.linked_sources(fake_query, ["libEGL.dylib", "libGLESv2.dylib"])
            components = build_angle.components(checkout, sources)
            self.assertEqual([component.path for component in components],
                             ["", "src/common/third_party/xxhash", "third_party/abseil-cpp", "third_party/zlib"])
            text = build_angle.notices_text(components, "1f6526a566c27c7c33aa73a78517450a4a2a1714", "chromium/8127")
            for licence in ("ANGLE licence (BSD 3-Clause)", "zlib licence", "Apache License 2.0 text",
                            "xxHash BSD 2-Clause"):
                self.assertIn(licence, text)
            self.assertIn("Abseil (third_party/abseil-cpp), licence: Apache-2.0", text)
            self.assertIn("1f6526a566c27c7c33aa73a78517450a4a2a1714", text)
            self.assertLess(text.index("ANGLE licence"), text.index("xxHash"))

    def test_a_linked_component_without_a_licence_file_stops_the_build(self):
        with tempfile.TemporaryDirectory(prefix="angle notices ") as checkout:
            checkout = fake_checkout(Path(checkout))
            (checkout / "src/common/third_party/xxhash/LICENSE").unlink()
            sources = build_angle.linked_sources(fake_query, ["libEGL.dylib", "libGLESv2.dylib"])
            with self.assertRaisesRegex(build_angle.BuildError, "src/common/third_party/xxhash"):
                build_angle.components(checkout, sources)


def compile_library(path, name, minimum="26.0", frameworks=()):
    """As ANGLE's build links: with debug information, so the linker writes a debug map (the absolute path and time of
    each object file) into the symbol table."""
    source = path.with_suffix(".c")
    source.write_text(f"int {name}_answer(void) {{ return 42; }}\n")
    framework_arguments = [argument for framework in frameworks for argument in ("-framework", framework)]
    subprocess.run(["clang", "-g", "-dynamiclib", "-arch", "arm64", f"-mmacosx-version-min={minimum}",
                    "-install_name", f"./{path.name}", "-o", path, source, *framework_arguments], check=True,
                   capture_output=True)


@unittest.skipUnless(ON_MAC, "needs clang and Apple's binary tools (macOS)")
class AssembleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="angle assemble ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.checkout = fake_checkout(self.root / "angle")
        self.out = self.root / "angle/out/OpenFallout"
        self.out.mkdir(parents=True)
        compile_library(self.out / "libEGL.dylib", "egl")
        compile_library(self.out / "libGLESv2.dylib", "gles", frameworks=("Metal",))
        self.version = angle_asset.load_version()
        sources = build_angle.linked_sources(fake_query, ["libEGL.dylib", "libGLESv2.dylib"])
        self.components = build_angle.components(self.checkout, sources)

    def test_the_asset_is_the_angle_root_layout_with_rpath_names_notices_and_sums(self):
        asset = self.root / "asset"
        build_angle.assemble(self.checkout, self.out, asset, self.version, self.components)
        self.assertEqual(sorted(path.relative_to(asset).as_posix() for path in asset.rglob("*") if path.is_file()), [
            "BUILDINFO.json", "NOTICES", "SHA256SUMS", "include/EGL/egl.h", "include/GLES2/gles2.h",
            "include/GLES3/gles3.h", "include/KHR/khr.h", "lib/libEGL.dylib", "lib/libGLESv2.dylib"])
        for name in ("libEGL.dylib", "libGLESv2.dylib"):
            library = asset / "lib" / name
            installed = subprocess.run(["otool", "-D", library], capture_output=True, text=True).stdout
            self.assertEqual(installed.splitlines()[-1], f"@rpath/{name}")
            self.assertEqual(subprocess.run(["codesign", "--verify", "--strict", library]).returncode, 0)
        angle_asset.verify_sums(asset)
        info = json.loads((asset / "BUILDINFO.json").read_text())
        self.assertEqual(info["commit"], self.version["angle"]["commit"])
        self.assertEqual(info["gn_args"], self.version["gn_args"])
        facts = build_angle.check_libraries(asset / "lib")
        self.assertIn("arm64", facts)
        # the same inputs give the same asset, byte for byte
        again = self.root / "again"
        build_angle.assemble(self.checkout, self.out, again, self.version, self.components)
        first, second = self.root / "first.tar.gz", self.root / "second.tar.gz"
        angle_asset.write_archive(asset, first)
        angle_asset.write_archive(again, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_the_asset_holds_no_local_path_and_does_not_depend_on_where_or_when_it_was_linked(self):
        other = self.root / "elsewhere/out/Other"
        other.mkdir(parents=True)
        compile_library(other / "libEGL.dylib", "egl")
        compile_library(other / "libGLESv2.dylib", "gles", frameworks=("Metal",))
        for name in ("libEGL.dylib", "libGLESv2.dylib"):
            # the linked files differ: their debug maps name other object files
            self.assertNotEqual((other / name).read_bytes(), (self.out / name).read_bytes())
        first, second = self.root / "first", self.root / "second"
        build_angle.assemble(self.checkout, self.out, first, self.version, self.components)
        build_angle.assemble(self.checkout, other, second, self.version, self.components)
        for name in ("libEGL.dylib", "libGLESv2.dylib"):
            library = first / "lib" / name
            self.assertEqual(library.read_bytes(), (second / "lib" / name).read_bytes(), name)
            symbols = subprocess.run(["nm", "-ap", library], capture_output=True, text=True).stdout
            self.assertNotIn(" OSO ", symbols)
            self.assertNotIn(str(self.root), library.read_bytes().decode("latin-1"))
            self.assertIn("_egl_answer" if name == "libEGL.dylib" else "_gles_answer", symbols)
            uuid = subprocess.run(["dwarfdump", "--uuid", library], capture_output=True, text=True).stdout
            self.assertRegex(uuid, r"UUID: [0-9A-F]{8}-[0-9A-F]{4}-5[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}")
            self.assertEqual(subprocess.run(["codesign", "--verify", "--strict", library]).returncode, 0)

    def test_a_library_that_links_opengl_or_is_built_for_another_macos_is_refused(self):
        folder = self.root / "lib"
        folder.mkdir()
        compile_library(folder / "libEGL.dylib", "egl", minimum="15.0")
        compile_library(folder / "libGLESv2.dylib", "gles", frameworks=("OpenGL",))
        for name in ("libEGL.dylib", "libGLESv2.dylib"):
            subprocess.run(["install_name_tool", "-id", f"@rpath/{name}", folder / name], check=True,
                           capture_output=True)
        with self.assertRaises(build_angle.BuildError) as raised:
            build_angle.check_libraries(folder)
        self.assertIn("libEGL.dylib: built for macOS 15.0, not 26.0", str(raised.exception))
        self.assertIn("libGLESv2.dylib links OpenGL.framework", str(raised.exception))

    def test_record_writes_the_digests_into_the_version_file(self):
        asset = self.root / "asset"
        build_angle.assemble(self.checkout, self.out, asset, self.version, self.components)
        archive = self.root / self.version["release"]["asset"]
        angle_asset.write_archive(asset, archive)
        version_file = self.root / "angle.json"
        version_file.write_text(json.dumps(self.version, indent=2) + "\n")
        build_angle.record(version_file, archive, asset)
        recorded = json.loads(version_file.read_text())
        self.assertEqual(recorded["release"]["sha256"], angle_asset.sha256_file(archive))
        self.assertEqual(recorded["files"], angle_asset.recorded_files(asset))
        self.assertEqual(recorded["gn_args"], self.version["gn_args"])
        with tarfile.open(archive) as tar:
            self.assertIn("NOTICES", tar.getnames())


if __name__ == "__main__":
    unittest.main()
