#!/bin/bash
# PROTOTYPE (throwaway). Builds bridge.mm against ANGLE's headers and the project-built ANGLE libraries.
# ANGLE_INC = ANGLE's include folder, ANGLE_LIB = folder with libEGL.dylib and libGLESv2.dylib
set -e
: "${ANGLE_INC:?}" "${ANGLE_LIB:?}"
clang++ -std=c++17 -fobjc-arc -O2 -Wall bridge.mm -o bridge \
  -I"$ANGLE_INC" $(sdl2-config --cflags) \
  -L"$ANGLE_LIB" -lEGL -lGLESv2 -Wl,-rpath,"$ANGLE_LIB" \
  $(sdl2-config --libs | sed 's/-lSDL2main//') \
  -framework Metal -framework MetalFX -framework QuartzCore -framework IOSurface -framework ImageIO \
  -framework CoreGraphics -framework Cocoa
