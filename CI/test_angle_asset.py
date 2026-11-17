"""Tests of CI/angle_asset.py: the ANGLE release asset that the Metal build and the packaged app take, how CI downloads
and verifies it, and the digest of a library that stays the same when the library is signed again."""

import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import angle_asset  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
ON_MAC = sys.platform == "darwin" and all(shutil.which(tool) for tool in ("clang", "codesign", "install_name_tool"))

LC_SEGMENT_64, LC_CODE_SIGNATURE = 0x19, 0x1D


def segment(name, vmaddr, vmsize, fileoff, filesize):
    return struct.pack("<II16sQQQQiiII", LC_SEGMENT_64, 72, name.encode().ljust(16, b"\0"), vmaddr, vmsize, fileoff,
                       filesize, 1, 1, 0, 0)


def fake_library(code=b"code" * 64, signature=None, page=0x4000):
    """A small 64-bit Mach-O dylib: a __TEXT and a __LINKEDIT segment and, when signature is given, LC_CODE_SIGNATURE
    pointing at those bytes at the end of the file, as codesign lays it out."""
    header_size, commands_room = 32, 512
    text_size = header_size + commands_room + len(code)
    linkedit_data = b"symbols" * 8
    linkedit_offset = text_size
    linkedit_size = len(linkedit_data) + (len(signature) if signature else 0)
    commands = [segment("__TEXT", 0, page, 0, text_size),
                segment("__LINKEDIT", page, -(-linkedit_size // page) * page, linkedit_offset, linkedit_size)]
    if signature is not None:
        commands.append(struct.pack("<IIII", LC_CODE_SIGNATURE, 16, linkedit_offset + len(linkedit_data),
                                    len(signature)))
    joined = b"".join(commands)
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, 0x0100000C, 0, 6, len(commands), len(joined), 0, 0)
    return header + joined.ljust(commands_room, b"\0") + code + linkedit_data + (signature or b"")


def version_for(archive, libraries, tag="angle-chromium-8127-1f6526a566c2-r1"):
    return {
        "angle": {"repository": "https://chromium.googlesource.com/angle/angle.git", "branch": "chromium/8127",
                  "commit": "1f6526a566c27c7c33aa73a78517450a4a2a1714"},
        "release": {"repository": "OpenBethesda/OpenFallout", "tag": tag, "asset": archive.name,
                    "sha256": angle_asset.sha256_file(archive)},
        "files": libraries,
    }


class LibraryDigestTests(unittest.TestCase):
    def test_a_new_signature_does_not_change_the_digest_without_signature(self):
        small = fake_library(signature=b"\xfa\xde\x0c\xc0" + b"a" * 60)
        # a Developer ID signature is larger (certificates, timestamp) and makes __LINKEDIT span more pages
        large = fake_library(signature=b"\xfa\xde\x0c\xc0" + b"b" * 40000)
        self.assertNotEqual(hashlib.sha256(small).hexdigest(), hashlib.sha256(large).hexdigest())
        self.assertEqual(angle_asset.code_digest(small), angle_asset.code_digest(large))

    def test_changed_code_changes_the_digest_without_signature(self):
        signature = b"\xfa\xde\x0c\xc0" + b"a" * 60
        self.assertNotEqual(angle_asset.code_digest(fake_library(code=b"code" * 64, signature=signature)),
                            angle_asset.code_digest(fake_library(code=b"edoc" * 64, signature=signature)))

    def test_an_unsigned_or_foreign_file_is_refused(self):
        with self.assertRaisesRegex(angle_asset.AssetError, "no code signature"):
            angle_asset.code_digest(fake_library(signature=None))
        with self.assertRaisesRegex(angle_asset.AssetError, "not a 64-bit Mach-O"):
            angle_asset.code_digest(b"#!/bin/sh\n" + bytes(64))

    def test_a_damaged_library_is_refused_and_not_a_crash(self):
        whole = fake_library(signature=b"\xfa\xde\x0c\xc0" + b"a" * 60)
        for length in (33, 40, 60, len(whole) // 2):
            with self.assertRaises(angle_asset.AssetError, msg=length):
                angle_asset.code_digest(whole[:length])

    @unittest.skipUnless(ON_MAC, "needs clang, codesign and install_name_tool (macOS)")
    def test_real_signatures_of_one_library_have_one_digest(self):
        with tempfile.TemporaryDirectory(prefix="angle digest ") as folder:
            folder = Path(folder)
            source = folder / "lib.c"
            source.write_text("int angle_answer(void) { return 42; }\n")
            built = folder / "libGLESv2.dylib"
            subprocess.run(["clang", "-dynamiclib", "-arch", "arm64", "-mmacosx-version-min=26.0", "-install_name",
                            "@rpath/libGLESv2.dylib", "-o", built, source], check=True)
            # a long designated requirement makes the signature span more pages of __LINKEDIT, as the certificates
            # and the timestamp of a Developer ID signature do
            requirement = "designated => " + " or ".join(f'identifier "org.example.padding.{n}"' for n in range(1500))
            variants = {"linker signature": None,
                        "ad hoc": ["--sign", "-", "--identifier", "libGLESv2"],
                        "hardened": ["--sign", "-", "--options", "runtime", "--identifier", "org.example.other"],
                        "large": ["--sign", "-", "--options", "runtime", f"-r={requirement}"]}
            digests, sizes = {}, {}
            for name, arguments in variants.items():
                copy = folder / f"{name}.dylib"
                shutil.copy(built, copy)
                if arguments:
                    subprocess.run(["codesign", "--force", *arguments, copy], check=True, capture_output=True)
                digests[name] = angle_asset.code_digest(copy.read_bytes())
                sizes[name] = copy.stat().st_size
            self.assertGreater(sizes["large"], sizes["ad hoc"] + 0x4000)
            self.assertEqual(len(set(digests.values())), 1, digests)
            renamed = folder / "renamed.dylib"
            shutil.copy(built, renamed)
            subprocess.run(["install_name_tool", "-id", "@executable_path/../Frameworks/libGLESv2.dylib", renamed],
                           check=True, capture_output=True)
            self.assertNotEqual(angle_asset.code_digest(renamed.read_bytes()), digests["ad hoc"])


class ArchiveTests(unittest.TestCase):
    def make_asset(self, folder):
        (folder / "lib").mkdir(parents=True)
        (folder / "include/EGL").mkdir(parents=True)
        (folder / "lib/libEGL.dylib").write_bytes(fake_library(code=b"egl!" * 16, signature=b"\xfa\xde" * 8))
        (folder / "lib/libGLESv2.dylib").write_bytes(fake_library(code=b"gles" * 16, signature=b"\xfa\xde" * 8))
        os.chmod(folder / "lib/libEGL.dylib", 0o700)
        (folder / "include/EGL/egl.h").write_text("/* egl */\n")
        (folder / "NOTICES").write_text("notices\n")
        angle_asset.write_sums(folder)
        return folder

    def test_the_archive_is_byte_for_byte_the_same_from_the_same_files(self):
        with tempfile.TemporaryDirectory(prefix="angle archive ") as temporary:
            temporary = Path(temporary)
            asset = self.make_asset(temporary / "asset")
            first, second = temporary / "first.tar.gz", temporary / "second.tar.gz"
            angle_asset.write_archive(asset, first)
            for path in asset.rglob("*"):
                os.utime(path, (1, 1))
            angle_asset.write_archive(asset, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with tarfile.open(first) as archive:
                members = archive.getmembers()
            self.assertEqual([member.name for member in members], sorted(member.name for member in members))
            self.assertIn("lib/libEGL.dylib", [member.name for member in members])
            self.assertEqual({member.mode for member in members if member.isfile()}, {0o644, 0o755})
            self.assertEqual({(member.uid, member.gid, member.mtime) for member in members}, {(0, 0, 0)})

    def test_sums_list_every_file_and_catch_a_change(self):
        with tempfile.TemporaryDirectory(prefix="angle sums ") as temporary:
            asset = self.make_asset(Path(temporary) / "asset")
            text = (asset / angle_asset.SUMS).read_text()
            self.assertIn(f"{angle_asset.sha256_file(asset / 'NOTICES')}  NOTICES\n", text)
            self.assertEqual(len(text.splitlines()), 4)
            angle_asset.verify_sums(asset)
            (asset / "NOTICES").write_text("changed\n")
            with self.assertRaisesRegex(angle_asset.AssetError, "NOTICES"):
                angle_asset.verify_sums(asset)


class FetchTests(unittest.TestCase):
    """The download is a file:// address here, so no test needs the network."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="angle fetch ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        asset = ArchiveTests.make_asset(self, self.root / "asset")
        self.archive = self.root / "release" / "angle-chromium-8127-1f6526a566c2-macos-arm64.tar.gz"
        self.archive.parent.mkdir()
        angle_asset.write_archive(asset, self.archive)
        self.files = angle_asset.recorded_files(asset)
        self.version = version_for(self.archive, self.files)
        self.base = self.archive.parent.as_uri()

    def test_a_good_asset_is_unpacked_and_verified(self):
        destination = self.root / "angle-root"
        angle_asset.fetch(self.version, destination, base_url=self.base)
        self.assertTrue((destination / "lib/libGLESv2.dylib").is_file())
        self.assertTrue((destination / "include/EGL/egl.h").is_file())
        self.assertEqual((destination / "NOTICES").read_text(), "notices\n")

    def test_the_address_is_the_release_of_this_project_by_tag_and_name(self):
        self.assertEqual(angle_asset.asset_url(self.version),
                         "https://github.com/OpenBethesda/OpenFallout/releases/download/"
                         "angle-chromium-8127-1f6526a566c2-r1/angle-chromium-8127-1f6526a566c2-macos-arm64.tar.gz")

    def test_a_different_archive_fails_loudly_and_unpacks_nothing(self):
        self.version["release"]["sha256"] = "0" * 64
        destination = self.root / "angle-root"
        with self.assertRaisesRegex(angle_asset.AssetError, "SHA-256 .* is not the recorded"):
            angle_asset.fetch(self.version, destination, base_url=self.base)
        self.assertFalse(destination.exists())

    def test_a_library_that_is_not_the_recorded_one_fails(self):
        self.version["files"]["lib/libEGL.dylib"]["sha256_without_signature"] = "1" * 64
        with self.assertRaisesRegex(angle_asset.AssetError, "lib/libEGL.dylib"):
            angle_asset.fetch(self.version, self.root / "angle-root", base_url=self.base)
        self.assertFalse((self.root / "angle-root").exists())

    def test_a_version_file_without_both_digests_of_a_library_is_refused(self):
        self.version["files"]["lib/libEGL.dylib"] = {}
        with self.assertRaisesRegex(angle_asset.AssetError, "lib/libEGL.dylib"):
            angle_asset.fetch(self.version, self.root / "angle-root", base_url=self.base)
        self.assertFalse((self.root / "angle-root").exists())

    def test_an_archive_that_writes_outside_its_folder_is_refused(self):
        evil = self.root / "release" / "evil.tar.gz"
        with tarfile.open(evil, "w:gz") as archive:
            data = b"x"
            member = tarfile.TarInfo("../escaped")
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
        version = version_for(evil, self.files)
        with self.assertRaisesRegex(angle_asset.AssetError, "escaped"):
            angle_asset.fetch(version, self.root / "angle-root", base_url=self.base)
        self.assertFalse((self.root / "escaped").exists())

    def test_a_folder_that_is_not_empty_is_not_overwritten(self):
        destination = self.root / "angle-root"
        destination.mkdir()
        (destination / "keep").write_text("mine")
        with self.assertRaisesRegex(angle_asset.AssetError, "not empty"):
            angle_asset.fetch(self.version, destination, base_url=self.base)
        self.assertEqual((destination / "keep").read_text(), "mine")

    def test_the_command_line_says_fail_and_exits_1(self):
        version_file = self.root / "angle.json"
        self.version["release"]["sha256"] = "0" * 64
        version_file.write_text(json.dumps(self.version))
        result = subprocess.run([sys.executable, str(ROOT / "CI/angle_asset.py"), "fetch", str(self.root / "out"),
                                 "--version-file", str(version_file), "--base-url", self.base],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("FAIL", result.stdout + result.stderr)
        self.version["release"]["sha256"] = angle_asset.sha256_file(self.archive)
        version_file.write_text(json.dumps(self.version))
        result = subprocess.run([sys.executable, str(ROOT / "CI/angle_asset.py"), "fetch", str(self.root / "out"),
                                 "--version-file", str(version_file), "--base-url", self.base],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("angle-chromium-8127-1f6526a566c2-r1", result.stdout)


class RecordedVersionTests(unittest.TestCase):
    """The version file in the repository: what the build script builds and what CI downloads."""

    def setUp(self):
        self.version = angle_asset.load_version()

    def test_it_names_the_fixed_commit_and_the_metal_only_arguments_of_the_spec(self):
        self.assertEqual(self.version["angle"]["commit"], "1f6526a566c27c7c33aa73a78517450a4a2a1714")
        self.assertEqual(self.version["angle"]["branch"], "chromium/8127")
        self.assertEqual(self.version["gn_args"], [
            "is_debug=false", 'target_cpu="arm64"', "is_component_build=false",
            "angle_enable_metal=true", "angle_enable_gl=false", "angle_enable_vulkan=false",
            "angle_enable_swiftshader=false", "angle_enable_wgpu=false", "angle_enable_null=false",
            "angle_build_tests=false", "use_siso=false",
            'mac_deployment_target="26.0"', 'mac_min_system_version="26.0"'])

    def test_the_tag_and_asset_name_follow_from_branch_and_commit(self):
        release = self.version["release"]
        self.assertEqual(release["repository"], "OpenBethesda/OpenFallout")
        self.assertRegex(release["tag"], r"^angle-chromium-8127-1f6526a566c2-r\d+$")
        self.assertEqual(release["asset"], "angle-chromium-8127-1f6526a566c2-macos-arm64.tar.gz")
        # never the prefix of an engine release (release.yml builds every openfallout-* tag)
        self.assertFalse(release["tag"].startswith("openfallout-"))

    def test_every_digest_is_recorded(self):
        self.assertRegex(self.version["release"]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(sorted(self.version["files"]), ["NOTICES", "lib/libEGL.dylib", "lib/libGLESv2.dylib"])
        for name, digests in self.version["files"].items():
            self.assertRegex(digests["sha256"], r"^[0-9a-f]{64}$", name)
            if name.endswith(".dylib"):
                self.assertRegex(digests["sha256_without_signature"], r"^[0-9a-f]{64}$", name)


if __name__ == "__main__":
    unittest.main()
