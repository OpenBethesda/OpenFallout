# Engine releases: address, statement, minimum macOS, release notes

An *engine release* is a packaged app that this project publishes with a version, signed with the project's Developer ID
and notarized, as a GitHub release of this repository. The shell (OpenBethesda, a separate project) fixes on one exact
engine release and starts it; it never builds the engine. Signing and notarization are in [mac-release.md](mac-release.md).
This page holds what an engine release needs beyond them: where it is, how a program reads what it is, what the release
notes say, and how the owner cuts one (spec #210, stories 6, 9, 10 and 13).

## What a release holds

A release made from the tag `openfallout-<version>` holds, as release assets:

| Asset | What it is |
| --- | --- |
| `OpenFallout-<version>-macOS-arm64.dmg` | the notarized disk image with the packaged app |
| `openfallout-engine-release.json` | the statement of the release (below), made by CI from that disk image |

and release notes with the lines of [Release notes](#release-notes).

## The address

GitHub serves the assets of a published release at an address built from the tag and the asset's file name:

    https://github.com/OpenBethesda/OpenFallout/releases/download/<tag>/OpenFallout-<version>-macOS-arm64.dmg
    https://github.com/OpenBethesda/OpenFallout/releases/download/<tag>/openfallout-engine-release.json

For a release the tag is exactly `openfallout-<version>`, so both addresses follow from the version alone. A candidate or
a test build has a suffix (`openfallout-<version>-rc1`); its addresses are the same pattern with that tag, and its
statement names the tag.

A draft release has no public address; the addresses work once it is published.

**Not `latest`.** GitHub's `releases/latest/download/<asset>` points at the newest published release that is not marked
prerelease, and moves with every new release. The release tooling publishes engine releases as prereleases, so it would
not point at one anyway. The shell fixes on one exact engine: it keeps the tag and the SHA-256 digest of the engine
release it was tested with, downloads from the address of that tag, and compares the digest of what it got with the one it
keeps. It moves to another engine release by changing that pair.

## The statement

`openfallout-engine-release.json` is written by `CI/engine_release.py write` from the disk image CI built. It is never
typed by hand. One JSON object:

| Field | Meaning | Where it comes from |
| --- | --- | --- |
| `schema` | `1`; changes only if a field changes meaning | the tool |
| `version` | the engine's version, e.g. `0.1.0` | `OPENFALLOUT_VERSION_*` in the top `CMakeLists.txt` |
| `tag` | the release tag | the tag CI builds |
| `source_commit` | the 40-character commit the image was built from | the commit CI builds |
| `disk_image` | the file name of the disk image, `OpenFallout-<version>-macOS-arm64.dmg` | the image itself |
| `sha256` | the SHA-256 of the disk image file, 64 lowercase hex digits | computed from the image file |
| `minimum_macos` | the oldest macOS the app starts on, e.g. `26.0` | `LSMinimumSystemVersion` of the outer app |
| `architecture` | `arm64` | the name of the image, checked against the engine binary |

### The value the shell reads for the minimum macOS

**`minimum_macos` in `openfallout-engine-release.json`.** The shell reads it from the release without mounting the image
or starting the app, and reports "this engine route needs macOS `<minimum_macos>` or later". Its source is the
`LSMinimumSystemVersion` of the outer `OpenFallout.app`'s `Info.plist`; the statement only repeats it. The packaged app
refuses to start on an older macOS because of that property list.

### What must agree

`CI/engine_release.py write` (and `verify`, which checks a statement against a disk image) mounts the image read-only and
fails, naming every difference, when any of these disagree. `write` then writes nothing.

- The tag is `openfallout-<version>` with an optional `-<suffix>`, and `<version>` is the version in `CMakeLists.txt`.
- Versions are numbered `0.<milestone>.<n>`: 0.1.x for the Mac-only milestone, 0.2.x for the Metal renderer, 0.3.x for the
  window and front door. The last number counts the releases cut inside a milestone. 1.0.0 is the owner's call.
- The disk image is named `OpenFallout-<version>-macOS-arm64.dmg`.
- The outer app and the nested `Contents/Helpers/OpenFallout Engine.app` both state the same `LSMinimumSystemVersion`,
  and it is the statement's `minimum_macos`.
- Both property lists carry the version in `CFBundleShortVersionString` and `CFBundleVersion`.
- The programs this project builds (`Contents/MacOS/openfallout`, the launcher and the nested engine app's program) were
  built for exactly that minimum (the `minos` of their Mach-O `LC_BUILD_VERSION`, read with `vtool -show-build`).
- No other Mach-O file in the app (a library) was built for a newer macOS than that. An older one is fine.
- The engine has the architecture of the statement and no other.

Run in CI on a tag build, after the packaged-app check (`.github/workflows/macos.yml`, step "Write the engine-release
statement"), and by the owner on the downloaded image.

## Release notes

Two lines are required, in these words (the glossary's: *supported* is what the project checks itself, *played on* is
what the owner ran the real games on):

    - Supported: any Apple Silicon Mac, macOS <minimum_macos> or later.
    - Played on: <Mac and chip>, macOS <version>.

CI adds the first line, from the statement. The second comes from the owner at release time and is never filled in by a
tool: `CI/engine_release.py notes <statement> --played-on-mac "<Mac and chip>" --played-on-macos <version>` prints it for
the release notes of the draft. The release notes also hold the lines of the packaged-app check for the one disk image
(`CI/check_mac_package.py --notes`, see [mac-release.md](mac-release.md)): the result, the digest, the Mac it was checked
on and what the image holds (Apple Silicon, signed and notarized, built for macOS 26.0). Those lines say what was checked
and never repeat the supported or the played-on line.

## The release gate

`CI/harness_release.py` (the `verify` and `publish` phases of the Harness pipeline, [harness.md](harness.md)) refuses to
publish a release when:

- the release has no `openfallout-engine-release.json` (or it is empty, not JSON, or lacks a field);
- the statement names a disk image the release does not hold, or its `sha256` differs from the digest GitHub reports for
  the uploaded image;
- the statement's `source_commit` is not the commit of the tag, or its `tag` is not the release's;
- the notes lack the `Supported` line, say another minimum macOS than the statement, or lack the `Played on` line;
- and, as before, the checks of [mac-release.md](mac-release.md): the release holds exactly one disk image, the Apple
  Silicon one, and no package of another platform; the image has a release-mode check PASS and a smoke PASS on a Mac
  with a GPU, both for the uploaded image's digest; and the latest master run passed the one Mac job,
  `macOS / Build, test and package`.

`CI/test_harness_release.py` has one test per refusal.

## Cutting an engine release

The owner runs these steps. Nothing starts without the owner.

1. **Version.** The version in `CMakeLists.txt` is the one the release gets, and is on master. The tag is
   `openfallout-<version>`.
2. **Tag and wait.** Run the Harness pipeline `openfallout_release` in `draft` mode with that tag (or push the tag). CI
   builds, signs, notarizes and checks the disk image, writes the statement and creates a draft prerelease with the image,
   the statement and the notes. If the job fails at "Write the engine-release statement", it names what disagrees.
3. **Smoke check on a Mac with a GPU.** Download the image from the draft and run
   `CI/check_mac_package.py OpenFallout-<version>-macOS-arm64.dmg --mode release --smoke run --notes notes.md`, then
   `CI/engine_release.py verify OpenFallout-<version>-macOS-arm64.dmg openfallout-engine-release.json --tag <tag> --commit <commit>`.
4. **Played on.** Make the line with `CI/engine_release.py notes` as above. Put it and the line of step 3's `notes.md` into
   the notes of the draft.
5. **The first release only: both openings play through.** On the owner's Mac, with the release build (the disk image's
   app, not a build folder), the openings of Fallout 3 and New Vegas play through automatically with the autoplay tool
   (`scripts/openfallout/autoplay/README.md`). Tale of Two Wastelands is not part of this bar.
6. **Gate and publish.** Run the pipeline in `publish` mode (`draft` mode runs the same checks and promotes nothing). It
   refuses, naming the first thing that is missing, or promotes the draft to a public prerelease.
7. **Hand over.** The shell takes the tag and the `sha256` of the statement. The script functions it uses to save, load and
   quit are in [shell-interface.md](shell-interface.md).

## What has run, and what has not

The unit tests (`CI/test_engine_release.py`, `CI/test_harness_release.py`) run on stand-in trees and stand-in GitHub
answers.

The whole path has run on real releases. The tag `openfallout-0.1.0` (15 November 2026) was the first run of the
workflow's steps: a real notarized image went through `CI/engine_release.py write`, and the statement was downloaded
from the real draft and verified. That image aborted when it quit, which the smoke check on a Mac with a GPU found; the
draft was never published, and tags are not moved, so the first engine release is `openfallout-0.1.1` (16 November
2026). For it every step above ran: the gate's `verify` and `publish` phases passed, both openings played through on
the image's app, and the asset addresses of [The address](#the-address) answer without a login.

Not run: the Harness pipeline itself. Both releases were cut with `CI/harness_release.py` run by hand from a checkout
of the tagged commit, phase by phase, with the same arguments the pipeline gives it.
