#!/bin/bash -e

# Builds OpenSceneGraph for OpenGL ES 3 (the Metal build of the engine draws with it) and installs it.
# Docs: docs/openfallout/osg-gles3.md. The version is fixed in osg_version.sh, the changes to OpenSceneGraph are the
# files in patches/, the libraries and plugins come from the engine's own lists (osg_gles3.py).
#
#   build_osg_gles3.sh --angle-root DIR --prefix-path PATH --work DIR --install DIR [--jobs N]
#
#   --angle-root   ANGLE's folder: lib/libEGL.dylib, lib/libGLESv2.dylib, include/{EGL,GLES2,GLES3,KHR}
#   --prefix-path  where libpng, libjpeg, zlib, freetype and collada-dom are (the dependency set's installed tree in CI,
#                  /opt/homebrew on a development Mac); a CMake path list
#   --work         a folder for the download, the source and the build tree (made if missing)
#   --install      the install prefix (made if missing)
#   --jobs         build jobs (default: the number of cores)
# Result: Apple Silicon only, macOS 26.0 or later, dynamic libraries, no link to Apple's OpenGL framework.

# also when it is started as `bash build_osg_gles3.sh`, where the -e of the first line does not count
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

usage() {
    sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

ANGLE_ROOT="" PREFIX_PATH="" WORK="" INSTALL="" JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 3)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --angle-root) ANGLE_ROOT="$2"; shift 2 ;;
        --prefix-path) PREFIX_PATH="$2"; shift 2 ;;
        --work) WORK="$2"; shift 2 ;;
        --install) INSTALL="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        *) usage ;;
    esac
done
for name in ANGLE_ROOT PREFIX_PATH WORK INSTALL; do
    [[ -n "${!name}" ]] || { echo "missing --$(echo "$name" | tr 'A-Z_' 'a-z-')" >&2; usage; }
done
for file in lib/libEGL.dylib lib/libGLESv2.dylib include/GLES3/gl3.h include/EGL/egl.h; do
    [[ -f "$ANGLE_ROOT/$file" ]] || { echo "not an ANGLE folder, $ANGLE_ROOT/$file is missing" >&2; exit 1; }
done

source "$HERE/osg_version.sh"
mkdir -p "$WORK" "$INSTALL"
WORK="$(cd "$WORK" && pwd)" INSTALL="$(cd "$INSTALL" && pwd)" ANGLE_ROOT="$(cd "$ANGLE_ROOT" && pwd)"
START=$SECONDS

# 1. What the engine uses, from its own lists. First, so that a list the helper cannot read stops the build before
# anything is fetched; and read into variables, so that the shell sees the helper fail.
SKIP="$(python3 "$HERE/osg_gles3.py" skip --root "$REPO")"
PLUGIN_OPTIONS="$(python3 "$HERE/osg_gles3.py" plugin-options --root "$REPO")"
declare -a PLUGINS=()
while IFS= read -r option; do
    [[ -z "$option" ]] || PLUGINS+=("$option")
