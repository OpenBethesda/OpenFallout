# ANGLE: the project's own build, its release asset, and how the app carries it

The translated Metal renderer draws through ANGLE: the engine writes OpenGL ES 3.0, and ANGLE's Metal backend turns it
into Metal (spec #210, milestone two, stories 21 and 22). The project builds ANGLE itself, at one fixed version, with
the Metal backend only. No borrowed copy ships. This page says what the build gives, how it is made again, how CI gets
it, and how it goes into the packaged app.

## The release asset

One build of ANGLE is one release asset of this repository, a `.tar.gz` file. Unpacked, it is the folder that
`-DOPENFALLOUT_ANGLE_ROOT` names:

| Path | What it is |
| --- | --- |
| `lib/libEGL.dylib`, `lib/libGLESv2.dylib` | the two libraries: arm64 only, built for macOS 26.0, install names `@rpath/libEGL.dylib` and `@rpath/libGLESv2.dylib`, without the linker's debug map, ad hoc signed, no link to Apple's OpenGL framework |
| `include/EGL`, `include/GLES2`, `include/GLES3`, `include/KHR` | ANGLE's headers |
| `NOTICES` | the licences of everything linked into the two libraries (below) |
| `BUILDINFO.json` | the commit, the build arguments, the compiler, SDK and Xcode the build used, the linked components |
| `SHA256SUMS` | the SHA-256 of every other file, as `shasum -a 256 -c SHA256SUMS` reads it |

The version file `CI/macos/angle/angle.json` records everything about it: the ANGLE repository, branch and commit; the
depot_tools commit; the build arguments; the release tag and asset name; the SHA-256 of the archive; and for each
library and the notices the digests below. The archive is the same byte for byte whenever its files are (sorted
entries, no owner, time 0, fixed modes, a gzip header without a time).

**Naming.** The tag is `angle-chromium-<branch number>-<first 12 characters of the commit>-r<n>` and the asset
`angle-chromium-<branch number>-<commit>-macos-arm64.tar.gz`, for example tag `angle-chromium-8127-1f6526a566c2-r1`. `r<n>`
counts new assets of the same ANGLE commit (a fixed notices file, other build arguments): tags are never moved, so a
new asset gets a new tag. The tag never starts with `openfallout-`, so it starts no engine release build
(`release.yml`). Publish it as a prerelease, so that it never becomes the repository's "latest" release.

**The build arguments**, Metal backend only (without `angle_enable_wgpu=false` the libraries are three times larger):

    is_debug=false target_cpu="arm64" is_component_build=false
    angle_enable_metal=true angle_enable_gl=false angle_enable_vulkan=false
    angle_enable_swiftshader=false angle_enable_wgpu=false angle_enable_null=false
    angle_build_tests=false use_siso=false
    mac_deployment_target="26.0" mac_min_system_version="26.0"

## Building it again

The build runs on a Mac, once per ANGLE version. The checkout is about 13 GB and the build folder about 600 MB, too
large for a free hosted runner, so CI never builds ANGLE.

    CI/macos/angle/build_angle.py WORK [--checkout DIR] [--record]

It fetches depot_tools and ANGLE at the commits in `angle.json` into `WORK` (or uses an existing, synced checkout at the
recorded commit given with `--checkout`; it refuses one at another commit), generates the build folder `out/OpenFallout`
with ANGLE's own `buildtools/mac/gn` (the `gn` of depot_tools refuses to run with `DEPOT_TOOLS_UPDATE=0`) and builds
`libEGL` and `libGLESv2` with `third_party/ninja/ninja`. Then it assembles the asset in `WORK/asset` and the archive
`WORK/<asset name>`, checks the libraries (arm64 only, built for macOS 26.0, `@rpath` install names, no
`OpenGL.framework`, valid signature) and prints the digests. `--record` writes the digests into `angle.json`. The
compile (1,324 steps) took 2.5 minutes on an otherwise idle Apple M3 Pro with 6 jobs and 10 minutes on a busy one, with
about 2 GB of memory; on the owner's Mac run it through the shared build lock.

**The same code gives the same asset.** The compiler's output is reproducible; the linked libraries are not quite,
because the linker writes a debug map into the symbol table (the absolute path of every object file on the build Mac,
with its time) and derives the library's UUID from all of it. That map would also publish the build Mac's folder
names. So the script removes it (`strip -S`; the exported symbols stay) and sets the UUID to a hash of the library's own
bytes. Two builds of the same commit with the same arguments, in different folders and on different days, then give
the same archive byte for byte.

**Changing the version.** Pick a commit on a Chromium release branch that matches a stable Chrome release. In
`angle.json` set `angle.branch` and `angle.commit`, the tag and the asset name (branch number, commit, `-r1`), and a
current depot_tools commit. Run the build with `--record` into an empty work folder, run the unit tests and the checks
below, commit `angle.json`, and publish the archive under the new tag. Then CI takes the new asset. The same steps with
`r<n+1>` give a new asset of the same commit.

## The notices

ANGLE ships no notices file, so the build script writes one. It does not guess the list: it reads the build graph
(`ninja -t query`): the object files and static archives each library links, the objects inside those archives, and the
source file of each object. Each source belongs to the innermost `third_party/<name>` folder above it, or to ANGLE
itself. Each component's licence file comes from its `README.chromium` (`License File:`), or else from a `LICENSE`,
`COPYING` or `NOTICE` file in its folder. A linked component without a licence file stops the build.

For the recorded commit and arguments the libraries link code from:

| Component | Licence |
| --- | --- |
| ANGLE | BSD 3-Clause |
| Abseil (`third_party/abseil-cpp`) | Apache 2.0 |
| zlib (`third_party/zlib`) | zlib |
| libc++ and libc++abi (`third_party/libc++`, `third_party/libc++abi`: Chromium's build links its own C++ library into each library) | Apache 2.0 with LLVM exceptions |
| Perfetto (`third_party/perfetto`, the tracing client) | Apache 2.0 |
| astc-encoder (`third_party/astc-encoder`) | Apache 2.0 |
| xxHash (`src/common/third_party/xxhash`) | BSD 2-Clause |

The packaged app carries the notices as `Contents/Resources/ANGLE-NOTICES.txt`.

## How CI gets it

    CI/angle_asset.py fetch DEST

downloads the asset from `https://github.com/OpenBethesda/OpenFallout/releases/download/<tag>/<asset>`, checks the
archive's SHA-256 against `angle.json`, unpacks it into `DEST` (new or empty; plain files only, nothing outside it),
checks every file against `SHA256SUMS` and the libraries and the notices against `angle.json`. A mismatch fails with
`FAIL angle: ...` and leaves `DEST` empty. It never rebuilds ANGLE. `DEST` is then the `-DOPENFALLOUT_ANGLE_ROOT` of the
Metal build. `CI/angle_asset.py digest <library>` prints the two digests of a library.

## In the packaged app, and how it is signed

`cmake/OpenFalloutAngle.cmake` puts the two libraries, unchanged, into `OpenFallout.app/Contents/Frameworks` and the
notices into `Contents/Resources` when the Metal build (`OPENFALLOUT_METAL`) or the ANGLE test build
(`OPENFALLOUT_BUNDLE_ANGLE`, below) is packaged. The top-level `CMakeLists.txt` includes it after the Apple bundling
block, because `fixup_bundle` rewrites the install name and deletes the rpaths of every library it finds in the bundle;
copied in after it, the libraries keep their bytes and their `@rpath` names. CPack's signing step
(`cmake/SignMacApplications.cmake`) then signs each library on its own with the project's Developer ID, the hardened
runtime and a secure timestamp, before the app. Libraries take their entitlements from the process that loads them.

**Matching a signed library to the published one.** Signing rewrites a library's signature and the two size fields of
its `__LINKEDIT` segment, so its SHA-256 changes. `angle.json` therefore records for each library, beside the plain
SHA-256, a *digest without signature*: the SHA-256 of the file up to the start of its signature, with the
`LC_CODE_SIGNATURE` load command taken out and those two size fields set to zero. It is the same for the asset's ad
hoc signature, the linker's signature and a Developer ID signature, and changes with every other byte, the install name
included. This was checked on the real libraries (the linker's signature, ad hoc, hardened ad hoc with another
identifier) and in `CI/test_angle_asset.py` on a library whose signature is made to span more pages, as a Developer ID
signature does. `codesign --remove-signature` is not used: it leaves the segment's size from the larger signature
behind, so its result depends on the signature that was removed.

**The packaged-app check** (`CI/check_mac_package.py`) grows two steps for milestone two, both off for today's OpenGL
release:

- `--angle bundled`: both libraries are in `Contents/Frameworks` and are the published ones (digest without signature);
  in release mode each is signed by a Developer ID Application identity of the app's team with the hardened runtime and
  a secure timestamp; `ANGLE-NOTICES.txt` is the published notices file. With the ANGLE test build it also runs the
  probe (below): the `angle-probe` step.
- `--forbid-opengl`: no Mach-O file in the app links Apple's OpenGL framework. It is for after the desktop OpenGL path
  is deleted.

## The early notarized test

Before the rest of the milestone relies on it, one real notarized app must show that the bundled libraries load and
that ANGLE's Metal backend gives an OpenGL ES 3.0 context under the hardened runtime. The Metal build does not exist
yet, so the test uses today's OpenGL app with `-DOPENFALLOUT_BUNDLE_ANGLE=ON`: the libraries and the notices are
packaged as in the Metal build, and so is `Contents/MacOS/openfallout-angle-probe` (`CI/macos/angle/angle_probe.c`), a
small C program. The probe opens ANGLE's Metal display with a 64 x 64 pbuffer surface (no window), asks for an OpenGL
ES 3.0 context, prints the EGL and GL version and renderer strings, compiles a GLSL ES 3.00 program (ANGLE turns it into
a Metal library while the program runs), draws a triangle and reads one pixel back. It ends with `ANGLE probe: PASS`.
It is signed like the engine, with the app's entitlements, and is never part of a release.

**The run that makes the image.** Run the workflow "Build and test" (`push.yml`) by hand on the default branch with
`bundle-angle` ticked. It is the only run that both signs and notarizes (the `macos-release` environment) and bundles
ANGLE: it downloads and verifies the asset (`Fetch the ANGLE libraries`), configures with
`CI/before_script.macos.sh -A <folder>`, and checks the image with `--angle bundled` in release mode. The image is the
job's `openfallout-macos-dmg-arm64-<commit>` artifact; no release is made. Every other build passes
`-DOPENFALLOUT_BUNDLE_ANGLE=OFF` explicitly, and the run stores no build tree for later runs, so the test build cannot
leak into a release. The asset must be published first.

**The proof** runs on a Mac with a GPU, because a hosted runner has none (its probe step reports a machine without a
Metal device as skipped; a library the loader refuses fails there too). Download the artifact, open the image, and run

    CI/check_mac_package.py OpenFallout-<version>-macOS-arm64.dmg --mode release --angle bundled --smoke run

It passes when, among the others, the `angle` and `angle-probe` steps pass: `angle-probe` prints the GL version and
renderer, for example `OpenGL ES 3.0 (ANGLE 2.1.1 git hash: 1f6526a566c2); ANGLE (Apple, ANGLE Metal Renderer: Apple
M3 Pro, ...)`. That also answers whether ANGLE's Metal backend needs an entitlement under the hardened runtime: the
probe carries only the app's entitlements (`allow-jit`).

## What has run, and what has not

On 16 November 2026, on an Apple M3 Pro with macOS 27.2.1 and Xcode 27.0:

- `build_angle.py --checkout <existing checkout> --record` ran on a checkout at the recorded commit that was synced on
  14 November (so the fetch step did not run), after a fresh compile into `out/OpenFallout`. It recorded the asset in
  `angle.json`: 2,099,957 bytes, the libraries 103,216 bytes (`libEGL`) and 6,056,608 bytes (`libGLESv2`).
- The same assembly from a build made on 14 November into another folder gave the same archive SHA-256. Before the
  debug map was stripped the two builds differed only in it and in the UUID; code and data were the same.
- `CI/angle_asset.py fetch` unpacked the archive from a `file://` address, and the result equals the built asset.
- The probe ran against the asset's libraries: OpenGL ES 3.0 on `ANGLE Metal Renderer: Apple M3 Pro`, the triangle
  drawn and read back. It also ran from a small app layout with the libraries in `Contents/Frameworks`, signed hardened
  ad hoc with only the app's entitlements and, because an ad hoc signature has no Team ID, the local-test entitlement
  that turns library validation off. With library validation on, the loader refused the ad hoc libraries ("different
  Team IDs"), as expected. The libraries signed again kept their recorded digest without signature, and the package
  check's `angle`, `angle-probe` and `no-opengl` steps passed on that layout.
- `CI/test_angle_bundle.py` installed a stand-in app for real (CMake's install with `fixup_bundle`): the libraries
  arrived byte for byte, and a stand-in engine linked to `@rpath/libGLESv2.dylib` loaded the copy in the app.

Not run yet: publishing the asset (the owner's word is needed), the Developer ID signed and notarized test image, and
the probe inside it on a Mac with a GPU. A fresh fetch of the sources (`gclient sync`) was not repeated either; the
recorded depot_tools commit is the one the 14 November checkout used. Whether a fresh Mac needs Apple's separate Metal
toolchain download to build ANGLE is still open.
