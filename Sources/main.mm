#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static bool smokeTest = false;
static int exitStatus = 0;

static void requireEGL(EGLBoolean ok, const char *operation) {
    if (!ok) {
        char message[256];
        snprintf(message, sizeof(message), "%s failed (EGL 0x%04x)", operation, eglGetError());
        throw std::runtime_error(message);
    }
}

static GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(std::max(length, 1));
        glGetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compilation: ") + log.data());
    }
    return shader;
}

@interface TriangleView : NSView {
    EGLDisplay _display;
    EGLContext _context;
    EGLSurface _surface;
    GLuint _program;
    GLuint _vertices;
    NSTimer *_timer;
    BOOL _stopped;
    NSUInteger _frames;
    GLsizei _initialWidth;
}
- (void)start;
- (void)stop;
@end

@implementation TriangleView
- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _display = EGL_NO_DISPLAY;
        _context = EGL_NO_CONTEXT;
        _surface = EGL_NO_SURFACE;
        self.wantsLayer = YES;
        self.layer = [CAMetalLayer layer];
        self.layer.opaque = YES;
    }
    return self;
}

- (void)updateDrawableSize {
    CGFloat scale = self.window.backingScaleFactor ?: 1.0;
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(std::max(1.0, self.bounds.size.width * scale),
                                    std::max(1.0, self.bounds.size.height * scale));
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self updateDrawableSize];
}

- (void)start {
    try {
        [self updateDrawableSize];
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!getPlatformDisplay) throw std::runtime_error("eglGetPlatformDisplayEXT unavailable");
        // Explicit Metal selection: never fall back to Apple's OpenGL implementation.
        const EGLint displayAttributes[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE
        };
        _display = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr,
                                      displayAttributes);
        requireEGL(_display != EGL_NO_DISPLAY, "eglGetPlatformDisplayEXT(Metal)");
        requireEGL(eglInitialize(_display, nullptr, nullptr), "eglInitialize");
        requireEGL(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
        const EGLint configAttributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        requireEGL(eglChooseConfig(_display, configAttributes, &config, 1, &count), "eglChooseConfig");
        if (count != 1) throw std::runtime_error("No compatible EGL window configuration");
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        _context = eglCreateContext(_display, config, EGL_NO_CONTEXT, contextAttributes);
        requireEGL(_context != EGL_NO_CONTEXT, "eglCreateContext");
        _surface = eglCreateWindowSurface(_display, config,
                                          (__bridge EGLNativeWindowType)self.layer, nullptr);
        requireEGL(_surface != EGL_NO_SURFACE, "eglCreateWindowSurface");
        requireEGL(eglMakeCurrent(_display, _surface, _surface, _context), "eglMakeCurrent");
        requireEGL(eglSwapInterval(_display, 1), "eglSwapInterval");
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        if (!renderer || !strstr(renderer, "ANGLE") || !strstr(renderer, "Metal"))
            throw std::runtime_error("Expected ANGLE Metal renderer");
        fprintf(stdout, "GL_RENDERER: %s\nGL_VERSION: %s\n", renderer, glGetString(GL_VERSION));
        fflush(stdout);
        self.window.subtitle = [NSString stringWithUTF8String:renderer];

        const char *vertexSource =
            "attribute vec2 position;\n"
            "attribute vec3 color;\n"
            "varying vec3 vertexColor;\n"
            "void main() { gl_Position = vec4(position, 0.0, 1.0); vertexColor = color; }\n";
        const char *fragmentSource =
            "precision mediump float;\n"
            "varying vec3 vertexColor;\n"
            "void main() { gl_FragColor = vec4(vertexColor, 1.0); }\n";
        GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
        GLuint fragment = 0;
        try { fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource); }
        catch (...) { glDeleteShader(vertex); throw; }
        _program = glCreateProgram();
        glAttachShader(_program, vertex);
        glAttachShader(_program, fragment);
        glBindAttribLocation(_program, 0, "position");
        glBindAttribLocation(_program, 1, "color");
        glLinkProgram(_program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        glGetProgramiv(_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint length = 0;
            glGetProgramiv(_program, GL_INFO_LOG_LENGTH, &length);
            std::vector<char> log(std::max(length, 1));
            glGetProgramInfoLog(_program, (GLsizei)log.size(), nullptr, log.data());
            throw std::runtime_error(std::string("Program link: ") + log.data());
        }
        const GLfloat vertices[] = {
             0.00f,  0.78f, 1.00f, 0.25f, 0.30f,
            -0.78f, -0.65f, 0.25f, 0.90f, 0.50f,
             0.78f, -0.65f, 0.20f, 0.50f, 1.00f
        };
        glGenBuffers(1, &_vertices);
        glBindBuffer(GL_ARRAY_BUFFER, _vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        __weak TriangleView *weakSelf = self;
        _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 repeats:YES block:^(NSTimer *) {
            [weakSelf render];
        }];
        [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    } catch (const std::exception &error) {
        [self fail:error.what()];
    }
}

