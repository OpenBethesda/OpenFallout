#!/bin/bash -e

VERBOSE=""
USE_CCACHE=""
KEEP=""
USE_WERROR=""
TEST_TREE=""
METAL_TREE=""
COVERAGE_APP_TREE=""
BUILD_DIR="build"
DEPENDENCIES_ROOT_PATH="/tmp/openmw-deps"
BUNDLE_ANGLE_ROOT=""
# The Metal build (-M): ANGLE (CI/angle_asset.py fetch puts it here) and OpenSceneGraph built for OpenGL ES 3
# (CI/macos/before_install_osg.sh puts it here)
ANGLE_ROOT_PATH="/tmp/openfallout-angle"
OSG_GLES3_PREFIX="/tmp/openfallout-osg-gles3"

# before the build folder becomes the working directory
HERE="$(cd "$(dirname "$0")" && pwd)"

source ./CI/macos/deps_versions.sh

while getopts VCkETMPd:a:o:A: ARG
do
    case $ARG in
	V )
	    VERBOSE=true ;;

	C )
	    USE_CCACHE=true ;;

	k )
	    KEEP=true ;;

	E )
	    USE_WERROR=true ;;

	T )
	    TEST_TREE=true
	    BUILD_DIR="build-tests" ;;

	M )
	    METAL_TREE=true
	    BUILD_DIR="build-metal" ;;

	P )
	    COVERAGE_APP_TREE=true
	    BUILD_DIR="build-coverage-app" ;;

        a )
            ANGLE_ROOT_PATH="$OPTARG"
            ;;

        o )
            OSG_GLES3_PREFIX="$OPTARG"
            ;;

        d )
            DEPENDENCIES_ROOT_PATH="$OPTARG"
            ;;

	A )
	    BUNDLE_ANGLE_ROOT="$OPTARG" ;;

	* )
	    cat <<EOF
Usage: $0 [-VCkETMPdaoA]
Options:
	-C
		Use ccache.
        -d <dir>
                This folder points to the openmw-deps (e.g. /tmp/openmw-deps).
	-k
		Keep the old build directory, default is to delete it.
	-V
		Run verbosely
	-E
		Use warnings as errors (-Werror)
	-T
		Configure the unit-test tree in build-tests instead: Debug, coverage, the components
		and OpenFallout test suites. It is never packaged.
	-A <dir>
		The early notarized test of ANGLE (docs/openfallout/angle.md): package the ANGLE
		libraries unpacked in <dir> and the ANGLE probe into the OpenGL app. Never for a release.
	-M
		Configure the Metal tree in build-metal instead: the Metal build of the engine (OpenGL ES 3
		through ANGLE), Debug without coverage, the engine and the two test suites, no tools. It is
		never packaged. It cannot be combined with -T.
	-P
		Configure the instrumented app tree in build-coverage-app instead: the packaged app
		(RelWithDebInfo) built for clang source coverage, for the manual coverage-app run of
		push.yml only (docs/openfallout/ci-reporting.md). It cannot be combined with -T or -M.
	-a <dir>
		ANGLE's folder for -M (lib/libEGL.dylib, lib/libGLESv2.dylib, include/), default /tmp/openfallout-angle.
	-o <dir>
		The OpenSceneGraph for OpenGL ES 3 for -M (CI/macos/osg/build_osg_gles3.sh), default /tmp/openfallout-osg-gles3.
EOF
	    exit 1
	    ;;
    esac
done

if [[ -n $TEST_TREE && -n $METAL_TREE ]]; then
    echo "-M and -T cannot be combined: the coverage tree is the desktop OpenGL build" >&2
    exit 1
fi

if [[ -n $COVERAGE_APP_TREE && ( -n $TEST_TREE || -n $METAL_TREE ) ]]; then
    echo "-P cannot be combined with -T or -M: it is the packaged OpenGL app, instrumented" >&2
    exit 1
fi

if [[ -z $KEEP ]]; then
    if [[ -n $VERBOSE && -d "$BUILD_DIR" ]]; then
        echo "Deleting existing build directory"
    fi
    rm -fr "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Supported: Apple Silicon, macOS 26.0 or later (CI/check_mac_package.py checks both on the packaged app).
VCPKG_TARGET_TRIPLET="arm64-osx-dynamic"

declare -a CMAKE_CONF_OPTS=(
-D CMAKE_C_COMPILER="clang"
-D CMAKE_CXX_COMPILER="clang++"
-DOPENFALLOUT_USE_SYSTEM_YAML_CPP=OFF
-D CMAKE_OSX_DEPLOYMENT_TARGET="26.0"
# CPack's `preinstall` target would otherwise build `all` again (a 15 s walk of the finished tree in the packaging step,
# measured on 21 November 2026). Whoever packages builds `all` first (CI/macos/build.sh does it for its default).
-D CMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON
)

declare -a BUILD_OPTS=(
-D BUILD_OPENFALLOUT=TRUE
-D BUILD_ESMTOOL=TRUE
-D BUILD_BSATOOL=TRUE
-D BUILD_NIFTEST=TRUE
-G"Ninja"
)

