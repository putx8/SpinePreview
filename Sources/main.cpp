#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_metal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "SpinePreview.h"
#include "FileLoader.h"
#include "AppProcess.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <charconv>
#include <optional>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static void checkEGL(EGLBoolean ok, const char *operation) {
    if (!ok) {
        char message[256];
        std::snprintf(message, sizeof(message), "%s failed (EGL 0x%04x)", operation, eglGetError());
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

// Destroy GL resources before EGL, the Metal view, and the SDL window.
struct Application {
    SDL_Window *window = nullptr;
    SDL_MetalView metalView = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint program = 0, vertices = 0;
    bool platformReady = false, rendererReady = false, current = false;
    std::string renderer;
    bool smokeTest = false, running = true, showTriangle = false, showRenderInfo = false;
    std::unique_ptr<SpinePreview> spine;
    FileLoader files;
    std::string assetLabel = "Spineboy";
    std::filesystem::path smokeSkeleton, smokeAtlas;
    std::optional<std::pair<int, int>> initialPosition;
    int windowWidth = 1136, windowHeight = 640;
    bool showPanelForDrop = false;
    bool playing = true, loop = true;
    float speed = 1.0f;
    int animationIndex = 0, skinIndex = 0;
    unsigned long long sampleHash = 0;
    float scale = 1.0f;
    ImVec2 pan{0, 0};
    ImVec2 dragStartPan{0, 0};
    bool draggingPreview = false;
    float background[3] = {0.035f, 0.045f, 0.075f};
    int frames = 0, initialWidth = 0;
    Uint64 started = 0;
    Uint32 newInstanceEvent = 0;

    ~Application() {
        files.cancel();
        spine.reset();
        if (rendererReady) ImGui_ImplOpenGL3_Shutdown();
        if (platformReady) ImGui_ImplSDL3_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        if (current) {
            glDeleteBuffers(1, &vertices);
            if (program) glDeleteProgram(program);
        }
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            eglTerminate(display);
            eglReleaseThread();
        }
        if (metalView) SDL_Metal_DestroyView(metalView);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void initialize() {
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        started = SDL_GetTicks();
        newInstanceEvent = SDL_RegisterEvents(1);
        check(newInstanceEvent != 0, "Cannot register the new-instance event");
        installApplicationMenu(newInstanceEvent);
        window = SDL_CreateWindow(SPINE_APP_NAME, windowWidth, windowHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL | SDL_WINDOW_HIDDEN);
        check(window != nullptr, SDL_GetError());
        check(SDL_SetWindowMinimumSize(window, 760, 480), SDL_GetError());
        if (initialPosition) {
            SDL_Rect desired{initialPosition->first, initialPosition->second, windowWidth, windowHeight}, usable{};
            const auto displayID = SDL_GetDisplayForRect(&desired);
            if (displayID && SDL_GetDisplayUsableBounds(displayID, &usable)) {
                desired.x = std::clamp(desired.x, usable.x, usable.x+std::max(0, usable.w-desired.w));
                desired.y = std::clamp(desired.y, usable.y, usable.y+std::max(0, usable.h-desired.h));
            }
            check(SDL_SetWindowPosition(window, desired.x, desired.y), SDL_GetError());
        }
        check(SDL_ShowWindow(window), SDL_GetError());
        metalView = SDL_Metal_CreateView(window);
        check(metalView != nullptr, SDL_GetError());
        auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        check(getDisplay != nullptr, "eglGetPlatformDisplayEXT unavailable");
        const EGLint attributes[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE
        };
        display = getDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, attributes);
        checkEGL(display != EGL_NO_DISPLAY, "eglGetPlatformDisplayEXT(Metal)");
        checkEGL(eglInitialize(display, nullptr, nullptr), "eglInitialize");
        checkEGL(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");
        const EGLint configAttributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        checkEGL(eglChooseConfig(display, configAttributes, &config, 1, &count), "eglChooseConfig");
        check(count == 1, "No compatible EGL configuration");
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        checkEGL(context != EGL_NO_CONTEXT, "eglCreateContext");
        surface = eglCreateWindowSurface(display, config, SDL_Metal_GetLayer(metalView), nullptr);
        checkEGL(surface != EGL_NO_SURFACE, "eglCreateWindowSurface");
        checkEGL(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
        current = true;
        checkEGL(eglSwapInterval(display, 1), "eglSwapInterval");
        const char *name = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        check(name && std::strstr(name, "ANGLE") && std::strstr(name, "Metal"),
              "Expected ANGLE Metal renderer");
        renderer = name;
        std::printf("SDL %d | Dear ImGui %s\nGL_RENDERER: %s\nGL_VERSION: %s\n",
                    SDL_GetVersion(), IMGUI_VERSION, name, glGetString(GL_VERSION));
        std::fflush(stdout);
        const char *vertexSource =
            "attribute vec2 position; attribute vec3 color;\n"
            "uniform float scale; varying vec3 vertexColor;\n"
            "void main(){ gl_Position=vec4(position*scale,0.0,1.0); vertexColor=color; }\n";
        const char *fragmentSource =
            "precision mediump float; varying vec3 vertexColor;\n"
            "void main(){ gl_FragColor=vec4(vertexColor,1.0); }\n";
        GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
        GLuint fragment = 0;
        try { fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource); }
        catch (...) { glDeleteShader(vertex); throw; }
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glBindAttribLocation(program, 0, "position");
        glBindAttribLocation(program, 1, "color");
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[2048] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("Program link: ") + log);
        }
        const GLfloat data[] = {
             0.00f,  0.78f, 1.00f, 0.25f, 0.30f,
            -0.78f, -0.65f, 0.25f, 0.90f, 0.50f,
             0.78f, -0.65f, 0.20f, 0.50f, 1.00f
        };
        glGenBuffers(1, &vertices);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImFontConfig latin;
        latin.SizePixels = 16;
        ImGui::GetIO().Fonts->AddFontDefault(&latin);
        const char *unicodeFont = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf";
        if (std::filesystem::exists(unicodeFont)) {
            ImFontConfig unicode;
            unicode.MergeMode = true;
            ImGui::GetIO().Fonts->AddFontFromFileTTF(unicodeFont, 16, &unicode);
        }
        ImGui::StyleColorsDark();
        // SDL handles input only. No SDL GL context is created.
        platformReady = ImGui_ImplSDL3_InitForOther(window);
        check(platformReady, "ImGui SDL3 initialization failed");
        rendererReady = ImGui_ImplOpenGL3_Init("#version 100");
        check(rendererReady, "ImGui OpenGL ES initialization failed");
        const char *base = SDL_GetBasePath();
        check(base != nullptr, SDL_GetError());
        const auto directory = std::filesystem::path(base)/"spineboy";
        const auto sample = prepareResources(smokeSkeleton.empty() ? directory/SPINE_SAMPLE_JSON : smokeSkeleton,
                                             smokeAtlas.empty() ? directory/"spineboy.atlas" : smokeAtlas);
        spine.reset(new SpinePreview(sample));
        files.loaded(sample);
        const auto &animations = spine->animations();
        const auto walk = std::find(animations.begin(), animations.end(), "walk");
        animationIndex = walk == animations.end() ? 0 : static_cast<int>(walk - animations.begin());
        if (smokeTest) {
            for (size_t i = 0; i < spine->skins().size(); ++i) { spine->setSkin(static_cast<int>(i)); spine->update(0); }
            for (size_t i = 0; i < animations.size(); ++i) { spine->setAnimation(static_cast<int>(i), true); spine->update(0.1f); }
            if (!animations.empty()) {
                spine->setAnimation(animationIndex, true); spine->setSkin(0); spine->setTime(0.25f);
                check(std::fabs(spine->time()-0.25f) < 0.001f, "Runtime seek check failed");
                spine->update(0);
                check(std::fabs(spine->time()-0.25f) < 0.001f, "Runtime pause check failed");
                spine->setAnimation(animationIndex, false); spine->update(100);
                const float endTime = spine->time(); spine->update(0);
                check(endTime > 0 && spine->time() == endTime, "Runtime end-pose check failed");
                spine->setAnimation(animationIndex, true); spine->setTime(0);
            }
        }
        std::printf("Spine runtime " SPINE_RUNTIME_VERSION " | export %s | %zu animations | %zu skins\n",
                    spine->version().c_str(), animations.size(), spine->skins().size());
    }

    void openNewInstance() {
        std::string error;
        if (!launchNewApplication(window, error)) files.failed("Cannot open a new instance: " + error);
    }

    void handleEvent(const SDL_Event &event) {
        if (event.type == newInstanceEvent) { openNewInstance(); return; }
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) draggingPreview = false;
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            files.drop(event.drop.data);
            showPanelForDrop = true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_Q &&
            (event.key.mod & SDL_KMOD_GUI)) running = false;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.mod & SDL_KMOD_GUI)) {
            if (event.key.key == SDLK_O) files.open(window);
            if (event.key.key == SDLK_R) files.reload();
        }
    }

    // SDL callbacks continue drawing during the native macOS live-resize loop.
    SDL_AppResult iterate() {
        if (!running) {
            check(!smokeTest, "Smoke test interrupted before completion");
            return SDL_APP_SUCCESS;
        }
        if (smokeTest && SDL_GetTicks() - started > 15000)
            throw std::runtime_error("Smoke test timed out");
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(20);
            return SDL_APP_CONTINUE;
        }
        checkEGL(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
        files.update();
        if (auto error = takeApplicationLaunchError(); !error.empty())
            files.failed("Cannot open a new instance: " + error);
        if (files.wantsPanel()) showPanelForDrop = true;
        if (auto resource = files.takeValidated()) {
            try {
                auto replacement = std::make_unique<SpinePreview>(*resource);
                const bool sameAsset = files.isCurrent(*resource);
                int nextAnimation = 0, nextSkin = 0;
                const auto &names = replacement->animations();
                const auto walk = std::find(names.begin(), names.end(), "walk");
                if (walk != names.end()) nextAnimation = static_cast<int>(walk-names.begin());
                if (sameAsset) {
                    const auto &oldAnimations = spine->animations();
                    if (!oldAnimations.empty()) {
                        const auto found = std::find(names.begin(), names.end(), oldAnimations[animationIndex]);
                        if (found != names.end()) nextAnimation = static_cast<int>(found-names.begin());
                    }
                    const auto &skins = replacement->skins();
                    if (!spine->skins().empty()) {
                        const auto found = std::find(skins.begin(), skins.end(), spine->skins()[skinIndex]);
                        if (found != skins.end()) nextSkin = static_cast<int>(found-skins.begin());
                    }
                    replacement->setAnimation(nextAnimation, loop);
                    replacement->setSkin(nextSkin);
                    replacement->setTime(spine->time());
                }
                std::string label = resource->skeleton.filename().string();
                files.loaded(*resource);
                spine.swap(replacement);
                assetLabel.swap(label);
                animationIndex = nextAnimation; skinIndex = nextSkin;
                if (!sameAsset) { showTriangle = false; scale = 1; pan = ImVec2(0, 0); draggingPreview = false; playing = true; loop = true; }
            } catch (const std::exception &e) { files.failed(e.what()); }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (smokeTest) {
            auto &input = ImGui::GetIO();
            input.ConfigInputTrickleEventQueue = false;
            if (frames >= 40 && frames <= 43) input.AddMousePosEvent(frames < 42 ? 650 : 730, frames < 42 ? 300 : 340);
            if (frames == 41) input.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            if (frames == 42) input.AddMousePosEvent(730, 340);
            if (frames == 43) input.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        ImGui::NewFrame();
        const ImGuiIO &io = ImGui::GetIO();
        EGLint width = 0, height = 0;
        checkEGL(eglQuerySurface(display, surface, EGL_WIDTH, &width), "eglQuerySurface width");
        checkEGL(eglQuerySurface(display, surface, EGL_HEIGHT, &height), "eglQuerySurface height");
        if (width <= 0 || height <= 0 || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
            ImGui::EndFrame();
            return SDL_APP_CONTINUE;
        }
        ImGui::SetNextWindowPos(ImVec2(12, 12));
        ImGui::SetNextWindowSize(ImVec2(280, io.DisplaySize.y - 24), ImGuiCond_FirstUseEver);
        // Keep a usable preview area while allowing the panel width to be adjusted.
        ImGui::SetNextWindowSizeConstraints(ImVec2(240, io.DisplaySize.y - 24),
            ImVec2(std::min(440.0f, io.DisplaySize.x - 360), io.DisplaySize.y - 24));
        if (showPanelForDrop) { ImGui::SetNextWindowCollapsed(false); showPanelForDrop = false; }
        if (smokeTest && frames == 34) ImGui::SetNextWindowCollapsed(true);
        if (smokeTest && frames == 38) ImGui::SetNextWindowCollapsed(false);
        const bool panelVisible = ImGui::Begin(SPINE_APP_NAME, nullptr, ImGuiWindowFlags_NoMove);
        const bool collapsed = ImGui::IsWindowCollapsed();
        const ImVec2 panelPosition = ImGui::GetWindowPos();
        const ImVec2 panelSize = ImGui::GetWindowSize();
        // A collapsed panel occupies only its title bar. Reserve a strip above
        // the preview so even that title bar cannot cover the animation.
        const ImVec2 previewMin = collapsed
            ? ImVec2(12, panelPosition.y + panelSize.y + 12)
            : ImVec2(panelPosition.x + panelSize.x + 12, 12);
        const ImVec2 previewMax(io.DisplaySize.x - 12, io.DisplaySize.y - 12);
        const int left = static_cast<int>(previewMin.x * width / io.DisplaySize.x);
        const int top = static_cast<int>(previewMin.y * height / io.DisplaySize.y);
        const int previewWidth = static_cast<int>((previewMax.x - previewMin.x) * width / io.DisplaySize.x);
        const int previewHeight = static_cast<int>((previewMax.y - previewMin.y) * height / io.DisplaySize.y);
        const int side = std::min(previewWidth, previewHeight);
        const int centerX = left + previewWidth / 2;
        const int centerY = height - top - previewHeight / 2;
        if (smokeTest && frames == 34) check(collapsed && previewMin.x == 12, "Panel collapse layout failed");
        if (smokeTest && frames == 38) check(!collapsed && previewMin.x > panelSize.x, "Panel expansion layout failed");
        if (panelVisible) {
            if (ImGui::Button("New instance (Cmd+N)")) openNewInstance();
            ImGui::TextUnformatted("SDL3 + Dear ImGui + ANGLE");
            ImGui::Text("Runtime: " SPINE_RUNTIME_VERSION " / Export: %s", spine->version().c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", renderer.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("%s", assetLabel.c_str());
            const auto &animations = spine->animations();
            if (!animations.empty() && ImGui::BeginCombo("Animation", animations[animationIndex].c_str())) {
                for (size_t i = 0; i < animations.size(); ++i) {
                    if (ImGui::Selectable(animations[i].c_str(), static_cast<int>(i) == animationIndex)) {
                        animationIndex = static_cast<int>(i);
                        spine->setAnimation(animationIndex, loop);
                    }
                }
                ImGui::EndCombo();
            }
            if (animations.empty()) ImGui::TextDisabled("No animations: setup pose");
            const auto &skins = spine->skins();
            if (!skins.empty() && ImGui::BeginCombo("Skin", skins[skinIndex].c_str())) {
                for (size_t i = 0; i < skins.size(); ++i) {
                    if (ImGui::Selectable(skins[i].c_str(), static_cast<int>(i) == skinIndex)) {
                        skinIndex = static_cast<int>(i);
                        spine->setSkin(skinIndex);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Playing", &playing);
            ImGui::SameLine();
            if (ImGui::Checkbox("Loop", &loop)) spine->setAnimation(animationIndex, loop);
            ImGui::SliderFloat("Speed", &speed, 0.0f, 2.0f, "%.2fx");
            if (ImGui::Button("Restart")) spine->setAnimation(animationIndex, loop);
            ImGui::SameLine();
            ImGui::Text("%.2f s", spine->time());
            ImGui::Checkbox("Triangle test", &showTriangle);
            ImGui::SliderFloat("Scale", &scale, 0.2f, 1.2f, "%.2fx");
            ImGui::ColorEdit3("Background", background, ImGuiColorEditFlags_NoInputs);
            if (ImGui::Button("Reset")) {
                showTriangle = false;
                scale = 1.0f;
                pan = ImVec2(0, 0);
                draggingPreview = false;
                background[0] = 0.035f; background[1] = 0.045f; background[2] = 0.075f;
            }
            ImGui::TextDisabled("Drag preview to move");
            ImGui::Separator();
            files.draw(window);
            ImGui::Separator();
            ImGui::Checkbox("Show render info", &showRenderInfo);
            if (showRenderInfo) {
                ImGui::Text("Window: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
                ImGui::Text("Framebuffer: %d x %d px", width, height);
                ImGui::Text("Pixel scale: %.2f x %.2f", width / io.DisplaySize.x, height / io.DisplaySize.y);
                ImGui::Text("Preview: %d x %d px", previewWidth, previewHeight);
                ImGui::Text("Viewport: %d x %d px", showTriangle ? side : previewWidth,
                            showTriangle ? side : previewHeight);
                ImGui::TextDisabled("Updates while resizing");
            }
            ImGui::Separator();
            ImGui::Text("%.1f FPS", io.Framerate);
            ImGui::Text("SDL %d / ImGui %s", SDL_GetVersion(), IMGUI_VERSION);
        }
        ImGui::End();
        const bool overPreview = ImGui::IsMouseHoveringRect(previewMin, previewMax, false);
        if (!showTriangle && overPreview && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            draggingPreview = true;
            dragStartPan = pan;
        }
        if (showTriangle) draggingPreview = false;
        if (draggingPreview) {
            const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0);
            pan = ImVec2(dragStartPan.x+delta.x, dragStartPan.y+delta.y);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) draggingPreview = false;
        }
        if (!showTriangle && (draggingPreview || (overPreview && !io.WantCaptureMouse)))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (smokeTest && frames == 43) {
            check(std::fabs(pan.x-80) < 0.01f && std::fabs(pan.y-40) < 0.01f && !draggingPreview,
                  "Preview mouse drag check failed");
            pan = ImVec2(0, 0);
        }
        spine->update(playing ? (smokeTest ? 1.0f / 60.0f : std::min(io.DeltaTime, 0.1f)) * speed : 0.0f);
        if (!showTriangle)
            spine->draw(ImGui::GetBackgroundDrawList(), previewMin, previewMax, scale, pan);
        ImGui::Render();
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glViewport(0, 0, width, height);
        glClearColor(background[0], background[1], background[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(centerX - side / 2, centerY - side / 2, side, side);
        if (showTriangle) {
            glUseProgram(program);
            glUniform1f(glGetUniformLocation(program, "scale"), scale);
            glBindBuffer(GL_ARRAY_BUFFER, vertices);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                                  reinterpret_cast<void *>(2 * sizeof(GLfloat)));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (smokeTest && (frames == 10 || frames == 30)) {
            GLubyte center[4] = {}, corner[4] = {}, panel[4] = {};
            glReadPixels(centerX, centerY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
            glReadPixels(width - 3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            // Sample the panel's empty left margin, independent of control layout.
            const int panelX = static_cast<int>((panelPosition.x + 3) * width / io.DisplaySize.x);
            glReadPixels(panelX, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, panel);
            check(center[0] > 50 && center[1] > 50 && center[2] > 50, "Triangle pixel check failed");
            check(corner[0] < 20 && corner[1] < 20 && corner[2] < 30, "Background pixel check failed");
            check(panel[0] > corner[0] + 5 && ImGui::GetDrawData()->TotalVtxCount > 100,
                  "ImGui panel rendering check failed");
            if (frames == 10) initialWidth = width;
            else check(width != initialWidth, "SDL resize check failed");
            std::printf("Pixel check OK: %dx%d, triangle=(%u,%u,%u), ImGui=(%u,%u,%u)\n",
                        width, height, center[0], center[1], center[2], panel[0], panel[1], panel[2]);
        }
        if (smokeTest && (frames == 45 || frames == 60)) {
            std::vector<unsigned char> pixels(static_cast<size_t>(previewWidth) * previewHeight * 4);
            glReadPixels(left, height - top - previewHeight, previewWidth, previewHeight,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            size_t visible = 0;
            unsigned long long hash = 1469598103934665603ULL;
            for (size_t i = 0; i < pixels.size(); i += 4) {
                if (pixels[i] > 30 || pixels[i + 1] > 30 || pixels[i + 2] > 30) ++visible;
                hash = (hash ^ pixels[i]) * 1099511628211ULL;
            }
            check(visible > 1000, "Spine textured geometry pixel check failed");
            if (frames == 45) sampleHash = hash;
            else check(hash != sampleHash, "Spine animation did not change the framebuffer");
            std::printf("Spine sample check OK: %zu visible pixels, animation time %.2f s\n", visible, spine->time());
        }
        check(glGetError() == GL_NO_ERROR, "OpenGL ES rendering error");
        checkEGL(eglSwapBuffers(display, surface), "eglSwapBuffers");
        if (frames == 0 && initialPosition) {
            // Activate the newly launched instance once its first frame is ready.
            SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
            check(SDL_RaiseWindow(window), SDL_GetError());
        }
        ++frames;
        if (smokeTest && frames == 12) check(SDL_SetWindowSize(window, 900, 600), SDL_GetError());
        if (smokeTest && frames == 32) showTriangle = false;
        if (smokeTest && frames == 64) {
            std::puts("PASS: SDL3, ANGLE Metal, triangle, ImGui, resize, Spine " SPINE_RUNTIME_VERSION " textured animation.");
            return SDL_APP_SUCCESS;
        }
        SDL_Delay(1);
        return SDL_APP_CONTINUE;
    }
};

static SDL_AppResult reportError(Application *app, const std::exception &error) {
    std::fprintf(stderr, "SpinePreview: %s\n", error.what());
    if (!app || !app->smokeTest)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SpinePreview", error.what(), nullptr);
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    if (argc > 1 && std::strcmp(argv[1], "--validate-resource") == 0)
        return validateResourceArguments(argc, argv) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    Application *app = nullptr;
    try {
        app = new Application;
        *appstate = app;
        app->smokeTest = argc > 1 && std::strcmp(argv[1], "--smoke-test") == 0;
        app->showRenderInfo = app->smokeTest;
        app->showTriangle = app->smokeTest;
        if (app->smokeTest && argc != 2 && argc != 4)
            throw std::runtime_error("Usage: --smoke-test [skeleton.json atlas.atlas]");
        if (app->smokeTest && argc == 4) { app->smokeSkeleton = argv[2]; app->smokeAtlas = argv[3]; }
        int firstFile = 1;
        while (firstFile < argc && (std::strcmp(argv[firstFile], "--window-position") == 0 ||
                                    std::strcmp(argv[firstFile], "--window-size") == 0)) {
            if (argc-firstFile < 3) throw std::runtime_error("Window position/size requires two integers");
            auto coordinate = [](const char *text) {
                int value = 0;
                const char *end = text + std::strlen(text);
                const auto result = std::from_chars(text, end, value);
                if (result.ec != std::errc{} || result.ptr != end)
                    throw std::runtime_error("Invalid window position/size");
                return value;
            };
            const int first = coordinate(argv[firstFile+1]), second = coordinate(argv[firstFile+2]);
            if (std::strcmp(argv[firstFile], "--window-position") == 0) app->initialPosition = {first, second};
            else {
                if (first < 760 || second < 480)
                    throw std::runtime_error("Window size must be at least 760 x 480");
                app->windowWidth = first; app->windowHeight = second;
            }
            firstFile += 3;
        }
        app->initialize();
        if (!app->smokeTest) for (int i = firstFile; i < argc; ++i) app->files.drop(argv[i]);
        return SDL_APP_CONTINUE;
    } catch (const std::exception &error) {
        return reportError(app, error);
    }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    auto *app = static_cast<Application *>(appstate);
    try { app->handleEvent(*event); return SDL_APP_CONTINUE; }
    catch (const std::exception &error) { return reportError(app, error); }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto *app = static_cast<Application *>(appstate);
    try { return app->iterate(); }
    catch (const std::exception &error) { return reportError(app, error); }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) {
    delete static_cast<Application *>(appstate);
}
