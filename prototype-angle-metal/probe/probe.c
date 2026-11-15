// PROTOTYPE - throwaway. Opens an ES 3 context on ANGLE's Metal backend (pbuffer, no window) and prints what it offers.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>
#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
#endif
static int has(const char* e) { GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n); for (GLint i = 0; i < n; ++i) if (!strcmp((const char*)glGetStringi(GL_EXTENSIONS, i), e)) return 1; return 0; }
int main(int argc, char** argv)
{
    EGLAttrib dattr[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, dattr);
    EGLint maj = 0, min = 0;
    if (!eglInitialize(d, &maj, &min)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
    printf("EGL %d.%d vendor=%s version=%s\n", maj, min, eglQueryString(d, EGL_VENDOR), eglQueryString(d, EGL_VERSION));
    EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(d, cattr, &cfg, 1, &n) || n < 1) { printf("no config 0x%x\n", eglGetError()); return 1; }
    EGLint pattr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pattr);
    for (int minor = 2; minor >= 0; --minor)
    {
        EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, minor, EGL_NONE };
        EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xattr);
        printf("ES 3.%d context: %s\n", minor, c != EGL_NO_CONTEXT ? "created" : "refused");
        if (c == EGL_NO_CONTEXT) continue;
        eglMakeCurrent(d, s, s, c);
        printf("GL_VERSION  %s\nGL_RENDERER %s\nGLSL        %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));
        GLint v = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v); printf("max texture size %d\n", v);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &v); printf("max draw buffers %d\n", v);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &v); printf("max colour attachments %d\n", v);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v); printf("max vertex attribs %d\n", v);
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &v); printf("max vertex uniform vectors %d\n", v);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &v); printf("max fragment uniform vectors %d\n", v);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &v); printf("max combined texture units %d\n", v);
        glGetIntegerv(GL_MAX_SAMPLES, &v); printf("max samples %d\n", v);
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &v); printf("max uniform buffer bindings %d\n", v);
        const char* want[] = { "GL_EXT_texture_compression_dxt1", "GL_ANGLE_texture_compression_dxt3", "GL_ANGLE_texture_compression_dxt5", "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_compression_s3tc_srgb", "GL_EXT_texture_compression_rgtc", "GL_EXT_texture_compression_bptc",
            "GL_EXT_color_buffer_float", "GL_EXT_color_buffer_half_float", "GL_OES_texture_float_linear", "GL_EXT_float_blend", "GL_APPLE_clip_distance", "GL_ANGLE_clip_cull_distance", "GL_EXT_clip_cull_distance", "GL_EXT_texture_filter_anisotropic",
            "GL_OES_depth_texture", "GL_ANGLE_depth_texture", "GL_EXT_depth_clamp", "GL_EXT_clip_control", "GL_EXT_blend_func_extended", "GL_OES_draw_buffers_indexed", "GL_EXT_draw_buffers_indexed", "GL_ANGLE_multi_draw", "GL_ANGLE_base_vertex_base_instance", "GL_EXT_draw_elements_base_vertex", "GL_OES_draw_elements_base_vertex",
            "GL_EXT_shader_framebuffer_fetch", "GL_ANGLE_provoking_vertex", "GL_ANGLE_polygon_mode", "GL_EXT_polygon_offset_clamp", "GL_OES_texture_border_clamp", "GL_EXT_texture_border_clamp", "GL_EXT_texture_mirror_clamp_to_edge", "GL_OES_texture_3D", "GL_EXT_disjoint_timer_query", "GL_KHR_debug",
            "GL_OES_mapbuffer", "GL_EXT_map_buffer_range", "GL_OES_vertex_array_object", "GL_EXT_shadow_samplers", "GL_ANGLE_framebuffer_multisample", "GL_ANGLE_framebuffer_blit", "GL_EXT_sRGB_write_control", "GL_ANGLE_logic_op", "GL_OES_texture_stencil8", "GL_EXT_texture_norm16", "GL_OES_sample_shading", "GL_ANGLE_stencil_texturing", "GL_OVR_multiview2" };
        for (unsigned i = 0; i < sizeof(want) / sizeof(*want); ++i) printf("%-42s %s\n", want[i], has(want[i]) ? "yes" : "NO");
        if (argc > 1) { GLint ne = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &ne); printf("all %d extensions:\n", ne); for (GLint i = 0; i < ne; ++i) printf("  %s\n", glGetStringi(GL_EXTENSIONS, i)); }
        // DXT5 upload and a float render target, tried for real
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char blk[16] = { 0 };
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x83F3 /*DXT5*/, 4, 4, 0, 16, blk); printf("DXT5 upload: glGetError 0x%x\n", glGetError());
        GLuint ft, fb; glGenTextures(1, &ft); glBindTexture(GL_TEXTURE_2D, ft); glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, 64, 64);
        glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ft, 0);
        printf("RGBA16F render target: status 0x%x (complete = 0x8cd5)\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
        break;
    }
    return 0;
}
