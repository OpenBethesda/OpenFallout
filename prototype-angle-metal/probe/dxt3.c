// PROTOTYPE - throwaway. Uploads one DXT1, DXT3 and DXT5 block (red, alpha 136 where the format has alpha), draws each to an RGBA8 target, reads it back.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
static GLuint sh(GLenum t, const char* s) { GLuint h = glCreateShader(t); glShaderSource(h, 1, &s, 0); glCompileShader(h); return h; }
int main(void)
{
    EGLAttrib dattr[] = { 0x3203, 0x3489, EGL_NONE };
    EGLDisplay d = eglGetPlatformDisplay(0x3202, (void*)EGL_DEFAULT_DISPLAY, dattr); eglInitialize(d, 0, 0);
    EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n = 0; eglChooseConfig(d, cattr, &cfg, 1, &n);
    EGLint pattr[] = { EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, cfg, pattr);
    EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE }; EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xattr); eglMakeCurrent(d, s, s, c);
    GLuint p = glCreateProgram();
    glAttachShader(p, sh(GL_VERTEX_SHADER, "#version 300 es\nin vec2 a; out vec2 uv; void main(){ uv = a*0.5+0.5; gl_Position = vec4(a,0.0,1.0); }"));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, "#version 300 es\nprecision highp float; uniform sampler2D t; in vec2 uv; out vec4 o; void main(){ o = texture(t, uv); }"));
    glBindAttribLocation(p, 0, "a"); glLinkProgram(p); glUseProgram(p);
    float v[] = { -1, -1, 3, -1, -1, 3 }; GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    GLuint ft, fb; glGenTextures(1, &ft); glBindTexture(GL_TEXTURE_2D, ft); glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ft, 0); glViewport(0, 0, 4, 4);
    unsigned char color[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };               // colour0 = colour1 = red, all texels index 0
    unsigned char dxt1[8]; unsigned char dxt3[16]; unsigned char dxt5[16];
    for (int i = 0; i < 8; ++i) { dxt1[i] = color[i]; dxt3[i] = 0x88; dxt3[8 + i] = color[i]; dxt5[8 + i] = color[i]; }
    dxt5[0] = 136; dxt5[1] = 136; for (int i = 2; i < 8; ++i) dxt5[i] = 0;         // alpha0 = alpha1 = 136, all texels index 0
    struct { const char* name; GLenum f; const unsigned char* data; int size; } t[] = { { "DXT1 rgb ", 0x83F0, dxt1, 8 }, { "DXT1 rgba", 0x83F1, dxt1, 8 }, { "DXT3     ", 0x83F2, dxt3, 16 }, { "DXT5     ", 0x83F3, dxt5, 16 } };
    for (int i = 0; i < 4; ++i)
    {
        GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, t[i].f, 4, 4, 0, t[i].size, t[i].data); GLenum e1 = glGetError();
        glClearColor(0, 0, 1, 0); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
        unsigned char px[4]; glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        printf("%s upload error 0x%x, read back %3d %3d %3d %3d\n", t[i].name, e1, px[0], px[1], px[2], px[3]);
    }
    return 0;
}
