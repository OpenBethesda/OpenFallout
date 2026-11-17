#!/usr/bin/env python3
"""The ANGLE release asset: the project's own build of ANGLE's two libraries, which the Metal build links and the
packaged app carries (docs/openfallout/angle.md).

    CI/angle_asset.py fetch DEST [--version-file FILE] [--base-url URL]
    CI/angle_asset.py digest LIBRARY...

fetch    downloads the asset named in CI/macos/angle/angle.json from the release of this project with the recorded tag,
         checks its SHA-256 against the recorded one, unpacks it into DEST (a new or empty folder), and checks every file
         against the asset's own SHA256SUMS and the libraries and NOTICES against the digests recorded in the version
         file. Any mismatch fails loudly and leaves DEST empty; nothing is ever built. DEST is then the folder that
         -DOPENFALLOUT_ANGLE_ROOT names: lib/libEGL.dylib, lib/libGLESv2.dylib, include/{EGL,GLES2,GLES3,KHR},
         NOTICES, SHA256SUMS. --base-url replaces https://github.com/<repository>/releases/download/<tag> (a file://
         address for tests, or a mirror).
digest   prints the two digests of each library: of the whole file, and without its code signature.

The digest without signature is what lets CI/check_mac_package.py recognise the published libraries inside a signed
app: signing a library rewrites its signature and the size fields of the __LINKEDIT segment, nothing else. That digest
is the SHA-256 of the file up to the start of the signature, with the LC_CODE_SIGNATURE load command taken out and the
two __LINKEDIT size fields set to zero, so it is the same for the ad hoc signature of the asset, a Developer ID
signature and any later one, and changes with any other byte (code, data, install name).
"""
import argparse
import gzip
import hashlib
import http.client
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tarfile
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "CI/macos/angle/angle.json"
SUMS = "SHA256SUMS"
LIBRARIES = ("lib/libEGL.dylib", "lib/libGLESv2.dylib")
NOTICES = "NOTICES"
RECORDED = LIBRARIES + (NOTICES,)

MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D


class AssetError(Exception):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------------------------------------- digest without signature

def code_digest(data):
    """SHA-256 of a thin 64-bit Mach-O file without its code signature (see the top of this file). A file that is cut
    short or damaged is refused like any other file that is not the library: AssetError, never a crash."""
    try:
        return _code_digest(data)
    except struct.error:
        raise AssetError("the Mach-O file is cut short or damaged") from None


def _code_digest(data):
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != MH_MAGIC_64:
        raise AssetError("not a 64-bit Mach-O file (a universal file or another format)")
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from("<IiiIIIII", data)
    commands, offset, signature = [], 32, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > 32 + sizeofcmds or offset + size > len(data):
            raise AssetError("a load command runs past the header")
        command = bytearray(data[offset:offset + size])
        if cmd == LC_CODE_SIGNATURE:
            signature = struct.unpack_from("<II", command, 8)
        else:
            if cmd == LC_SEGMENT_64 and command[8:24].rstrip(b"\0") == b"__LINKEDIT":
                # vmsize (offset 32) and filesize (offset 48): signing grows them with the signature
                struct.pack_into("<Q", command, 32, 0)
                struct.pack_into("<Q", command, 48, 0)
            commands.append(bytes(command))
        offset += size
    if signature is None:
        raise AssetError("the file has no code signature")
    start, size = signature
    if start + size != len(data) or start < 32 + sizeofcmds:
        raise AssetError("the code signature is not at the end of the file")
    joined = b"".join(commands)
    header = struct.pack("<IiiIIIII", magic, cputype, cpusubtype, filetype, ncmds - 1, len(joined), flags, reserved)
    # the bytes after the load commands (padding, then the segments) stay where they are
    canonical = header + joined + bytes(sizeofcmds - len(joined)) + data[32 + sizeofcmds:start]
    return hashlib.sha256(canonical).hexdigest()


def library_digests(path):
    data = Path(path).read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "sha256_without_signature": code_digest(data)}


def recorded_files(asset):
    """The digests the version file records for an asset folder: both digests of each library, the plain one of the
    notices."""
    asset = Path(asset)
    files = {name: library_digests(asset / name) for name in LIBRARIES}
    files[NOTICES] = {"sha256": sha256_file(asset / NOTICES)}
    return files


# -------------------------------------------------------------------------------------------------------------- sums

def asset_files(folder):
    folder = Path(folder)
    return sorted(path.relative_to(folder).as_posix() for path in folder.rglob("*")
                  if path.is_file() and path.name != SUMS and not path.name.startswith("."))


def write_sums(folder):
    """SHA256SUMS of every file of the asset, in the format `shasum -a 256 -c` reads."""
    folder = Path(folder)
    lines = [f"{sha256_file(folder / name)}  {name}\n" for name in asset_files(folder)]
    (folder / SUMS).write_text("".join(lines), encoding="utf-8")


def verify_sums(folder):
    folder = Path(folder)
    listed = {}
    for line in (folder / SUMS).read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        listed[name] = digest
    present = asset_files(folder)
    problems = [f"{name} is not in {SUMS}" for name in present if name not in listed]
    problems += [f"{name} is listed in {SUMS} but missing" for name in listed if name not in present]
    problems += [f"{name} does not match {SUMS}" for name in present
                 if name in listed and sha256_file(folder / name) != listed[name]]
    if problems:
        raise AssetError("; ".join(problems))


# ----------------------------------------------------------------------------------------------------------- archive

