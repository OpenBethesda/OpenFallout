// PROTOTYPE - throwaway. Compiles dumped GLSL ES sources with ANGLE (Metal) and prints the errors. Pairs NNNN_x.vert / .frag are not linked.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv)
{
    EGLAttrib dattr[] = { 0x3203, 0x3489, EGL_NONE };
    EGLDisplay d = eglGetPlatformDisplay(0x3202, (void*)EGL_DEFAULT_DISPLAY, dattr);
    eglInitialize(d, 0, 0);
    EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n = 0; eglChooseConfig(d, cattr, &cfg, 1, &n);
    EGLint pattr[] = { EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pattr);
    EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xattr);
    eglMakeCurrent(d, s, s, c);
    int bad = 0;
    for (int i = 1; i < argc; ++i)
    {
        FILE* f = fopen(argv[i], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        char* src = malloc(len + 1); fread(src, 1, len, f); src[len] = 0; fclose(f);
        GLenum type = strstr(argv[i], ".vert") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
        GLuint h = glCreateShader(type); const char* p = src; glShaderSource(h, 1, &p, 0); glCompileShader(h);
        GLint ok = 0; glGetShaderiv(h, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[8192]; glGetShaderInfoLog(h, sizeof log, 0, log); printf("== %s\n%s\n", argv[i], log); ++bad; }
        glDeleteShader(h); free(src);
    }
    printf("%d of %d failed\n", bad, argc - 1);
    return bad != 0;
}