- (void)render {
    if (_stopped || !self.window.isVisible || self.window.isMiniaturized) return;
    try {
        [self updateDrawableSize];
        requireEGL(eglMakeCurrent(_display, _surface, _surface, _context), "eglMakeCurrent");
        EGLint width = 0, height = 0;
        requireEGL(eglQuerySurface(_display, _surface, EGL_WIDTH, &width), "eglQuerySurface width");
        requireEGL(eglQuerySurface(_display, _surface, EGL_HEIGHT, &height), "eglQuerySurface height");
        glViewport(0, 0, width, height);
        glClearColor(0.035f, 0.045f, 0.075f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // A square viewport keeps the triangle's proportions when the window is resized.
        GLsizei side = std::min(width, height);
        glViewport((width - side) / 2, (height - side) / 2, side, side);
        glUseProgram(_program);
        glBindBuffer(GL_ARRAY_BUFFER, _vertices);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                              reinterpret_cast<void *>(2 * sizeof(GLfloat)));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (smokeTest && (_frames == 10 || _frames == 30)) {
            GLubyte center[4] = {}, corner[4] = {};
            glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
            glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            if (center[0] < 50 || center[1] < 50 || center[2] < 50 ||
                corner[0] > 20 || corner[1] > 20 || corner[2] > 30)
                throw std::runtime_error("Framebuffer triangle/background pixel check failed");
            fprintf(stdout, "Pixel check OK: %dx%d, center=(%u,%u,%u), background=(%u,%u,%u)\n",
                    width, height, center[0], center[1], center[2], corner[0], corner[1], corner[2]);
            if (_frames == 10) _initialWidth = width;
            else if (width == _initialWidth) throw std::runtime_error("Resize check failed");
        }
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL ES rendering error");
        requireEGL(eglSwapBuffers(_display, _surface), "eglSwapBuffers");
        if (smokeTest && ++_frames == 12) [self.window setContentSize:NSMakeSize(720, 480)];
        if (smokeTest && _frames == 32) {
            fprintf(stdout, "PASS: ANGLE Metal window, shaders, triangle pixels, resize, swap.\n");
            fflush(stdout);
            [NSApp terminate:nil];
        }
    } catch (const std::exception &error) {
        [self fail:error.what()];
    }
}

- (void)fail:(const char *)message {
    fprintf(stderr, "ANGLE Triangle: %s\n", message);
    exitStatus = 1;
    [self stop];
    if (!smokeTest) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"ANGLE rendering failed";
        alert.informativeText = [NSString stringWithUTF8String:message];
        [alert runModal];
    }
    [NSApp terminate:nil];
}

- (void)stop {
    if (_stopped) return;
    _stopped = YES;
    [_timer invalidate];
    _timer = nil;
    if (_display != EGL_NO_DISPLAY) {
        if (_context != EGL_NO_CONTEXT && _surface != EGL_NO_SURFACE &&
            eglMakeCurrent(_display, _surface, _surface, _context)) {
            glDeleteBuffers(1, &_vertices);
            if (_program) glDeleteProgram(_program);
        }
        eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (_surface != EGL_NO_SURFACE) eglDestroySurface(_display, _surface);
        if (_context != EGL_NO_CONTEXT) eglDestroyContext(_display, _context);
        eglTerminate(_display);
        eglReleaseThread();
    }
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(strong) NSWindow *window;
@property(strong) TriangleView *triangle;
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    NSMenu *menu = [[NSMenu alloc] init];
    NSMenuItem *application = [[NSMenuItem alloc] init];
    [menu addItem:application];
    NSMenu *applicationMenu = [[NSMenu alloc] initWithTitle:@"ANGLE Triangle"];
    [applicationMenu addItemWithTitle:@"Quit ANGLE Triangle" action:@selector(terminate:) keyEquivalent:@"q"];
    application.submenu = applicationMenu;
    NSApp.mainMenu = menu;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 640)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"ANGLE Triangle · OpenGL ES → Metal";
    self.window.minSize = NSMakeSize(360, 280);
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    self.triangle = [[TriangleView alloc] initWithFrame:NSMakeRect(0, 0, 960, 640)];
    self.window.contentView = self.triangle;
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self.triangle start];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}
- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    [self.triangle stop];
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    [self.triangle stop];
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        smokeTest = argc > 1 && strcmp(argv[1], "--smoke-test") == 0;
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
        return exitStatus;
    }
}