# The unit tests and their coverage (CI/test_reports.py) need an unoptimized, instrumented build, and the packaged app
# must not be instrumented, so they get their own tree. Only the test suites are built in it. The launcher stays on:
# it turns on Qt, which the launcher-settings tests in components-tests need. Debug without -g: coverage needs no debug
# information, and without it the tree, which the job caches, is about a quarter of the size.
if [[ -n $TEST_TREE ]]; then
    CMAKE_BUILD_TYPE=Debug
    BUILD_OPTS+=(
        -D CMAKE_C_FLAGS_DEBUG=-O0
        -D CMAKE_CXX_FLAGS_DEBUG=-O0
        -D BUILD_WITH_CODE_COVERAGE=ON
        -D BUILD_COMPONENTS_TESTS=ON
        -D BUILD_OPENFALLOUT_TESTS=ON
    )
fi

# The instrumented app (-P) is the packaged app with clang source coverage (-fprofile-instr-generate -fcoverage-mapping,
# cmake/OpenFalloutMacros.cmake), so that a manual run can measure which engine code a played round reaches. It keeps
# the app's build type and tools; only the manual coverage-app run of push.yml builds it, never a pull request or push.
if [[ -n $COVERAGE_APP_TREE ]]; then
    BUILD_OPTS+=(
        -D BUILD_WITH_CODE_COVERAGE=ON
    )
fi

# The Metal tree builds what the done test needs and nothing else: the engine and the two test suites (Debug, -O0, no
# coverage: the coverage report stays the desktop OpenGL build's). The tools do not touch the graphics library.
if [[ -n $METAL_TREE ]]; then
    CMAKE_BUILD_TYPE=Debug
    BUILD_OPTS+=(
        -D CMAKE_C_FLAGS_DEBUG=-O0
        -D CMAKE_CXX_FLAGS_DEBUG=-O0
        -D BUILD_COMPONENTS_TESTS=ON
        -D BUILD_OPENFALLOUT_TESTS=ON
        -D BUILD_ESMTOOL=FALSE
        -D BUILD_BSATOOL=FALSE
        -D BUILD_NIFTEST=FALSE
    )
fi

DEPENDENCIES_INSTALLED_PATH="$DEPENDENCIES_ROOT_PATH/installed/$VCPKG_TARGET_TRIPLET"
QT_PATH=/tmp/Qt/$QT_VER/macos

if [[ -n $METAL_TREE ]]; then
    # The OpenGL ES 3 OpenSceneGraph comes before the dependency set, which holds the desktop OpenGL one: find_package
    # takes the first match. The engine's CMake refuses a build whose OpenSceneGraph has the wrong profile.
    CMAKE_CONF_OPTS+=(
        -D CMAKE_PREFIX_PATH="$OSG_GLES3_PREFIX;$DEPENDENCIES_INSTALLED_PATH;$QT_PATH"
        -DOPENFALLOUT_METAL=ON
        -DOPENFALLOUT_ANGLE_ROOT="$ANGLE_ROOT_PATH"
        -DOSG_DIR="$OSG_GLES3_PREFIX"
        -DOPENTHREADS_DIR="$OSG_GLES3_PREFIX"
    )
    # The order above does not decide it: the dependency set's toolchain file puts its own folders in front of every
    # search. So each header folder and library of OpenSceneGraph is named, from the engine's own list.
    OSG_FIND_OPTIONS="$(python3 "$HERE/macos/osg/osg_gles3.py" find-options --root "$HERE/.." --prefix "$OSG_GLES3_PREFIX")" || exit 1
    while IFS= read -r option; do
        CMAKE_CONF_OPTS+=("$option")
    done <<< "$OSG_FIND_OPTIONS"
else
    CMAKE_CONF_OPTS+=(
        -D CMAKE_PREFIX_PATH="$DEPENDENCIES_INSTALLED_PATH;$QT_PATH"
    )
fi

CMAKE_CONF_OPTS+=(
    -DVCPKG_HOST_TRIPLET="$VCPKG_TARGET_TRIPLET"
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET"
    -DCMAKE_TOOLCHAIN_FILE="$DEPENDENCIES_ROOT_PATH/scripts/buildsystems/vcpkg.cmake"
)

if [[ "${CMAKE_BUILD_TYPE}" ]]; then
    CMAKE_CONF_OPTS+=(
        -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )
else
    CMAKE_CONF_OPTS+=(
        -D CMAKE_BUILD_TYPE=RelWithDebInfo
    )
fi

# Always explicit: a cached build tree must not carry the test switch into the next build.
if [[ -n $BUNDLE_ANGLE_ROOT ]]; then
    CMAKE_CONF_OPTS+=(
        -D OPENFALLOUT_BUNDLE_ANGLE=ON
        -D OPENFALLOUT_ANGLE_ROOT="$BUNDLE_ANGLE_ROOT"
    )
else
    CMAKE_CONF_OPTS+=(
        -D OPENFALLOUT_BUNDLE_ANGLE=OFF
    )
fi

if [[ -n $USE_CCACHE ]]; then
    CMAKE_CONF_OPTS+=(
        -D CMAKE_C_COMPILER_LAUNCHER="ccache"
        -D CMAKE_CXX_COMPILER_LAUNCHER="ccache"
    )
fi

if [[ -n $USE_WERROR ]]; then
    CMAKE_CONF_OPTS+=(
        -D OPENFALLOUT_CXX_FLAGS="-Werror"
    )
fi

if [[ -n $VERBOSE ]]; then
    echo CMake arguments: \
        "${CMAKE_CONF_OPTS[@]}" \
        "${BUILD_OPTS[@]}" \
        ..
fi

cmake \
    "${CMAKE_CONF_OPTS[@]}" \
    "${BUILD_OPTS[@]}" \
    ..
