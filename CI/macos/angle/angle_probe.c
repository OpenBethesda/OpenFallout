/*
 * The ANGLE probe (docs/openfallout/angle.md). Packaged beside the engine only in the test build with
 * OPENFALLOUT_BUNDLE_ANGLE, it proves inside a signed app that the bundled ANGLE libraries load and that ANGLE's
 * Metal backend gives an OpenGL ES 3.0 context: it opens the Metal display with a 64x64 pbuffer surface (no window),
 * prints the EGL and GL strings, compiles a GLSL ES 3.00 program (ANGLE turns it into a Metal library at run time),
 * draws one triangle over the surface and reads one pixel back.
 *
 * The last line is "ANGLE probe: PASS" or "ANGLE probe: FAIL: <why>". Exit status: 0 pass, 2 the machine has no Metal
 * device (a hosted CI runner; the probe asks Metal itself, so nothing ANGLE does can look like this), 3 any failure
 * on a machine that has one, ANGLE's display not starting among them.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PASS = 0,
    NO_DEVICE = 2,
    FAILED = 3
};

/* Metal's C entry point (Metal.framework). The probe is plain C: the object it returns is only tested for NULL. */
extern void* MTLCreateSystemDefaultDevice(void);

static int fail(int status, const char* why)
{
    printf("ANGLE probe: FAIL: %s (EGL error 0x%04x)\n", why, (unsigned)eglGetError());
    return status;
}

static GLuint compile(GLenum stage, const char* text)
{
    GLuint shader = glCreateShader(stage);
    GLint compiled = GL_FALSE;
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[1024] = "";
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("shader log: %s\n", log);
        return 0;
    }
    return shader;
}

static const char* VERTEX = "#version 300 es\n"
                            "in vec2 position;\n"
                            "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
static const char* FRAGMENT = "#version 300 es\n"
                              "precision mediump float;\n"
                              "uniform vec4 colour;\n"
                              "out vec4 result;\n"
                              "void main() { result = colour; }\n";

int main(void)
{
    if (MTLCreateSystemDefaultDevice() == NULL)
    {
        printf("ANGLE probe: FAIL: this machine has no Metal device\n");
        return NO_DEVICE;
    }
    const EGLAttrib displayAttributes[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
    EGLDisplay display
        = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, displayAttributes);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
        return fail(FAILED, "ANGLE's Metal display cannot be initialised on a machine that has a Metal device");
    printf("EGL_VERSION: %s\n", eglQueryString(display, EGL_VERSION));
    printf("EGL_VENDOR: %s\n", eglQueryString(display, EGL_VENDOR));

    const EGLint configAttributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
    EGLConfig config;
    EGLint configs = 0;
    if (!eglBindAPI(EGL_OPENGL_ES_API) || !eglChooseConfig(display, configAttributes, &config, 1, &configs)
        || configs < 1)
        return fail(FAILED, "no RGBA8 pbuffer configuration for OpenGL ES 3");
    const EGLint surfaceAttributes[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    const EGLint contextAttributes[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context))
        return fail(FAILED, "no OpenGL ES 3.0 context on a pbuffer surface");

    const char* version = (const char*)glGetString(GL_VERSION);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    printf("GL_VERSION: %s\n", version ? version : "(none)");
    printf("GL_RENDERER: %s\n", renderer ? renderer : "(none)");
    printf("GL_VENDOR: %s\n", (const char*)glGetString(GL_VENDOR));
    printf("GL_SHADING_LANGUAGE_VERSION: %s\n", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (!version || strncmp(version, "OpenGL ES 3.", 12) != 0)
        return fail(FAILED, "the context is not OpenGL ES 3");
    if (!renderer || !strstr(renderer, "Metal"))
        return fail(FAILED, "the renderer is not ANGLE's Metal backend");

    GLuint vertex = compile(GL_VERTEX_SHADER, VERTEX);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, FRAGMENT);
    if (!vertex || !fragment)
        return fail(FAILED, "a GLSL ES 3.00 shader does not compile");
    GLuint program = glCreateProgram();
    GLint linked = GL_FALSE;
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return fail(FAILED, "the GLSL ES 3.00 program does not link");

    /* one triangle that covers the whole surface, in a vertex buffer and a vertex array object (OpenGL ES 3.0) */
    const GLfloat corners[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    GLuint buffer = 0, vertices = 0;
    glGenVertexArrays(1, &vertices);
    glBindVertexArray(vertices);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);

    glViewport(0, 0, 64, 64);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glUniform4f(glGetUniformLocation(program, "colour"), 0.25f, 0.5f, 0.75f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("pixel: %u %u %u %u (expected about 64 128 191 255)\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        printf("GL error 0x%04x\n", (unsigned)error);
        return fail(FAILED, "OpenGL ES reported an error");
    }
    if (abs(pixel[0] - 64) > 2 || abs(pixel[1] - 128) > 2 || abs(pixel[2] - 191) > 2 || pixel[3] != 255)
        return fail(FAILED, "the drawn pixel is not the colour drawn");

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    printf("ANGLE probe: PASS\n");
    return PASS;
}
