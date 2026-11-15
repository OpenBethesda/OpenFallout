#!/bin/bash
# PROTOTYPE (ANGLE spike), throwaway. Builds OpenSceneGraph for OpenGL ES 3 and then the engine against it and ANGLE.
# Set these first:
#   A     work folder (gets osg-src, osg-build, osg-install, build-engine)
#   SRC   this checkout
#   ANGLE folder holding libEGL.dylib and libGLESv2.dylib (arm64, install names @rpath/...)
#   GLES  folder holding the EGL, GLES2, GLES3 and KHR header folders
set -e
: "${A:?}" "${SRC:?}" "${ANGLE:?}" "${GLES:?}"

# 1. OpenSceneGraph, OpenMW's fork, branch 3.6 (the spike used commit e568aac), with osg-gles3.patch applied
[ -d "$A/osg-src" ] || { git clone --branch 3.6 https://github.com/OpenMW/osg.git "$A/osg-src"; git -C "$A/osg-src" apply "$SRC/prototype-angle-metal/osg-gles3.patch"; }
cmake -S "$A/osg-src" -B "$A/osg-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$A/osg-install" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DOPENGL_PROFILE=GLES3 "-DOPENGL_HEADER1=#include <GLES3/gl3.h>" "-DOPENGL_HEADER2=" \
  -DOPENGL_INCLUDE_DIR="$GLES" -DOPENGL_gl_LIBRARY="$ANGLE/libGLESv2.dylib" -DEGL_LIBRARY="$ANGLE/libEGL.dylib" \
  -DOSG_WINDOWING_SYSTEM=None -DBUILD_OSG_APPLICATIONS=OFF -DBUILD_OSG_PLUGIN_IMAGEIO=ON
# plugins the engine needs: osg, dds, tga, bmp, jpeg, png, freetype, imageio (osgDB maps png to imageio on a Mac)
ninja -C "$A/osg-build" install

# 2. The engine, with the spike switched on by OPENFALLOUT_ANGLE
OSG_DIR="$A/osg-install" cmake -S "$SRC" -B "$A/build-engine" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$A/osg-install;/opt/homebrew" \
  "-DCMAKE_CXX_FLAGS=-I$GLES -DOPENFALLOUT_ANGLE -DGL_GLEXT_PROTOTYPES" \
  -DOSG_DIR="$A/osg-install" -DOPENTHREADS_DIR="$A/osg-install" \
  -DOPENGL_gl_LIBRARY="$ANGLE/libGLESv2.dylib" -DOPENGL_INCLUDE_DIR="$GLES" -DOPENGL_glu_LIBRARY= \
  -DBUILD_OPENFALLOUT=ON -DBUILD_LAUNCHER=OFF -DBUILD_OPENCS=OFF -DBUILD_WIZARD=OFF \
  -DBUILD_COMPONENTS_TESTS=OFF -DBUILD_OPENFALLOUT_TESTS=OFF -DBUILD_UNIT_TESTS=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_DOCS=OFF
ninja -C "$A/build-engine"

# 3. Run: the same command line as any build, with these in the environment
#   SDL_VIDEO_EGL_DRIVER=$ANGLE/libEGL.dylib SDL_VIDEO_GL_DRIVER=$ANGLE/libGLESv2.dylib
#   ANGLE_DEFAULT_PLATFORM=metal OSG_LIBRARY_PATH=$A/osg-install/lib
#   OPENFALLOUT_ES_DUMP=<folder>   (optional: writes every converted shader there)
# Settings used: shadows off, post-processing off, reverse-z off, antialiasing 0.
