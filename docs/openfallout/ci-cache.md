# CI build cache contract

`CI/build_cache.py` fingerprints a build environment and reconciles source file
timestamps after a workflow restores a cached build directory. It does not
transfer or save the build directory itself: the GitHub Actions cache steps of
the Mac jobs ([macos.yml](../../.github/workflows/macos.yml), called by
[push.yml](../../.github/workflows/push.yml) and
[release.yml](../../.github/workflows/release.yml)) do that. The related
test-report behavior is described in [CI reporting](ci-reporting.md).

The Mac jobs run on one runner image, `macos-26` (Apple Silicon), and build
three trees, one tree in each of three jobs that run side by side; the first two are cached. `build` is the packaged
app, built by the job `MacOS` ("Build, test and package"); its cache key starts with
`build-macos-v2-` and its fingerprint mode is the image name and build type
(`macos-26-RelWithDebInfo-ninja`). `build-tests` is the instrumented unit-test tree, built by the job `UnitTests` ("Unit tests and coverage"; Debug without `-g`,
coverage, only the two test suites; about 1.8 GB before compression, against
about 8 GB with debug information); its key starts with `build-macos-coverage-v2-`, its mode
is `macos-26-coverage-ninja`, and the job passes `--build-dir
build-tests` to `restore` and `snapshot`. The two never share a cache entry, so
ordinary objects are never restored into an instrumented build. Each job has a ccache of its own
(`hendrikmuhs/ccache-action`, keys `macos-26-app-<build type>`, `macos-26-coverage` and `macos-26-metal`), because the
three trees are compiled with different flags and share no objects; one cache for all three was always full and
evicted most of what each needed. Only `master` runs save either tree, and a release-tag
run, which does not build the test tree, saves only `build`.

All three trees are configured for Ninja (`-G Ninja`) and built with `cmake --build`. The `-ninja` ending of the two
modes keeps a tree made by the Unix Makefiles generator from being restored into a Ninja build (a different generator
in an existing build folder is a CMake error). The ccache keys did not change, but the entries did for two trees:
when debug information (`-g`, the app tree) or coverage mapping (the unit-test tree) is on, ccache also hashes the
directory the compiler runs in, and Ninja runs every compile from the build root where Make ran it from each target's
subfolder. So the first runs after the switch missed the app and unit-test caches (0% and 4.6% hits on 21 November,
against 99.94% for the Metal tree, which has neither), until a `master` run stored Ninja entries; after that they hit as
before. A run that changes the build folder layout again has the same one-time cost.

`build-metal` is the Metal build, built by the job `Metal` ("Metal build"; `CI/before_script.macos.sh -M`: Debug, `-O0`,
no coverage, the engine and the two test suites). It is never packaged and its
folder is **not** cached: it would double the space the test tree takes in the
10 GB budget, and the ccache already gives most of the saving (the rest is
linking). It uses the ccache of its job. Nearly every file of this tree is compiled with
`-DOPENFALLOUT_METAL`, so its objects are their own entries in the ccache: the
first run on `master` after the change fills them, and later runs hit them.
A release-tag run does not build it. If the first warm run of this tree takes
more than ten minutes, the fix is a cache of `build-metal` on `master` only,
with the same three steps as `build-tests` and the key start
`build-macos-metal-v1-`.

Each ccache is limited to 1,000 MB when it is stored (`ccache --max-size=1000M; ccache --cleanup` as the last step of
its job). ccache identifies the compiler by a hash of `xcrun clang -v` (`CCACHE_COMPILERCHECK=string:...`, set by
`.github/actions/mac-build-setup`), not by the compiler's file time: the hosted pool hands out more than one runner
image with the same Xcode but different file times, and with the default check a cache saved on one image missed on
the other (18-20% hits instead of 99.8%). The Metal build
also has a small cache of its own: the OpenSceneGraph for OpenGL ES 3 it needs
(about 19 MB). Its key is the output of `CI/macos/osg/osg_gles3.py key`, not
`CI/build_cache.py`; see [OpenSceneGraph for OpenGL ES 3](osg-gles3.md). It is
saved as soon as it is built, by any run, so that a later failure does not lose
it.

## Working directory and cache identity

Run the script from the checkout root. The command uses `Path.cwd()` as the
checkout root, and `git ls-files` is run there. The workflows run these commands
from the repository root. A different current directory changes the root and
the interpretation of relative arguments.

The `key` command prints a SHA-256 fingerprint of:

- the canonical absolute checkout root;
- the supplied mode and `ImageVersion` environment value;
- the resolved compiler path and its `--version` output;
- `cmake --version`, and the SDK path when `xcrun` is available;
- `SDKROOT`, `DEVELOPER_DIR`, `CC`, `CXX`, `CFLAGS`, `CXXFLAGS`, and `LDFLAGS`;
- contents of the tracked files that decide how a build folder was made (below); and
- the contents of each explicitly supplied dependency file.

