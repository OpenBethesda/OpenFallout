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

    // Fixed-function switches (GL_NORMALIZE, user clip planes, alpha test, light model, texture environment...) exist
    // only in desktop OpenGL. The shaders do that work in both builds.
    inline constexpr bool sHasFixedFunction = !sOpenGLES;

    // glPolygonMode (wireframe) exists only in desktop OpenGL.
    inline constexpr bool sHasPolygonMode = !sOpenGLES;

    // The texture parameter GL_TEXTURE_LOD_BIAS exists only in desktop OpenGL.
    inline constexpr bool sHasTextureLodBias = !sOpenGLES;

    // OpenGL ES promises only RGBA read-back of the frame buffer, and has no BGR or BGRA upload formats.
    inline constexpr bool sReadsBackRGB = !sOpenGLES;
    inline constexpr bool sUploadsBGR = !sOpenGLES;

    // Object labels (glObjectLabel) are OpenGL ES 3.2; ANGLE's Metal backend stops at 3.0.
    inline constexpr bool sHasObjectLabels = !sOpenGLES;

    // Suffix of the per-draw-buffer switches (glEnablei, glColorMaski...) and of the KHR_debug calls: OpenGL ES 3.0 has
    // them only as extension functions.
    inline constexpr const char* sIndexedFunctionSuffix = sOpenGLES ? "OES" : "";
    inline constexpr const char* sDebugFunctionSuffix = sOpenGLES ? "KHR" : "";
}

#endif
