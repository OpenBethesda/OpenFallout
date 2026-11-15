# PROTOTYPE: a shared surface between ANGLE and a native bridge

Throwaway code. It answers one question for the map "the route to a Mac Native OpenFallout" and is never merged.

**Question:** does one frame drawn through ANGLE into a surface the application owns come out of Apple's scaler and onto the screen through a native bridge?

**Answer:** yes.

`bridge.mm` is one test program with a plain test scene; the game does not run in it.

1. OpenGL ES 3 through ANGLE draws the scene into a small IOSurface (1728 x 1117).
2. The MetalFX spatial scaler makes it full size (3456 x 2234) into a second IOSurface.
3. OpenGL ES 3 through ANGLE draws a stand-in interface on that full-size surface.
4. A native bridge copies it to the Metal layer of a window SDL made, and presents it.

Measured on an Apple M3 Pro, macOS 27, ANGLE at commit 1f6526a566c2 built with the Metal backend only.

## Build and run

```
ANGLE_INC=<ANGLE include folder> ANGLE_LIB=<folder with libEGL.dylib and libGLESv2.dylib> ./build.sh
./bridge --show                      # visible window
./bridge --frames 300 --no-present --mode scaled --heavy 200
./bridge --frames 300 --no-present --mode direct --heavy 200
```

## What it showed

- ANGLE hands over its Metal device (`EGL_ANGLE_device_metal`), so the scaler and the bridge use the same one.
- ANGLE draws into an IOSurface the application owns (`EGL_ANGLE_iosurface_client_buffer`, as the current drawing surface).
- ANGLE also draws into a Metal texture the application made (`EGL_ANGLE_metal_texture_client_buffer`: an EGL image, bound to a texture, attached to a framebuffer).
- The scaler takes the first surface and fills the second. A flat field keeps its exact colour.
- An interface drawn through ANGLE after the scaler is exact to the pixel.
- Ordering between ANGLE's work and the bridge's work held with `eglWaitUntilWorkScheduledANGLE` on one side and `waitUntilScheduled` on the other; no full wait for the graphics card was needed inside a frame.

## Cost

Without presenting, per frame:

| Scene | Drawn small, then scaled | Drawn at full size |
|---|---|---|
| Trivial | 1.3 ms | 0.4 ms |
| Heavy (`--heavy 200`) | 4.2 ms | 12.0 ms |
| Very heavy (`--heavy 600`) | 10.0 ms | 36.2 ms |

The scaled path has a fixed cost of about 0.9 ms a frame.

With presenting, in a visible window with display sync switched off: 8.9 ms a frame (112 a second). The same figure appears in a hidden window. The wait is in getting the next drawable, so a window's layer takes frames at about the display's 120 Hz and no faster.

## To know before building on it

- The picture arrives upside down for the bridge: OpenGL and Metal count rows from opposite ends. The bridge has to flip it, or the scene has to be drawn flipped.
- The scene is made up. The numbers show the mechanism, not what the game will reach.
- Only the spatial scaler was tried. The temporal one needs depth and motion vectors.
- Nothing here measures how evenly frames arrive.