def write_archive(folder, archive):
    """A .tar.gz of the asset folder that is the same byte for byte whenever the files are: sorted entries, no owner,
    time 0, modes 0755 (programs and libraries) and 0644, and a gzip header without a time or name."""
    folder = Path(folder)
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as tar:
        entries = sorted(path for path in folder.rglob("*") if not path.name.startswith("."))
        for path in entries:
            name = path.relative_to(folder).as_posix()
            if path.is_symlink():
                raise AssetError(f"{name} is a link; the asset holds plain files only")
            member = tarfile.TarInfo(name)
            member.mtime, member.uid, member.gid, member.uname, member.gname = 0, 0, 0, "", ""
            if path.is_dir():
                member.type, member.mode = tarfile.DIRTYPE, 0o755
                tar.addfile(member)
            else:
                member.size = path.stat().st_size
                member.mode = 0o755 if os.access(path, os.X_OK) or name.endswith(".dylib") else 0o644
                with path.open("rb") as file:
                    tar.addfile(member, file)
    with open(archive, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as compressed:
            compressed.write(buffer.getvalue())


def unpack(archive, destination):
    """Unpacks plain files and folders only, each inside destination."""
    with tarfile.open(archive, "r:gz") as tar:
        members = tar.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not (member.isfile() or member.isdir()):
                raise AssetError(f"the archive holds {member.name!r}, which is not a plain file inside the asset")
        for member in members:
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as source, target.open("wb") as file:
                shutil.copyfileobj(source, file)
            target.chmod(0o755 if member.mode & 0o111 else 0o644)


# ------------------------------------------------------------------------------------------------------------- fetch

def load_version(path=VERSION_FILE):
    with open(path, encoding="utf-8") as file:
        version = json.load(file)
    for keys in (("angle", "commit"), ("release", "repository"), ("release", "tag"), ("release", "asset"),
                 ("release", "sha256")):
        if not isinstance(version.get(keys[0], {}).get(keys[1]), str):
            raise AssetError(f"{path} has no {'.'.join(keys)}")
    return version


def asset_url(version, base_url=None):
    release = version["release"]
    base = base_url or f"https://github.com/{release['repository']}/releases/download/{release['tag']}"
    return f"{base.rstrip('/')}/{release['asset']}"


def download(url, target, attempts=3):
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(url, timeout=120) as response, open(target, "wb") as file:
                shutil.copyfileobj(response, file)
            return
        except (OSError, http.client.HTTPException) as error:  # HTTPException: the connection ended mid-body
            if attempt == attempts or url.startswith("file:"):
                raise AssetError(f"cannot download {url}: {error}") from None
            time.sleep(5 * attempt)


def verify_recorded(version, folder):
    problems = []
    for name, recorded in version.get("files", {}).items():
        path = folder / name
        if not path.is_file():
            problems.append(f"{name} is missing")
            continue
        found = library_digests(path) if name.endswith(".dylib") else {"sha256": sha256_file(path)}
        # every digest this script can compute must be recorded: a version file that leaves one out checks nothing
        for kind in found:
            if not isinstance(recorded.get(kind), str):
                problems.append(f"{name}: the version file records no {kind}")
        for kind, digest in recorded.items():
            if found.get(kind) != digest:
                problems.append(f"{name}: {kind} {found.get(kind)} is not the recorded {digest}")
    missing = [name for name in RECORDED if name not in version.get("files", {})]
    if missing:
        problems.append(f"the version file records no digest for {', '.join(missing)}")
    if problems:
        raise AssetError("; ".join(problems))


def fetch(version, destination, base_url=None):
    """Downloads, verifies and unpacks the recorded asset into destination (new or empty). Raises AssetError."""
    destination = Path(destination)
    if destination.exists() and any(destination.iterdir()):
        raise AssetError(f"{destination} is not empty")
    url = asset_url(version, base_url)
    with tempfile.TemporaryDirectory(prefix="angle-asset-") as temporary:
        archive = Path(temporary) / version["release"]["asset"]
        download(url, archive)
        found = sha256_file(archive)
        if found != version["release"]["sha256"]:
            raise AssetError(f"the SHA-256 of {url} is {found}, which is not the recorded "
                             f"{version['release']['sha256']} (CI/macos/angle/angle.json); not unpacked")
        staged = Path(temporary) / "unpacked"
        staged.mkdir()
        unpack(archive, staged)
        verify_sums(staged)
        verify_recorded(version, staged)
        destination.mkdir(parents=True, exist_ok=True)
        for entry in staged.iterdir():
            shutil.move(str(entry), destination / entry.name)
    return url


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    fetch_parser = commands.add_parser("fetch", help="download, verify and unpack the recorded asset")
    fetch_parser.add_argument("destination", type=Path)
    fetch_parser.add_argument("--version-file", type=Path, default=VERSION_FILE)
    fetch_parser.add_argument("--base-url", help="instead of the release address of the recorded tag")
    digest_parser = commands.add_parser("digest", help="print both digests of libraries")
    digest_parser.add_argument("libraries", nargs="+", type=Path)
    options = parser.parse_args(argv)
    try:
        if options.command == "fetch":
            version = load_version(options.version_file)
            url = fetch(version, options.destination, options.base_url)
            print(f"PASS angle: {version['release']['tag']} ({url}), SHA-256 {version['release']['sha256']}, "
                  f"unpacked into {options.destination}")
        else:
            for library in options.libraries:
                print(json.dumps({str(library): library_digests(library)}))
    except (AssetError, OSError) as error:
        print(f"FAIL angle: {error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
