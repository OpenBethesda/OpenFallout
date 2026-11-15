#!/bin/bash -e

VERBOSE=""
USE_CCACHE=""
KEEP=""
USE_WERROR=""
TEST_TREE=""
BUILD_DIR="build"
DEPENDENCIES_ROOT_PATH="/tmp/openmw-deps"

source ./CI/macos/deps_versions.sh

while getopts VCkETd: ARG
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

        d )
            DEPENDENCIES_ROOT_PATH="$OPTARG"
            ;;

	* )
	    cat <<EOF
Usage: $0 [-VCkETd]
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
		and OpenFallout test suites, no editor or wizard. It is never packaged.
EOF
	    exit 1
	    ;;
    esac
done

if [[ -z $KEEP ]]; then
    if [[ -n $VERBOSE && -d "$BUILD_DIR" ]]; then
        echo "Deleting existing build directory"
    fi
    rm -fr "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

declare -a CMAKE_CONF_OPTS=(
-D CMAKE_C_COMPILER="clang"
-D CMAKE_CXX_COMPILER="clang++"
-DOPENFALLOUT_USE_SYSTEM_YAML_CPP=OFF
-D CMAKE_OSX_DEPLOYMENT_TARGET="14.8"
)

declare -a BUILD_OPTS=(
-D BUILD_OPENFALLOUT=TRUE
-D BUILD_OPENCS=TRUE
-D BUILD_ESMTOOL=TRUE
-D BUILD_BSATOOL=TRUE
-D BUILD_ESSIMPORTER=TRUE
-D BUILD_NIFTEST=TRUE
-D BUILD_NAVMESHTOOL=TRUE
-D BUILD_BULLETOBJECTTOOL=TRUE
-G"Unix Makefiles"
)

# The unit tests and their coverage (CI/test_reports.py) need an unoptimized, instrumented build, and the packaged app
# must not be instrumented, so they get their own tree. Only the test suites are built in it. The launcher stays on:
# it turns on Qt, which the launcher-settings tests in components-tests need. The editor's tests stay off: the editor
# is not built on the Mac. Debug without -g: coverage needs no debug information, and without it the tree, which the
# job caches, is about a quarter of the size.
if [[ -n $TEST_TREE ]]; then
    CMAKE_BUILD_TYPE=Debug
    BUILD_OPTS+=(
        -D CMAKE_C_FLAGS_DEBUG=-O0
        -D CMAKE_CXX_FLAGS_DEBUG=-O0
        -D BUILD_WITH_CODE_COVERAGE=ON
        -D BUILD_COMPONENTS_TESTS=ON
        -D BUILD_OPENFALLOUT_TESTS=ON
        -D BUILD_OPENCS=OFF
        -D BUILD_WIZARD=OFF
    )
fi

if [[ "${MACOS_AMD64}" ]]; then
    VCPKG_TARGET_TRIPLET="x64-osx-dynamic"
    CMAKE_CONF_OPTS+=(
        -D CMAKE_OSX_ARCHITECTURES="x86_64"
    )
else
    VCPKG_TARGET_TRIPLET="arm64-osx-dynamic"
fi

DEPENDENCIES_INSTALLED_PATH="$DEPENDENCIES_ROOT_PATH/installed/$VCPKG_TARGET_TRIPLET"
QT_PATH=/tmp/Qt/$QT_VER/macos

CMAKE_CONF_OPTS+=(
    -D CMAKE_PREFIX_PATH="$DEPENDENCIES_INSTALLED_PATH;$QT_PATH"
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
