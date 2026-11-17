#ifndef OPENFALLOUT_COMPONENTS_SCENEUTIL_GLDIALECT_H
#define OPENFALLOUT_COMPONENTS_SCENEUTIL_GLDIALECT_H

namespace SceneUtil::GLDialect
{
    // The graphics library this build writes. The Metal build (CMake option OPENFALLOUT_METAL) writes OpenGL ES 3.0,
    // which ANGLE's Metal backend turns into Metal; the other build writes desktop OpenGL and is the reference. The
    // choice is made when the engine is built, never at run time.
    //
    // Code that differs between the two asks the names below instead of testing the build option itself. When the
    // Metal build has passed its done test the desktop side is deleted: every use of these names is then one branch to
    // keep and one to remove.
#ifdef OPENFALLOUT_METAL
    inline constexpr bool sOpenGLES = true;
#else
    inline constexpr bool sOpenGLES = false;
#endif

    // Shader text is written in GLSL 1.20; OpenGL ES 3.0 compiles only GLSL ES 3.00, so the shader manager converts it
    // and folds the separately linked library shaders into each shader (OpenGL ES links one shader object per stage).
    inline constexpr bool sConvertsShadersToGlslEs = sOpenGLES;

    // OpenGL ES has no user clip planes (glClipPlane, gl_ClipVertex). The Metal build gives the shaders the planes as
    // uniforms and they write clip distances instead (SceneUtil::setEyeSpaceClipPlane).
    inline constexpr bool sHasUserClipPlanes = !sOpenGLES;

    // OpenGL ES promises only RGBA read-back of the frame buffer, and has no BGR or BGRA upload formats.
    inline constexpr bool sReadsBackRGB = !sOpenGLES;
    inline constexpr bool sUploadsBGR = !sOpenGLES;

    // Post-processing techniques (.omwfx) carry their own shaders in desktop GLSL 1.20 or 3.30, which are not yet
    // turned into GLSL ES 3.00 as the engine's own shaders are; until they are, the Metal build keeps post-processing
    // off at run time.
    inline constexpr bool sHasPostProcessing = !sOpenGLES;

    // Suffix of the KHR_debug calls: OpenGL ES 3.0 has them only as extension functions. (The fixed-function switches,
    // texture LOD bias, per-draw-buffer switches and object labels OpenGL ES 3.0 lacks are dealt with once, when the
    // context starts: see components/sceneutil/glextensions.cpp.)
    inline constexpr const char* sDebugFunctionSuffix = sOpenGLES ? "KHR" : "";
}

#endif
