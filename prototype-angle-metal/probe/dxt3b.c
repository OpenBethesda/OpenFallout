// PROTOTYPE - throwaway. An 8x32 DXT3 texture with a different alpha in every texel; compares ANGLE's read-back with a decode done here.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
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
    enum { W = 8, H = 32 };
    GLuint ft, fb; glGenTextures(1, &ft); glBindTexture(GL_TEXTURE_2D, ft); glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ft, 0); glViewport(0, 0, W, H);
    unsigned char data[(W / 4) * (H / 4) * 16]; unsigned char want[W * H]; srand(7);
    for (int by = 0; by < H / 4; ++by) for (int bx = 0; bx < W / 4; ++bx)
    {
        unsigned char* blk = data + (by * (W / 4) + bx) * 16;
        for (int i = 0; i < 8; ++i) blk[i] = (unsigned char)(rand() & 0xff);
        blk[8] = 0x00; blk[9] = 0xF8; blk[10] = 0x00; blk[11] = 0xF8; blk[12] = blk[13] = blk[14] = blk[15] = 0;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { int nib = (blk[y * 2 + x / 2] >> ((x & 1) * 4)) & 0xf; want[(by * 4 + y) * W + bx * 4 + x] = (unsigned char)(nib * 17); }
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (pass == 0) glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x83F2, W, H, 0, sizeof data, data);
        else { glTexStorage2D(GL_TEXTURE_2D, 1, 0x83F2, W, H); glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, 0x83F2, sizeof data, data); }
        GLenum e = glGetError();
        glClearColor(0, 0, 1, 0); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
        unsigned char px[W * H * 4]; glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        int badA = 0, badC = 0; for (int i = 0; i < W * H; ++i) { if (px[i * 4 + 3] != want[i]) ++badA; if (px[i * 4] != 255 || px[i * 4 + 1] || px[i * 4 + 2]) ++badC; }
        printf("%s: upload error 0x%x, %d of %d alpha values differ, %d colours differ; first row got %d %d %d %d want %d %d %d %d\n", pass ? "TexStorage+SubImage" : "CompressedTexImage2D", e, badA, W * H, badC, px[3], px[7], px[11], px[15], want[0], want[1], want[2], want[3]);
    }
    return 0;
}
