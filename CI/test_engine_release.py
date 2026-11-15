"""The engine release's statement, its checks against a packaged app, and its release-notes lines.

Stand-in app trees and stand-in tools: nothing here needs a Mac, a signature or the game."""

import contextlib
import hashlib
import io
import json
import plistlib
from pathlib import Path
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import engine_release  # noqa: E402

VERSION = "0.52.0"
TAG = "openfallout-0.52.0"
COMMIT = "c" * 40
IMAGE = "OpenFallout-0.52.0-macOS-arm64.dmg"
IMAGE_BYTES = b"a disk image"
DIGEST = hashlib.sha256(IMAGE_BYTES).hexdigest()
CMAKE = f"set(OPENFALLOUT_VERSION_MAJOR 0)\nset(OPENFALLOUT_VERSION_MINOR 52)\nset(OPENFALLOUT_VERSION_RELEASE 0)\n"
FAKE_MACHO = bytes.fromhex("cffaedfe") + bytes(28)

ENGINE = "Contents/MacOS/openfallout"
LAUNCHER = "Contents/MacOS/openfallout-launcher"
STUB = "Contents/Helpers/OpenFallout Engine.app/Contents/MacOS/OpenFallout Engine"
LIBRARY = "Contents/Frameworks/libopenal.1.25.0.dylib"


def write_plist(bundle, **keys):
    (bundle / "Contents").mkdir(parents=True, exist_ok=True)
    with (bundle / "Contents/Info.plist").open("wb") as file:
        plistlib.dump(keys, file)


def fake_app(root, minimum="26.0", nested_minimum="26.0", version=VERSION, build_version=VERSION):
    """OpenFallout.app as laid out by the packaging block, with stand-in files."""
    app = root / "OpenFallout.app"
    for item in (ENGINE, LAUNCHER, STUB, LIBRARY):
        (app / item).parent.mkdir(parents=True, exist_ok=True)
        (app / item).write_bytes(FAKE_MACHO)
    versions = {"CFBundleShortVersionString": version, "CFBundleVersion": build_version}
    outer = dict(CFBundleExecutable="openfallout-launcher", **versions)
    nested = dict(CFBundleExecutable="OpenFallout Engine", **versions)
    if minimum is not None:
        outer["LSMinimumSystemVersion"] = minimum
    if nested_minimum is not None:
        nested["LSMinimumSystemVersion"] = nested_minimum
    write_plist(app, **outer)
    write_plist(app / "Contents/Helpers/OpenFallout Engine.app", **nested)
    return app


class Binaries:
    """What vtool and lipo say about the stand-in files: the minimum macOS and the architectures by path suffix."""

    def __init__(self, minos="26.0"):
        self.minos = {ENGINE: minos, LAUNCHER: minos, STUB: minos, LIBRARY: "15.0"}
        self.archs = {ENGINE: ["arm64"]}

    def minos_of(self, path):
        return next((value for suffix, value in self.minos.items() if str(path).endswith(suffix)), None)

    def archs_of(self, path):
        return next((value for suffix, value in self.archs.items() if str(path).endswith(suffix)), [])


def machos(app):
    return [app / item for item in (ENGINE, LAUNCHER, STUB, LIBRARY)]


def statement(**changes):
    fields = dict(version=VERSION, tag=TAG, commit=COMMIT, disk_image=IMAGE, sha256=DIGEST, minimum_macos="26.0",
                  architecture="arm64")
    fields.update(changes)
    return engine_release.build_statement(**fields)


class VersionTests(unittest.TestCase):
    def test_cmake_version_is_the_three_numbers_of_the_top_cmake_file(self):
        self.assertEqual(engine_release.cmake_version(CMAKE), "0.52.0")
        with self.assertRaises(engine_release.StatementError):
            engine_release.cmake_version("project(OpenFallout)\n")

    def test_a_tag_is_openfallout_then_the_version_and_may_carry_a_suffix(self):
        self.assertEqual(engine_release.version_of_tag("openfallout-0.52.0"), "0.52.0")
        self.assertEqual(engine_release.version_of_tag("openfallout-0.52.0-rc1"), "0.52.0")
        self.assertEqual(engine_release.version_of_tag("openfallout-1.0.0-test.2"), "1.0.0")
        for bad in ("v0.52.0", "openfallout-0.52", "openfallout-x", "openfallout-0.52.0.1", "openfallout-0.52.0-",
                    "openmw-0.52.0", "openfallout-0.52.0 "):
            with self.subTest(tag=bad), self.assertRaises(engine_release.StatementError):
                engine_release.version_of_tag(bad)

    def test_the_disk_image_is_named_from_the_version_and_the_architecture(self):
        self.assertEqual(engine_release.disk_image_name("0.52.0", "arm64"), IMAGE)

    def test_macos_versions_compare_as_numbers(self):
        self.assertTrue(engine_release.same_macos("26", "26.0"))
        self.assertTrue(engine_release.same_macos("26.0.0", "26.0"))
        self.assertFalse(engine_release.same_macos("26.0", "26.1"))
        self.assertTrue(engine_release.macos_newer("26.1", "26.0"))
        self.assertFalse(engine_release.macos_newer("26.0", "26"))
        self.assertTrue(engine_release.macos_newer("27.0", "26.9.9"))


