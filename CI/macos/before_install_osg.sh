#!/bin/bash -e

# Builds the OpenSceneGraph for OpenGL ES 3 that the Metal build uses (docs/openfallout/osg-gles3.md). The workflow runs
# it only when the cache of $OSG_GLES3_PREFIX missed. It needs the dependency set (CI/before_install.macos.sh) for the
# picture libraries and ANGLE (CI/macos/angle/fetch_angle.sh) for the OpenGL ES headers and libraries.

DEPENDENCIES_ROOT_PATH="${DEPENDENCIES_ROOT_PATH:-/tmp/openmw-deps}"
ANGLE_ROOT_PATH="${ANGLE_ROOT_PATH:-/tmp/openfallout-angle}"
OSG_GLES3_PREFIX="${OSG_GLES3_PREFIX:-/tmp/openfallout-osg-gles3}"
OSG_GLES3_WORK="${OSG_GLES3_WORK:-/tmp/openfallout-osg-gles3-work}"

rm -rf "$OSG_GLES3_PREFIX"
./CI/macos/osg/build_osg_gles3.sh \
    --angle-root "$ANGLE_ROOT_PATH" \
    --prefix-path "$DEPENDENCIES_ROOT_PATH/installed/arm64-osx-dynamic" \
    --work "$OSG_GLES3_WORK" \
    --install "$OSG_GLES3_PREFIX" \
    --jobs "$(sysctl -n hw.logicalcpu)"

# only the installed library is cached, not the source and the build tree
rm -rf "$OSG_GLES3_WORK"
