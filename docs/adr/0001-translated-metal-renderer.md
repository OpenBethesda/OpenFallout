---
status: accepted
date: 2026-11-14
---

# The Mac reaches Metal through a translation layer, not a native renderer

Apple's OpenGL is deprecated and can be removed, so the Mac build needs a Metal renderer. We decided on a translated Metal renderer: the engine stays on OpenSceneGraph, writes OpenGL ES 3.0, and ANGLE's Metal backend turns that into Metal. The purpose is safety first and things the player can see second; owning a renderer outright is not a goal, so the route with the smallest change that passes the test was chosen.

The test for a Metal renderer is that every frame reaches the screen through Metal and Apple's OpenGL is never loaded. The terms are in `CONTEXT.md`. The full decision is in the issue "Which route takes OpenFallout to Metal?" (#175); the spike that proved the route is on the branch `spike/angle-metal`.

## Considered options

- **Native Metal renderer behind a new interface.** Rejected for now. It means rewriting about 74,000 lines of drawing code, running two drawing parts side by side for a long period, taking no more drawing fixes from upstream OpenMW, and getting safety last. It is reopened only if ANGLE's Metal backend is abandoned, or if the translated route blocks something the player would gain (frame time counts) that a native bridge cannot give.
- **Vulkan through a layer over Metal.** Removed. It is a rewrite of the same size, the frame still passes a second layer, and its only advantage is portability, which Mac Native gives up. It comes back only if upstream OpenMW ships a Vulkan renderer.
- **Stay on Apple's OpenGL until Apple sets a date.** Rejected: the spike showed the move is small, and waiting leaves the one certain risk in place.

## Consequences

- OpenGL ES 3.0 is a permanent ceiling as far as is known. An effect that needs more is built again for OpenGL ES 3.0 or done through a native bridge. It is never dropped only because of the ceiling.
- Upscaling, HDR and frame pacing do not come with the route. Each needs a native bridge; HDR needs one that owns the window layer.
- The packaged app holds one renderer. There is no OpenGL fallback for players. The OpenGL build is kept only as the reference until the translated renderer passes its done test: machine picture comparison, the three automatic play-throughs, shadows, post-processing and water, frame time no worse in a visible window, and the owner's play session.
- The project builds ANGLE itself at one fixed version and signs it with the app.
- OpenSceneGraph stays with no leaving date, and the project carries its own fixes to it.
- While Windows and Linux are still built, the shaders serve two dialects.
