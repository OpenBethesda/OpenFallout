# PROTOTYPE: one real cell through ANGLE on Metal

Throwaway code. It answers one question for the map "the route to a Mac Native OpenFallout" and is never merged.

**Question:** does one real cell draw when OpenSceneGraph is built for OpenGL ES 3 and the context comes from ANGLE's Metal backend?

**Answer:** yes. One interior cell draws with walls, lighting, the player character and the interface, matching the OpenGL build except for one texture format. One exterior cell also draws, with wrong lighting.

Measured on an Apple M3 Pro, macOS 27, ANGLE 2.1 (Metal renderer), OpenGL ES 3.0, New Vegas data, 1280x720.

## What is in this branch

- Engine changes, every one behind `#ifdef OPENFALLOUT_ANGLE` and marked `PROTOTYPE (ANGLE spike)`: 11 C++ files.
- `files/shaders/es/`: 18 text rules and two header blocks that turn the GLSL 1.20 shaders into GLSL ES 3.00 when they load.
- One hand edit to an existing shader: `files/shaders/lib/light/util.glsl`, one line.
- This folder: `build.sh` (how it was built), `osg-gles3.patch` (three edits to OpenSceneGraph), `probe/` (small test programs), `scenario.lua` (the hidden-run scenario for the round tool).

The ANGLE libraries themselves are not here. The spike borrowed a prebuilt pair from an application already installed on the machine; a real route needs its own ANGLE build.

## Results

| | ANGLE on Metal | OpenGL |
|---|---|---|
| Interior, frame time | about 16 ms | about 12 ms |
| Exterior, frame time | 19.8 ms | 38.9 ms |

The numbers are rough: hidden window, a busy machine, one to three runs. A bare triangle in a hidden ANGLE window topped out near 116 frames per second with swap interval 0, so the interior ANGLE figure may be held to the display's refresh. Not checked in a visible window.

Shaders: 73 variants from 26 shader files compile. The one failure is the multiview resolve shader, which is not used.

### Visibly wrong

- Textures in the DXT3 format draw wrongly (black with bright hard-edged patches, white fringes). DXT1 and DXT5 are right. ANGLE decodes DXT3 correctly on its own (`probe/dxt3.c`, `probe/dxt3b.c`), so the fault is in the engine's or OpenSceneGraph's OpenGL ES path. Cause not found.
- Outdoors, sunlit terrain is blown out to white, the clouds are too strong and one building texture is corrupt. Cause not found.
- Four uniforms lose their default values, because GLSL ES has no uniform initialisers.

### Not tried

Shadows, post-processing, reverse-Z, water, and the ten shader files written for GLSL 3.30 or later. All were switched off.

## What OpenGL ES 3.0 on ANGLE's Metal backend lacks, as met here

- Only ES 3.0 is offered; 3.1 and 3.2 are refused.
- One shader object per stage, so library shaders that the engine links as separate objects had to be folded into the main shader as text.
- No `GL_RGB` read-back, no BGR or BGRA upload.
- Per-buffer colour mask and blend switches exist only as the `OES` extension functions.
- No `GL_NORMALIZE`, no `GL_CLIP_PLANE0`, no polygon mode, no texture LOD bias parameter, no client-state vertex arrays.
- Debug calls carry the `KHR` suffix; object labels need ES 3.2.
- The S3TC formats are there under other extension names than the engine checks for.

Present and useful: float and half-float render targets, clip distance, clip control, depth clamp, dual-source blending, anisotropic filtering, RGTC and BPTC compression, base-vertex drawing.

## Errors ANGLE still reports per run

A few hundred: the LOD bias texture parameter (372), object labels (32), six texture uploads with a format ES refuses, two mipmap generations on an unsupported format, one copy-texture overflow.