class StatementTests(unittest.TestCase):
    def test_the_statement_has_exactly_the_documented_fields(self):
        self.assertEqual(statement(), {
            "schema": 1, "version": VERSION, "tag": TAG, "source_commit": COMMIT, "disk_image": IMAGE,
            "sha256": DIGEST, "minimum_macos": "26.0", "architecture": "arm64"})
        self.assertEqual(list(statement()), list(engine_release.FIELDS))

    def test_a_good_statement_has_no_problems(self):
        self.assertEqual(engine_release.statement_problems(statement()), [])

    def test_each_malformed_field_is_named(self):
        cases = {"version": "0.52", "tag": "v1", "commit": "abc", "disk_image": "OpenFallout.zip",
                 "sha256": "abc", "minimum_macos": "latest", "architecture": "x86_64"}
        for field, value in cases.items():
            key = "source_commit" if field == "commit" else field
            with self.subTest(field=field):
                problems = engine_release.statement_problems(statement(**{field: value}))
                self.assertTrue(any(key in problem for problem in problems), problems)

    def test_a_missing_field_or_wrong_schema_is_named(self):
        broken = statement()
        del broken["sha256"]
        self.assertTrue(any("sha256" in problem for problem in engine_release.statement_problems(broken)))
        self.assertTrue(any("schema" in problem for problem in engine_release.statement_problems(dict(statement(), schema=2))))
        self.assertTrue(engine_release.statement_problems(["not", "an", "object"]))

    def test_the_tag_and_the_version_must_agree(self):
        self.assertTrue(engine_release.statement_problems(statement(tag="openfallout-0.53.0")))
        self.assertEqual(engine_release.statement_problems(statement(tag="openfallout-0.52.0-rc1")), [])

    def test_the_statement_is_written_and_read_back_as_json(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / engine_release.STATEMENT_NAME
            engine_release.write_statement(path, statement())
            self.assertEqual(json.loads(path.read_text()), statement())
            self.assertTrue(path.read_text().endswith("\n"))
            self.assertEqual(engine_release.read_statement(path), statement())
            path.write_text("{not json")
            with self.assertRaises(engine_release.StatementError):
                engine_release.read_statement(path)


class PackageTests(unittest.TestCase):
    """The statement, the property lists and the binaries agree, or the check names where they do not."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binaries = Binaries()

    def problems(self, app=None, **changes):
        app = app or fake_app(self.root)
        return engine_release.package_problems(app, statement(**changes), machos(app), self.binaries.minos_of,
                                               self.binaries.archs_of)

    def test_a_package_that_agrees_with_its_statement_has_no_problems(self):
        self.assertEqual(self.problems(), [])

    def test_the_minimum_is_read_from_the_outer_property_list(self):
        self.assertEqual(engine_release.plist_minimum(fake_app(self.root, minimum="26.0")), "26.0")
        self.assertIsNone(engine_release.plist_minimum(fake_app(self.root / "b", minimum=None)))

    def test_an_outer_app_without_a_minimum_system_version_is_named(self):
        problems = self.problems(fake_app(self.root, minimum=None))
        self.assertTrue(any("OpenFallout.app" in problem and "LSMinimumSystemVersion" in problem for problem in problems), problems)

    def test_the_nested_engine_app_states_the_same_minimum(self):
        for nested in (None, "14.8", "26.1"):
            with self.subTest(nested=nested):
                problems = self.problems(fake_app(self.root / str(nested), nested_minimum=nested))
                self.assertTrue(any("OpenFallout Engine.app" in problem and "LSMinimumSystemVersion" in problem
                                    for problem in problems), problems)

    def test_the_statement_states_the_minimum_of_the_property_list(self):
        problems = self.problems(minimum_macos="14.8")
        self.assertTrue(any("minimum_macos" in problem and "14.8" in problem and "26.0" in problem for problem in problems), problems)

    def test_the_projects_own_programs_report_exactly_the_minimum(self):
        for suffix in (ENGINE, LAUNCHER, STUB):
            self.binaries = Binaries()
            self.binaries.minos[suffix] = "14.8"
            with self.subTest(binary=suffix):
                problems = self.problems(fake_app(self.root / hashlib.sha1(suffix.encode()).hexdigest()))
                self.assertTrue(any(Path(suffix).name in problem and "14.8" in problem for problem in problems), problems)

    def test_a_program_whose_minimum_cannot_be_read_is_named(self):
        del self.binaries.minos[ENGINE]
        self.assertTrue(any("openfallout" in problem and "cannot be read" in problem for problem in self.problems()))

    def test_a_library_built_for_a_newer_macos_than_the_minimum_is_named(self):
        self.binaries.minos[LIBRARY] = "27.0"
        problems = self.problems()
        self.assertTrue(any("libopenal" in problem and "27.0" in problem for problem in problems), problems)

    def test_a_library_built_for_an_older_macos_is_fine(self):
        self.binaries.minos[LIBRARY] = "11.0"
        self.assertEqual(self.problems(), [])

    def test_the_engine_has_the_statements_architecture_only(self):
        self.binaries.archs[ENGINE] = ["x86_64"]
        self.assertTrue(any("architecture" in problem for problem in self.problems()))
        self.binaries.archs[ENGINE] = ["arm64", "x86_64"]
        self.assertTrue(any("architecture" in problem for problem in self.problems()))
        self.binaries.archs[ENGINE] = []
        self.assertTrue(any("architecture" in problem for problem in self.problems()))

    def test_both_property_lists_carry_the_version(self):
        for keys in ({"version": "0.51.0"}, {"build_version": "7"}):
            with self.subTest(keys=keys):
                app = fake_app(self.root / "-".join(keys), **keys)
                problems = self.problems(app)
                self.assertTrue(any(("CFBundleShortVersionString" in problem or "CFBundleVersion" in problem)
                                    and VERSION in problem for problem in problems), problems)
        # the nested app alone
        app = fake_app(self.root / "nested")
        write_plist(app / "Contents/Helpers/OpenFallout Engine.app", CFBundleExecutable="OpenFallout Engine",
                    LSMinimumSystemVersion="26.0", CFBundleShortVersionString="0.50.0", CFBundleVersion=VERSION)
        self.assertTrue(any("OpenFallout Engine.app" in problem and "CFBundleShortVersionString" in problem
                            for problem in self.problems(app)))


class SchemeTests(unittest.TestCase):
    def test_tag_cmake_and_statement_version_agree(self):
        self.assertEqual(engine_release.scheme_problems(statement(), "0.52.0", IMAGE, DIGEST, TAG, COMMIT), [])

    def test_a_tag_of_another_version_than_cmake_is_named(self):
        problems = engine_release.scheme_problems(statement(tag="openfallout-0.53.0", disk_image=IMAGE), "0.52.0", IMAGE,
                                                  DIGEST, "openfallout-0.53.0", COMMIT)
        self.assertTrue(any("tag" in problem and "0.53.0" in problem and "0.52.0" in problem for problem in problems), problems)

    def test_the_disk_image_is_named_from_the_version_and_the_architecture(self):
        other = statement(disk_image="OpenFallout-0.51.0-macOS-arm64.dmg")
        problems = engine_release.scheme_problems(other, "0.52.0", other["disk_image"], DIGEST, TAG, COMMIT)
        self.assertTrue(any("disk_image" in problem and IMAGE in problem for problem in problems), problems)

    def test_a_statement_of_another_image_digest_tag_or_commit_than_expected_is_named(self):
        cases = (("disk_image", "OpenFallout-0.52.0-macOS-arm64.dmg", dict(image="OpenFallout-0.52.0-macOS-other.dmg")),
                 ("sha256", DIGEST, dict(digest="9" * 64)),
                 ("tag", TAG, dict(tag="openfallout-0.52.0-rc1")),
                 ("source_commit", COMMIT, dict(commit="d" * 40)))
        expected = dict(image=IMAGE, digest=DIGEST, tag=TAG, commit=COMMIT)
        for field, _, change in cases:
            with self.subTest(field=field):
                problems = engine_release.scheme_problems(statement(), "0.52.0", **dict(expected, **change))
                self.assertTrue(any(field in problem for problem in problems), problems)


class NotesTests(unittest.TestCase):
    def test_the_supported_line_says_any_apple_silicon_mac_and_the_minimum(self):
        self.assertEqual(engine_release.supported_line("26.0"),
                         "- Supported: any Apple Silicon Mac, macOS 26.0 or later.")

    def test_the_played_on_line_says_the_mac_and_the_macos_the_owner_gave(self):
        self.assertEqual(engine_release.played_on_line("MacBook Pro, Apple M3 Pro", "27.2"),
                         "- Played on: MacBook Pro, Apple M3 Pro, macOS 27.2.")

    def test_the_played_on_line_is_never_made_up(self):
        for mac, macos in (("", "27.2"), ("  ", "27.2"), ("MacBook Pro", ""), ("MacBook Pro", "latest"), ("MacBook Pro", "27.")):
            with self.subTest(mac=mac, macos=macos), self.assertRaises(engine_release.StatementError):
                engine_release.played_on_line(mac, macos)

    def test_notes_with_both_lines_pass_and_the_minimum_must_match_the_statement(self):
        notes = "\n".join(["text", engine_release.supported_line("26.0"),
                            engine_release.played_on_line("MacBook Pro, Apple M3 Pro", "27.2"), ""])
        self.assertEqual(engine_release.notes_problems(notes, "26.0"), [])
        self.assertTrue(any("Supported" in problem for problem in engine_release.notes_problems(notes, "27.0")))

    def test_notes_without_the_supported_line_or_the_played_on_line_are_named(self):
        supported = engine_release.supported_line("26.0")
        played = engine_release.played_on_line("MacBook Pro", "27.2")
        self.assertTrue(any("Supported" in problem for problem in engine_release.notes_problems(played, "26.0")))
        self.assertTrue(any("Played on" in problem for problem in engine_release.notes_problems(supported, "26.0")))
        for broken in ("- Played on: .", "- Played on: macOS 27.2.", "- Played on: MacBook Pro.", "- Played on: TODO, macOS ?."):
            with self.subTest(line=broken):
                self.assertTrue(any("Played on" in problem
                                    for problem in engine_release.notes_problems(supported + "\n" + broken, "26.0")))
        self.assertTrue(engine_release.notes_problems(None, "26.0"))

    def test_the_notes_command_prints_the_supported_line_and_the_played_on_line_when_given(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "statement.json"
            engine_release.write_statement(path, statement())
            notes = Path(directory) / "notes.md"
            notes.write_text("earlier line\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(engine_release.main(["notes", str(path), "--append", str(notes)]), 0)
            self.assertEqual(notes.read_text(), "earlier line\n- Supported: any Apple Silicon Mac, macOS 26.0 or later.\n")
            self.assertIn("Played on", out.getvalue())  # says it is still to be added by the owner
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(engine_release.main(["notes", str(path), "--played-on-mac", "MacBook Pro, Apple M3 Pro",
                                                      "--played-on-macos", "27.2", "--append", str(notes)]), 0)
            self.assertIn("- Played on: MacBook Pro, Apple M3 Pro, macOS 27.2.\n", notes.read_text())

    def test_the_notes_command_refuses_half_of_the_played_on_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "statement.json"
            engine_release.write_statement(path, statement())
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                engine_release.main(["notes", str(path), "--played-on-mac", "MacBook Pro"])


class CommandTests(unittest.TestCase):
    """`write` and `verify` on a stand-in disk image: the way CI and the owner run them."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.volume = self.root / "volume"
        self.volume.mkdir()
        self.app = fake_app(self.volume)
        self.dmg = self.root / IMAGE
        self.dmg.write_bytes(IMAGE_BYTES)
        self.binaries = Binaries()
        self.cmake = self.root / "CMakeLists.txt"
        self.cmake.write_text(CMAKE)
        self.out = self.root / engine_release.STATEMENT_NAME

        @contextlib.contextmanager
        def mounted(path):
            yield self.volume

        def run(command, **kwargs):
            import subprocess
            path = str(command[-1])
            if command[0] == "vtool":
                minos = self.binaries.minos_of(path)
                text = f"{path}:\nLoad command 9\n      cmd LC_BUILD_VERSION\n platform MACOS\n    minos {minos}\n" if minos else ""
                return subprocess.CompletedProcess(command, 0, text, "")
            if command[0] == "lipo":
                return subprocess.CompletedProcess(command, 0, " ".join(self.binaries.archs_of(path)) + "\n", "")
            raise AssertionError(command)

        for name, value in (("mounted", mounted), ("run", run), ("CMAKE_LISTS", self.cmake)):
            patcher = unittest.mock.patch.object(engine_release, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def write(self, *extra, tag=TAG, commit=COMMIT):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            status = engine_release.main(["write", str(self.dmg), "--tag", tag, "--commit", commit, "--out",
                                          str(self.out), *extra])
        return status, out.getvalue()

    def verify(self, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            status = engine_release.main(["verify", str(self.dmg), str(self.out), *extra])
        return status, out.getvalue()

    def test_write_makes_the_statement_from_the_package(self):
        status, output = self.write()
        self.assertEqual(status, 0, output)
        self.assertEqual(json.loads(self.out.read_text()), statement())

    def test_write_takes_the_minimum_from_the_property_list_and_the_digest_from_the_image(self):
        path = self.app / "Contents/Info.plist"
        keys = plistlib.loads(path.read_bytes())
        keys["LSMinimumSystemVersion"] = "26.2"
        path.write_bytes(plistlib.dumps(keys))
        nested = self.app / "Contents/Helpers/OpenFallout Engine.app/Contents/Info.plist"
        keys = plistlib.loads(nested.read_bytes())
        keys["LSMinimumSystemVersion"] = "26.2"
        nested.write_bytes(plistlib.dumps(keys))
        for suffix in (ENGINE, LAUNCHER, STUB):
            self.binaries.minos[suffix] = "26.2"
        status, output = self.write()
        self.assertEqual(status, 0, output)
        written = json.loads(self.out.read_text())
        self.assertEqual((written["minimum_macos"], written["sha256"]), ("26.2", DIGEST))

    def test_write_refuses_and_writes_nothing_when_the_package_disagrees(self):
        self.binaries.minos[ENGINE] = "14.8"
        status, output = self.write()
        self.assertEqual(status, 1)
        self.assertIn("openfallout", output)
        self.assertFalse(self.out.exists())

    def test_write_refuses_a_tag_of_another_version_than_cmake(self):
        status, output = self.write(tag="openfallout-0.53.0")
        self.assertEqual(status, 1)
        self.assertIn("0.53.0", output)
        self.assertFalse(self.out.exists())

    def test_write_refuses_an_image_that_is_not_named_from_the_version(self):
        renamed = self.root / "OpenFallout-0.51.0-macOS-arm64.dmg"
        self.dmg.rename(renamed)
        self.dmg = renamed
        status, output = self.write()
        self.assertEqual(status, 1)
        self.assertIn("OpenFallout-0.51.0-macOS-arm64.dmg", output)

    def test_write_refuses_a_cmake_version_that_the_property_lists_do_not_carry(self):
        self.cmake.write_text(CMAKE.replace("MINOR 52", "MINOR 53"))
        status, output = self.write()
        self.assertEqual(status, 1)
        self.assertIn("CFBundleShortVersionString", output)

    def test_verify_accepts_a_statement_that_the_package_agrees_with(self):
        self.assertEqual(self.write()[0], 0)
        status, output = self.verify("--tag", TAG, "--commit", COMMIT)
        self.assertEqual(status, 0, output)

    def test_verify_refuses_when_the_statement_the_property_list_and_the_binaries_disagree(self):
        self.assertEqual(self.write()[0], 0)
        written = json.loads(self.out.read_text())
        for field, value in (("minimum_macos", "14.8"), ("sha256", "9" * 64), ("source_commit", "d" * 40),
                             ("tag", "openfallout-0.53.0")):
            with self.subTest(field=field):
                self.out.write_text(json.dumps(dict(written, **{field: value})))
                status, output = self.verify("--commit", COMMIT)
                self.assertEqual(status, 1, output)
                self.assertIn(field, output)
        self.out.write_text(json.dumps(written))
        self.binaries.minos[STUB] = "27.0"
        status, output = self.verify()
        self.assertEqual(status, 1)
        self.assertIn("OpenFallout Engine", output)


if __name__ == "__main__":
    unittest.main()
