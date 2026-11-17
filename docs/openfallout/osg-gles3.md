# OpenSceneGraph for OpenGL ES 3

The Metal build of the engine draws with OpenSceneGraph built for OpenGL ES 3. This page says what that build is, why
the project makes it, what is changed in it, how to make it again and how its version changes. The route is described in
the Mac Native spec (milestone two): the engine writes OpenGL ES 3.0 and ANGLE's Metal backend turns it into Metal.

## What it is

- OpenMW's fork of OpenSceneGraph, branch 3.6, at one commit. The commit and the SHA-256 of its source archive are in
  `CI/macos/osg/osg_version.sh`. It is the commit the desktop OpenGL build's dependency set is made from, so both builds
  run the same OpenSceneGraph code.
- Built with `OPENGL_PROFILE=GLES3`, against ANGLE's `libGLESv2.dylib` and `libEGL.dylib` and ANGLE's headers. No window
  system: the window is SDL's. No applications and no examples.
- Apple Silicon only, built for macOS 26.0 or later. Release, dynamic libraries.
- C++ exceptions are on (`OSG_CPP_EXCEPTIONS_AVAILABLE=ON`). OpenSceneGraph turns them off for an OpenGL ES profile, which
  would drop the `dae` plugin and change how the png plugin reports a bad file; the desktop build has them on.
- Only what the engine uses. The libraries come from `USED_OSG_COMPONENTS` and the plugins from `USED_OSG_PLUGINS` in the
  root `CMakeLists.txt`, plus the libraries those link. So a new component in the engine's list is built the next time,
  and the cache key (below) changes with it.
  - Libraries: `osg`, `osgDB`, `osgUtil`, `osgGA`, `osgText`, `osgViewer`, `osgAnimation`, `osgFX`, `osgParticle`,
    `osgShadow`, `osgSim` and `OpenThreads`, with their serializer plugins (`osgdb_serializers_*`).
  - Not built: `osgManipulator`, `osgPresentation`, `osgTerrain`, `osgUI`, `osgVolume`, `osgWidget`.
  - Plugins: `bmp`, `dae`, `dds`, `freetype`, `jpeg`, `osg`, `png`, `tga`. The picture plugins use libpng and libjpeg,
    as in the desktop OpenGL build. There is no ImageIO plugin.
