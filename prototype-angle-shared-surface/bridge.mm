// PROTOTYPE (throwaway, never merge): does a frame drawn through ANGLE into a surface the application owns
// come out of Apple's scaler and reach the screen through a native bridge?
//
//   scene (OpenGL ES 3 through ANGLE)  -> IOSurface A, small
//   MetalFX spatial scaler (bridge)    -> IOSurface B, full size
//   interface (OpenGL ES 3 again)      -> drawn on top of B at full size
//   blit + present (bridge)            -> CAMetalLayer of a window SDL made
//
// bridge [--frames N] [--show] [--no-present] [--mode scaled|direct] [--heavy K] [--low WxH] [--out WxH] [--png file]

#define EGL_EGLEXT_PROTOTYPES 1
#define GL_GLEXT_PROTOTYPES 1
#define SDL_MAIN_HANDLED 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL.h>
#include <SDL_metal.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

static double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void die(const char* what)
{
    std::fprintf(stderr, "FAIL: %s (EGL error 0x%x)\n", what, eglGetError());
    std::exit(1);
}

static IOSurfaceRef makeSurface(int w, int h)
{
    NSDictionary* props = @{
        (id)kIOSurfaceWidth : @(w),
        (id)kIOSurfaceHeight : @(h),
        (id)kIOSurfaceBytesPerElement : @4,
        (id)kIOSurfacePixelFormat : @((unsigned)'BGRA'),
    };
    return IOSurfaceCreate((__bridge CFDictionaryRef)props);
}

static EGLSurface pbufferFor(EGLDisplay dpy, EGLConfig cfg, IOSurfaceRef surf, int w, int h, EGLint target)
{
    const EGLint attribs[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_IOSURFACE_PLANE_ANGLE, 0, EGL_TEXTURE_TARGET, target,
        EGL_TEXTURE_INTERNAL_FORMAT_ANGLE, GL_BGRA_EXT, EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA, EGL_TEXTURE_TYPE_ANGLE,
        GL_UNSIGNED_BYTE, EGL_NONE };
    return eglCreatePbufferFromClientBuffer(dpy, EGL_IOSURFACE_ANGLE, (EGLClientBuffer)surf, cfg, attribs);
}

static id<MTLTexture> textureFor(id<MTLDevice> dev, IOSurfaceRef surf, int w, int h, MTLTextureUsage usage)
{
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    d.usage = usage;
    d.storageMode = MTLStorageModeShared;
    return [dev newTextureWithDescriptor:d iosurface:surf plane:0];
}

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader: %s\n", log);
        std::exit(1);
    }
    return s;
}

static const char* kVert = R"(#version 300 es
out vec2 uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// A flat field on the left (for an exact colour check), fine rings and lines on the right (detail for the scaler),
// and an optional loop that only burns fragment time.
static const char* kFrag = R"(#version 300 es
precision highp float;
in vec2 uv;
uniform float angle;
uniform int heavy;
out vec4 colour;
void main() {
    vec3 c = vec3(0.2, 0.4, 0.8);
    if (uv.x > 0.5) {
        vec2 q = (uv - vec2(0.75, 0.5)) * vec2(2.0, 1.0);
        float r = length(q);
        float rings = step(0.5, fract(r * 40.0));
        float a = atan(q.y, q.x) + angle;
        float spokes = step(0.5, fract(a * 6.0 / 3.14159));
        c = mix(vec3(0.9, 0.85, 0.2), vec3(0.1, 0.1, 0.1), rings * 0.7 + spokes * 0.3);
    }
    float burn = 0.0;
    for (int i = 0; i < heavy; ++i) burn += sin(float(i) * 0.37 + uv.x * 13.0) * cos(float(i) * 0.11 + uv.y * 7.0);
    c += burn * 1e-9;
    colour = vec4(c, 1.0);
})";

struct Pixel
{
    int b, g, r, a;
};

static Pixel pixelAt(IOSurfaceRef surf, int x, int y)
{
    const unsigned char* base = (const unsigned char*)IOSurfaceGetBaseAddress(surf);
    const size_t row = IOSurfaceGetBytesPerRow(surf);
    const unsigned char* p = base + row * y + 4 * x;
    return { p[0], p[1], p[2], p[3] };
}

static bool near(int a, int b, int tol)
{
    return std::abs(a - b) <= tol;
}