done <<< "$PLUGIN_OPTIONS"
[[ ${#PLUGINS[@]} -gt 0 ]] || { echo "the engine's plugin list is empty" >&2; exit 1; }
echo "Libraries not built: ${SKIP:-none}"
echo "Plugins: ${PLUGINS[*]}"

# 2. The source: the pinned archive, checked, with the project's patches applied in order
ARCHIVE="$WORK/osg-$OSG_COMMIT.tar.gz"
if [[ ! -f "$ARCHIVE" ]]; then
    curl --fail --retry 3 -sSL "$OSG_REPOSITORY/archive/$OSG_COMMIT.tar.gz" -o "$ARCHIVE"
fi
echo "$OSG_ARCHIVE_SHA256  $ARCHIVE" | shasum -a 256 -c -
rm -rf "$WORK/src" "$WORK/build"
mkdir "$WORK/src"
tar -xzf "$ARCHIVE" -C "$WORK/src" --strip-components=1
for patch in "$HERE"/patches/*.patch; do
    echo "Applying $(basename "$patch")"
    patch -p1 -N -s -d "$WORK/src" < "$patch"
done

# 3. Configure, build, install. OpenGL ES 3 from ANGLE (libGLESv2, libEGL), no windowing system (the engine's window is
# SDL's), no applications, only the plugins the engine lists, picture plugins on libpng and libjpeg.
# CMAKE_FIND_FRAMEWORK/APPBUNDLE LAST: a framework on the machine (Mono's, for one) holds old png and jpeg headers that
# CMake would find before the prefix path. (NEVER is not possible: the Carbon and Cocoa frameworks are found this way.) OSG_CPP_EXCEPTIONS_AVAILABLE=ON: OpenSceneGraph turns it off for an OpenGL ES
# profile, which would also skip the dae plugin and change how the png plugin reports a bad file; the desktop build has it on.
GENERATOR="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GENERATOR="Ninja"
cmake -S "$WORK/src" -B "$WORK/build" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_STANDARD=11 \
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH" \
    -DCMAKE_FIND_FRAMEWORK=LAST \
    -DCMAKE_FIND_APPBUNDLE=LAST \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DOPENGL_PROFILE=GLES3 \
    "-DOPENGL_HEADER1=#include <GLES3/gl3.h>" \
    -DOPENGL_HEADER2= \
    -DOSG_KEEP_OPENGL_LIBRARY=ON \
    -DOPENGL_INCLUDE_DIR="$ANGLE_ROOT/include" \
    -DOPENGL_gl_LIBRARY="$ANGLE_ROOT/lib/libGLESv2.dylib" \
    -DEGL_INCLUDE_DIR="$ANGLE_ROOT/include" \
    -DEGL_LIBRARY="$ANGLE_ROOT/lib/libEGL.dylib" \
    -DOSG_WINDOWING_SYSTEM=None \
    -DOSG_CPP_EXCEPTIONS_AVAILABLE=ON \
    -DOSG_TEXT_USE_FONTCONFIG=OFF \
    -DBUILD_OSG_APPLICATIONS=OFF \
    -DBUILD_OSG_EXAMPLES=OFF \
    -DBUILD_DOCUMENTATION=OFF \
    -DBUILD_DASHBOARD_REPORTS=OFF \
    -DBUILD_OSG_DEPRECATED_SERIALIZERS=OFF \
    -DBUILD_OSG_PLUGINS_BY_DEFAULT=OFF \
    "${PLUGINS[@]}" \
    "-DOSG_SKIP_LIBRARIES=$SKIP"
cmake --build "$WORK/build" --target install -j "$JOBS"

# 4. Check the result: the library and every plugin that was asked for are there, and nothing links Apple's OpenGL
# framework
BAD=0
INSTALLED="$(find "$INSTALL/lib" \( -name '*.dylib' -o -name '*.so' \) | sort)"
for wanted in libosg.dylib libosgDB.dylib libosgViewer.dylib; do
    grep -q "/$wanted\$" <<< "$INSTALLED" || { echo "FAIL $wanted is not installed" >&2; BAD=1; }
done
for option in "${PLUGINS[@]}"; do
    plugin="${option#-DBUILD_OSG_PLUGIN_}"
    plugin="osgdb_$(echo "${plugin%=ON}" | tr 'A-Z' 'a-z').so"
    grep -q "/$plugin\$" <<< "$INSTALLED" || { echo "FAIL the plugin $plugin is not installed" >&2; BAD=1; }
done
while IFS= read -r file; do
    if otool -L "$file" 2>/dev/null | grep -q "OpenGL.framework"; then
        echo "FAIL $file links Apple's OpenGL framework" >&2
        BAD=1
    fi
done <<< "$INSTALLED"
[[ $BAD -eq 0 ]] || exit 1

{
    echo "OpenSceneGraph for OpenGL ES 3, built by CI/macos/osg/build_osg_gles3.sh"
    echo "commit $OSG_COMMIT"
    for patch in "$HERE"/patches/*.patch; do echo "patch $(basename "$patch")"; done
    echo "skipped libraries: ${SKIP:-none}"
} > "$INSTALL/osg-gles3-build.txt"
echo "Built and installed to $INSTALL in $((SECONDS - START)) s"