The files in the fingerprint are every `CMakeLists.txt` and `.cmake` file, plus the explicit list `KEY_FILES` in
`CI/build_cache.py`, each with a comment there saying why: `CI/before_script.macos.sh` (the configure options),
`CI/macos/build.sh` (the make call), `CI/macos/deps_versions.sh` (sourced by the configure script),
`CI/before_install.macos.sh`, `CI/macos/fetch_deps_manifest.sh`, `CI/macos/build_luajit_hrt.sh` and its patch (they
produce the dependency trees, and the LuaJIT build is not described by the manifest), `CI/macos/angle/angle.json`,
`CI/build_cache.py` itself and `.github/workflows/macos.yml`.
Nothing else under `CI/` is in it, and neither are the other workflow files or the shared setup action (it only installs the dependency set, whose own cache key hashes its scripts, and ccache, which the build folder does not hold). The reasoning: the Configure step re-runs
CMake on every run, and `restore` gives changed sources a newer timestamp, so a changed CMake file or source is
corrected by the build itself. What neither can correct is the way the tools and dependencies were produced and how
the tree is restored, which is what the listed files do. The checkers, report and release scripts, the Metal, OSG and
ANGLE builders (the Metal tree is not cached), the tests and the other workflows are never run by the Configure or
Build steps of a cached tree, so they cannot change the folder. A pull request that only changes them restores the
folder of its base. Changing the meaning of the key means changing the prefix (`v1` became `v2` when the list was
narrowed).

This is deliberately tied to the absolute checkout path because generated
build rules and toolchain state contain absolute paths. The snapshot also
records that same canonical root, so a snapshot from another checkout path is
incompatible. The build directory itself is not in the fingerprint. Keep the
build directory path consistent when using a custom `--build-dir`; callers
must ensure the cache key and build directory refer to the same environment.

The fingerprint is only useful if the caller uses it to namespace the external
build cache. `restore` does not recompute or validate compiler, mode, dependency,
or configure-option identity. It verifies the snapshot version and checkout
root, then reconciles tracked source files. Untracked files are not included in
the source snapshot or tracked CMake-rule fingerprint. Dependency files are
hashed only when explicitly passed to `key`; relative dependency paths are
resolved from the current working directory.

The CLI accepts `key`, `snapshot`, and `restore`. `--build-dir` defaults to
`build`; it must resolve to a directory beneath the checkout and the argument
itself cannot be a symlink. This keeps the cleanup performed by `restore`
confined to an in-checkout build directory. `--compiler`, repeated
`--dependency-file`, and `--mode` affect the key command.

## Snapshot and restore sequence

The intended sequence is:

1. Compute `key` before asking the external cache to restore a build directory.
2. After the external cache restore, run `restore` before CMake configuration
   and the build.
3. After a successful configuration/build, run `snapshot` before saving the
   build directory to the external cache.

`snapshot` walks paths reported by `git ls-files`. For each tracked regular
file that is not a symlink, it records the SHA-256 content digest and
nanosecond modification time in `build/.ci-source-state.json`, along with
snapshot version 1 and the absolute checkout root. It creates the build
directory if needed. It records source metadata only; the workflow's cache
action stores the build outputs.

`restore` handles the cache as follows:

1. If the build directory does not exist, it prints that no incremental cache
   was restored and returns without creating or deleting anything.
2. It reads and validates `.ci-source-state.json`: version must be 1, the
   recorded root must match, and `files` must be a mapping whose entries hold a
   string `sha256` value and integer `mtime_ns`.
3. It enumerates current tracked regular non-symlink files. If any path present
   in the snapshot is no longer in that set, it discards the cached build tree.
   This also covers a formerly regular tracked path that is now missing or a
   symlink. A newly tracked file does not by itself invalidate the tree.
4. Otherwise, for each current tracked file, it compares the content digest.
   Unchanged files get their recorded modification time back. Changed and new
   files receive a common time at least one second newer than the newest file
   in the restored build tree, or the current time if that is later. This makes
   normal build dependency checks see those inputs as newer than restored
   outputs, even if checkout timestamps were old. The file's current access
   time is retained.

The build directory is deleted only in these two restore cases: the snapshot is
missing, unreadable, malformed, at an unsupported version, or for a different
checkout root; or the snapshot contains a tracked regular file that is no
longer present as a current tracked regular non-symlink file. Removal is
intentional: CMake may have copied a source-only resource into the build tree,
and retaining that output after its source was removed could ship stale content.
Deletion removes the entire selected build directory; the script then returns
and leaves the next configure/build to create a fresh one. A missing build
directory is a no-op. Changed or newly added files cause timestamp updates, not
directory deletion.

## Safe use and recovery

- Run from the repository root, use the same canonical checkout path across
  cache save and restore, and keep the selected build directory consistent.
- Let the workflow's environment fingerprint determine the external cache
  namespace. Do not assume the snapshot metadata alone proves compiler,
  dependency, generator, or configure-option compatibility.
- Run `restore` only after the intended cache action has restored the intended
  in-checkout build directory. The deletion cases above remove that whole
  directory, not just `.ci-source-state.json`.
- If no cache is available, configure and build normally. If the metadata is
  invalid or a tracked file was removed, the script discards the cached tree;
  configure and build normally from an empty build directory. If changed files
  are detected, allow the regular build system to rebuild their dependents.
- `snapshot` is not a repair operation: take it after a successful build so its
  hashes and timestamps describe the source state associated with that build.

The implementation's unit tests use temporary repositories and build
directories. They cover unchanged checkout reuse, changed header/source
rebuilds despite stale timestamps, key changes, removed resources, and
incompatible snapshots. This documentation reports those as unit-test coverage;
it does not claim a live hosted cache restore, manual play, automated in-game
evidence, or engine parity.