static void writePng(IOSurfaceRef surf, const char* path)
{
    const size_t w = IOSurfaceGetWidth(surf), h = IOSurfaceGetHeight(surf);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(IOSurfaceGetBaseAddress(surf), w, h, 8, IOSurfaceGetBytesPerRow(surf), cs,
        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
}

int main(int argc, char** argv)
{
    int frames = 300, lowW = 1728, lowH = 1117, outW = 3456, outH = 2234, heavy = 0;
    bool show = false, scaled = true, present = true;
    std::string png;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--frames")
            frames = std::atoi(next());
        else if (a == "--show")
            show = true;
        else if (a == "--no-present")
            present = false;
        else if (a == "--mode")
            scaled = std::string(next()) != "direct";
        else if (a == "--heavy")
            heavy = std::atoi(next());
        else if (a == "--low")
            std::sscanf(next(), "%dx%d", &lowW, &lowH);
        else if (a == "--out")
            std::sscanf(next(), "%dx%d", &outW, &outH);
        else if (a == "--png")
            png = next();
    }

    @autoreleasepool
    {
        // 1. A window SDL made, with a Metal layer the bridge owns.
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            std::fprintf(stderr, "FAIL: SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        Uint32 flags = SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | (show ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
        SDL_Window* window
            = SDL_CreateWindow("ANGLE shared surface (prototype)", 60, 60, outW / 2, outH / 2, flags);
        if (!window)
        {
            std::fprintf(stderr, "FAIL: SDL_CreateWindow: %s\n", SDL_GetError());
            return 1;
        }
        SDL_MetalView view = SDL_Metal_CreateView(window);
        CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(view);

        // 2. ANGLE on Metal, with no window surface of its own.
        const EGLAttrib dpyAttribs[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
        EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)EGL_DEFAULT_DISPLAY, dpyAttribs);
        EGLint major = 0, minor = 0;
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor))
            die("eglInitialize");
        const char* ext = eglQueryString(dpy, EGL_EXTENSIONS);
        auto has = [&](const char* name) { return std::strstr(ext, name) != nullptr; };
        std::printf("EGL %d.%d  iosurface_client_buffer=%d metal_texture_client_buffer=%d wait_until_work_scheduled=%d\n",
            major, minor, has("EGL_ANGLE_iosurface_client_buffer"), has("EGL_ANGLE_metal_texture_client_buffer"),
            has("EGL_ANGLE_wait_until_work_scheduled"));
        const bool waitExt = has("EGL_ANGLE_wait_until_work_scheduled");

        // 3. The Metal device ANGLE draws with.
        EGLAttrib eglDevice = 0, mtlDevice = 0;
        if (!eglQueryDisplayAttribEXT(dpy, EGL_DEVICE_EXT, &eglDevice)
            || !eglQueryDeviceAttribEXT((EGLDeviceEXT)eglDevice, EGL_METAL_DEVICE_ANGLE, &mtlDevice))
            die("query Metal device");
        id<MTLDevice> device = (__bridge id<MTLDevice>)(void*)mtlDevice;
        const char* devExt = eglQueryDeviceStringEXT((EGLDeviceEXT)eglDevice, EGL_EXTENSIONS);
        std::printf("Metal device from ANGLE: %s (device extension EGL_ANGLE_device_metal=%d)\n", device.name.UTF8String,
            devExt && std::strstr(devExt, "EGL_ANGLE_device_metal") != nullptr);
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.drawableSize = CGSizeMake(outW, outH);
        layer.displaySyncEnabled = NO; // measure the cost, not the display's refresh
        id<MTLCommandQueue> queue = [device newCommandQueue];

        // 4. Context.
        const EGLint cfgAttribs[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
        EGLConfig cfg;
        EGLint count = 0;
        if (!eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &count) || count == 0)
            die("eglChooseConfig");
        EGLint target = EGL_TEXTURE_2D;
        eglGetConfigAttrib(dpy, cfg, EGL_BIND_TO_TEXTURE_TARGET_ANGLE, &target);
        const EGLint ctxAttribs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (ctx == EGL_NO_CONTEXT)
            die("eglCreateContext");

        // 5. The two shared surfaces, each seen by ANGLE as a drawing surface and by the bridge as a Metal texture.
        const int sceneW = scaled ? lowW : outW, sceneH = scaled ? lowH : outH;
        IOSurfaceRef surfA = makeSurface(lowW, lowH), surfB = makeSurface(outW, outH);
        EGLSurface pbA = pbufferFor(dpy, cfg, surfA, lowW, lowH, target);
        EGLSurface pbB = pbufferFor(dpy, cfg, surfB, outW, outH, target);
        if (pbA == EGL_NO_SURFACE || pbB == EGL_NO_SURFACE)
            die("eglCreatePbufferFromClientBuffer(IOSurface)");
        std::printf("IOSurface drawing surfaces made: scene %dx%d, full %dx%d, texture target 0x%x\n", lowW, lowH, outW,
            outH, target);
        if (!eglMakeCurrent(dpy, pbA, pbA, ctx))
            die("eglMakeCurrent(IOSurface A)");
        std::printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

        id<MTLTexture> texA = textureFor(device, surfA, lowW, lowH, MTLTextureUsageShaderRead);

        // 6. Apple's simple scaler.
        id<MTLFXSpatialScaler> scaler = nil;
        MTLTextureUsage outUsage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        if (scaled)
        {
            if (![MTLFXSpatialScalerDescriptor supportsDevice:device])
            {
                std::fprintf(stderr, "FAIL: MetalFX spatial scaler not supported on this device\n");
                return 1;
            }
            MTLFXSpatialScalerDescriptor* sd = [MTLFXSpatialScalerDescriptor new];
            sd.inputWidth = lowW;
            sd.inputHeight = lowH;
            sd.outputWidth = outW;
            sd.outputHeight = outH;
            sd.colorTextureFormat = MTLPixelFormatBGRA8Unorm;
            sd.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
            sd.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
            scaler = [sd newSpatialScalerWithDevice:device];
            if (!scaler)
            {
                std::fprintf(stderr, "FAIL: could not make the MetalFX spatial scaler\n");
                return 1;
            }
            outUsage |= scaler.outputTextureUsage;
            std::printf("MetalFX spatial scaler made: %dx%d -> %dx%d\n", lowW, lowH, outW, outH);
        }
        id<MTLTexture> texB = textureFor(device, surfB, outW, outH, outUsage);
        if (!texA || !texB)
        {
            std::fprintf(stderr, "FAIL: Metal texture over IOSurface\n");
            return 1;
        }

        // 7. Scene program.
        GLuint prog = glCreateProgram();
        glAttachShader(prog, compile(GL_VERTEX_SHADER, kVert));
        glAttachShader(prog, compile(GL_FRAGMENT_SHADER, kFrag));
        glLinkProgram(prog);
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        const GLint uAngle = glGetUniformLocation(prog, "angle"), uHeavy = glGetUniformLocation(prog, "heavy");

        auto glWorkScheduled = [&]() {
            if (waitExt)
            {
                glFlush();
                eglWaitUntilWorkScheduledANGLE(dpy);
            }
            else
                glFinish();
        };

        double tScene = 0, tScale = 0, tUi = 0, tPresent = 0;
        int presented = 0;
        const double start = now();
        id<MTLCommandBuffer> last = nil;
        for (int f = 0; f < frames; ++f)
        {
            @autoreleasepool
            {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    if (e.type == SDL_QUIT)
                        frames = f;
                const bool checkFrame = f == frames - 1;
                const float angle = checkFrame ? 0.f : f * 0.01f;

                // scene, through ANGLE
                double t0 = now();
                EGLSurface sceneSurface = scaled ? pbA : pbB;
                if (!eglMakeCurrent(dpy, sceneSurface, sceneSurface, ctx))
                    die("eglMakeCurrent(scene)");
                glViewport(0, 0, sceneW, sceneH);
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glUseProgram(prog);
                glBindVertexArray(vao);
                glUniform1f(uAngle, angle);
                glUniform1i(uHeavy, heavy);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glWorkScheduled();
                double t1 = now();

                // scaler, in the bridge
                if (scaled)
                {
                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    scaler.colorTexture = texA;
                    scaler.outputTexture = texB;
                    [scaler encodeToCommandBuffer:cb];
                    [cb commit];
                    [cb waitUntilScheduled];
                }
                double t2 = now();

                // interface, through ANGLE again, on the full-size surface, after the scaler
                if (scaled && !eglMakeCurrent(dpy, pbB, pbB, ctx))
                    die("eglMakeCurrent(IOSurface B)");
                glViewport(0, 0, outW, outH);
                glEnable(GL_SCISSOR_TEST);
                glScissor(200, 200, 300, 80);
                glClearColor(1, 1, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(210, 210, 280, 60);
                glClearColor(0.8f, 0.1f, 0.1f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glDisable(GL_SCISSOR_TEST);
                glWorkScheduled();
                double t3 = now();

                // blit and present, in the bridge
                id<CAMetalDrawable> drawable = present ? [layer nextDrawable] : nil;
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                if (drawable)
                {
                    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                    [blit copyFromTexture:texB
                              sourceSlice:0
                              sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(outW, outH, 1)
                                toTexture:drawable.texture
                         destinationSlice:0
                         destinationLevel:0
                        destinationOrigin:MTLOriginMake(0, 0, 0)];
                    [blit endEncoding];
                    [cb presentDrawable:drawable];
                    ++presented;
                }
                [cb commit];
                last = cb;
                double t4 = now();
                tScene += t1 - t0;
                tScale += t2 - t1;
                tUi += t3 - t2;
                tPresent += t4 - t3;
            }
        }
        glFinish();
        [last waitUntilCompleted];
        const double total = now() - start;
        std::printf("mode=%s heavy=%d frames=%d presented=%d  total %.2f ms/frame (%.1f fps)\n",
            scaled ? "scaled" : "direct", heavy, frames, presented, 1000 * total / frames, frames / total);
        std::printf("  CPU side per frame: scene %.3f ms, scaler %.3f ms, interface %.3f ms, blit+present %.3f ms\n",
            1000 * tScene / frames, 1000 * tScale / frames, 1000 * tUi / frames, 1000 * tPresent / frames);

        // 8. Check the full-size surface: the flat field kept its colour, and the interface rectangle is exact.
        IOSurfaceLock(surfB, kIOSurfaceLockReadOnly, nullptr);
        // ANGLE's row 0 is the bottom row of what OpenGL drew; find the flat field and the rectangle either way up.
        Pixel flat = pixelAt(surfB, outW / 4, outH / 2);
        Pixel uiTop = pixelAt(surfB, 350, 240), uiBottom = pixelAt(surfB, 350, outH - 1 - 240);
        const bool flipped = !(uiTop.r > 150 && uiTop.g < 80);
        const int yIn = flipped ? outH - 1 - 240 : 240;
        const int yEdgeIn = flipped ? outH - 1 - 200 : 200, yEdgeOut = flipped ? outH - 1 - 199 : 199;
        Pixel inside = pixelAt(surfB, 350, yIn), border = pixelAt(surfB, 350, yEdgeIn), outside = pixelAt(surfB, 350, yEdgeOut);
        const bool flatOk = near(flat.r, 51, 8) && near(flat.g, 102, 8) && near(flat.b, 204, 8);
        const bool insideOk = near(inside.r, 204, 2) && near(inside.g, 26, 2) && near(inside.b, 26, 2);
        const bool borderOk = border.r == 255 && border.g == 255 && border.b == 255;
        const bool outsideOk = near(outside.r, 51, 8) && near(outside.g, 102, 8) && near(outside.b, 204, 8);
        std::printf("check: flat field rgb(%d,%d,%d) %s | interface inside rgb(%d,%d,%d) %s | border rgb(%d,%d,%d) %s | "
                    "one pixel outside rgb(%d,%d,%d) %s | rows %s\n",
            flat.r, flat.g, flat.b, flatOk ? "ok" : "WRONG", inside.r, inside.g, inside.b, insideOk ? "ok" : "WRONG",
            border.r, border.g, border.b, borderOk ? "ok" : "WRONG", outside.r, outside.g, outside.b,
            outsideOk ? "ok" : "WRONG", flipped ? "bottom-up" : "top-down");
        (void)uiBottom;
        if (!png.empty())
            writePng(surfB, png.c_str());
        IOSurfaceUnlock(surfB, kIOSurfaceLockReadOnly, nullptr);

        // 9. The other documented way: can ANGLE draw into a Metal texture the application made?
        if (has("EGL_ANGLE_metal_texture_client_buffer"))
        {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:64
                                                                                         height:64
                                                                                      mipmapped:NO];
            d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            id<MTLTexture> own = [device newTextureWithDescriptor:d];
            EGLImageKHR image = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_METAL_TEXTURE_ANGLE,
                (__bridge EGLClientBuffer)own, nullptr);
            if (image == EGL_NO_IMAGE_KHR)
                std::printf("Metal texture as drawing surface: image not made (EGL error 0x%x)\n", eglGetError());
            else
            {
                GLuint tex = 0, fbo = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
                glGenFramebuffers(1, &fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
                const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                glViewport(0, 0, 64, 64);
                glClearColor(0, 1, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glFinish();
                unsigned char px[4] = { 0, 0, 0, 0 };
                [own getBytes:px bytesPerRow:4 fromRegion:MTLRegionMake2D(32, 32, 1, 1) mipmapLevel:0];
                std::printf("Metal texture as drawing surface: framebuffer status 0x%x, GL error 0x%x, read back "
                            "bgra(%d,%d,%d,%d) %s\n",
                    status, glGetError(), px[0], px[1], px[2], px[3],
                    (px[1] == 255 && px[0] == 0 && px[2] == 0) ? "ok" : "WRONG");
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }

        const bool ok = flatOk && insideOk && borderOk && outsideOk;
        std::printf("%s\n", ok ? "RESULT: PASS" : "RESULT: FAIL");
        if (show)
            SDL_Delay(1500);
        return ok ? 0 : 2;
    }
}
