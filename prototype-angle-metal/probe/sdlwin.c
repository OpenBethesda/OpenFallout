// PROTOTYPE - throwaway. SDL2 window (hidden) with an ES 3 context from ANGLE; draws one triangle, reads a pixel back.
#include <SDL.h>
#include <GLES3/gl3.h>
#include <stdio.h>
static GLuint sh(GLenum t, const char* s) { GLuint h = glCreateShader(t); glShaderSource(h, 1, &s, 0); glCompileShader(h); GLint ok; glGetShaderiv(h, GL_COMPILE_STATUS, &ok); if (!ok) { char b[512]; glGetShaderInfoLog(h, 512, 0, b); printf("shader: %s\n", b); } return h; }
int main(void)
{
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_SetHint("SDL_MAC_BACKGROUND_APP", "1");
    if (SDL_Init(SDL_INIT_VIDEO)) { printf("init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* w = SDL_CreateWindow("angle-probe", 100, 100, 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!w) { printf("window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext c = SDL_GL_CreateContext(w);
    if (!c) { printf("context: %s\n", SDL_GetError()); return 1; }
    int dw, dh; SDL_GL_GetDrawableSize(w, &dw, &dh);
    printf("GL_VERSION %s\nGL_RENDERER %s\ndrawable %dx%d swap interval set: %d\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), dw, dh, SDL_GL_SetSwapInterval(0));
    GLuint p = glCreateProgram();
    glAttachShader(p, sh(GL_VERTEX_SHADER, "#version 300 es\nin vec2 a; void main(){ gl_Position = vec4(a,0.0,1.0); }"));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, "#version 300 es\nprecision mediump float; out vec4 o; void main(){ o = vec4(1.0,0.5,0.0,1.0); }"));
    glBindAttribLocation(p, 0, "a"); glLinkProgram(p); glUseProgram(p);
    float v[] = { -1, -1, 3, -1, -1, 3 }; GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b); glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
    Uint64 t0 = SDL_GetPerformanceCounter(); int frames = 0; unsigned char px[4] = { 0 };
    for (; frames < 300; ++frames) { glViewport(0, 0, dw, dh); glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glDrawArrays(GL_TRIANGLES, 0, 3);
        if (frames == 299) glReadPixels(dw / 2, dh / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); SDL_GL_SwapWindow(w); }
    double s = (double)(SDL_GetPerformanceCounter() - t0) / SDL_GetPerformanceFrequency();
    printf("pixel %d %d %d %d (want 255 128 0 255), %d frames in %.2f s, glGetError 0x%x\n", px[0], px[1], px[2], px[3], frames, s, glGetError());
    SDL_GL_DeleteContext(c); SDL_DestroyWindow(w); SDL_Quit(); return 0;
}