- Nothing in it links Apple's OpenGL framework. The build script checks every library and plugin it installs and fails
  if one does. The libraries link ANGLE's two libraries as `@rpath/libGLESv2.dylib` and `@rpath/libEGL.dylib`; the
  packaged app puts them in `Contents/Frameworks` (packaging is the ANGLE work item's).
- The libraries carry no run path, as in the desktop set. The program that loads them (the engine, the unit tests) gets
  the folders of OpenSceneGraph, ANGLE and the dependency set from CMake, because it links libraries in them.

## Why the project builds it itself

The dependency set that CI downloads (openmw-deps, built with vcpkg) holds an OpenSceneGraph for desktop OpenGL.
OpenSceneGraph picks its OpenGL profile when it is built, so that one cannot be used for OpenGL ES 3. No prebuilt
OpenSceneGraph for OpenGL ES 3 on the Mac exists. The Metal build needs its own, and it needs the same version as the
other build, so the project builds the fork at the same commit with a different profile.

It is built once and cached (see below), not on every run. On this development Mac it took 173 seconds on 8 jobs and
installs 19 MB. It is not part of the packaged app until the packaged app
changes to Metal.

## The patches

`CI/macos/osg/patches/` holds the project's changes to OpenSceneGraph. Each file starts with the reason it is needed.
They are applied in number order to the unpacked source.

| Patch | What and why |
|---|---|
| `0001-macos-use-libpng-and-libjpeg-plugins` | On a Mac, read pictures with the libpng and libjpeg plugins and not with Apple's ImageIO plugin, so that both builds decode a picture with the same code. The openmw-deps project makes the same change for its Mac build. |
| `0002-keep-given-opengl-library` | A Mac build of OpenSceneGraph forgets the OpenGL library and header folder given on the command line and looks for Apple's framework. The new option `OSG_KEEP_OPENGL_LIBRARY` keeps them, so ANGLE's are used. |
| `0003-skip-unused-libraries` | The new list `OSG_SKIP_LIBRARIES` names libraries that are not built or installed (with their serializers). |
| `0004-screen-capture-reads-rgba-on-es` | The screenshot handler reads the frame back as `GL_RGB`, which OpenGL ES refuses. In an OpenGL ES profile it reads `GL_RGBA`. The engine's screenshots, and the picture comparison, use this handler. |

Every change to OpenSceneGraph that the Metal build needs goes here as a numbered patch with its reason. A change
that is not needed is not made.

## Dynamic, not static

The packaged app already carries OpenSceneGraph as dynamic libraries and plugins: the bundle step copies the libraries
the engine links and the plugins named in `USED_OSG_PLUGINS` into the app and rewrites their install names. The Metal
build is packaged the same way, so it needs no new packaging code. A static build would instead need the engine's
`OSG_STATIC` path, which builds OpenSceneGraph as part of the engine and links its plugins in. That path is not used
on the Mac today.

## Make it again

On a Mac with CMake, Ninja or Make, and the picture libraries (Homebrew's, or the dependency set's installed tree):

```
CI/macos/osg/build_osg_gles3.sh \
    --angle-root <ANGLE folder: lib/, include/> \
    --prefix-path <folder that holds libpng, libjpeg, zlib, freetype, collada-dom> \
    --work <a work folder> \
    --install <the install folder>
```

It downloads the pinned source archive, checks its SHA-256, applies the patches, configures, builds and installs, and
checks that nothing links Apple's OpenGL framework. Then give the install folder to the engine's configure step as
the front of `CMAKE_PREFIX_PATH` (and as `OSG_DIR`) together with `-DOPENFALLOUT_METAL=ON` and
`-DOPENFALLOUT_ANGLE_ROOT=<ANGLE folder>`. In CI, `CI/macos/before_install_osg.sh` runs the script and
`CI/before_script.macos.sh -M` configures the Metal tree.

## In the Mac job

The steps are one block named "Metal build: ..." in `.github/workflows/macos.yml`, after every step that makes, checks
or reports the packaged app, so a failure there never hides those results. A release tag run skips the whole block (the
packaged app is still the OpenGL build). In order:

1. Fingerprint: `python3 CI/macos/osg/osg_gles3.py key` with four `--input` files (see the cache key below).
2. Restore the install (`/tmp/openfallout-osg-gles3`, 19 MB) from the cache of that key.
3. Fetch the ANGLE asset into `/tmp/openfallout-angle` (`CI/angle_asset.py fetch`), unless the manual ANGLE test run
   already did: both read the one folder, and the fetch never changes it.
4. Only when the cache missed: build the library (`CI/macos/before_install_osg.sh`, about 8 to 10 minutes), then save the
   install at once.
5. Configure `build-metal` (`CI/before_script.macos.sh -C -k -M`), build the engine and the two test suites in it
   with the job's ccache, and run both suites. The results are pass or fail only (see [CI reporting](ci-reporting.md)).

The ANGLE release named in `CI/macos/angle/angle.json` must be published before this block can pass: until then the
fetch step fails with the address it could not read, and the steps after it do not run.

## When it is built again (the cache key)

`python3 CI/macos/osg/osg_gles3.py key --input <file>...` prints the SHA-256 that names the cached build. It covers the
commit file, every patch, the build script, the helper, the engine's two component lists and the files given with
`--input`. The Mac job gives four: `CI/macos/angle/angle.json` (the ANGLE record), the dependency set's manifest,
`CI/macos/before_install_osg.sh` (it chooses the build's folders and arguments) and the output of `clang --version`
(a new compiler on the runner builds it again). A change to any of them builds it again. A change to anything else in
the repository does not.

## How the version changes

1. Choose the new commit on OpenMW's branch (or a new branch). Check what the commit changes in the files the patches
   touch.
2. Put the commit and the SHA-256 of `<repository>/archive/<commit>.tar.gz` into `osg_version.sh`.
3. Run the build script. If a patch no longer applies, write it again against the new source, with its reason.
4. Run `python3 -m unittest CI.test_osg_gles3` and the Metal tree's unit tests.
5. When the desktop OpenGL build's dependency set moves to the same commit, say so in the commit message. The two
   builds should stay on one commit.
