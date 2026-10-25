# SpinePreview rendering foundation

使用 SDL3 管理窗口和输入，Dear ImGui 绘制控制面板，通过 `~/Downloads/angle-mac-universal.zip` 内的 Google ANGLE，以 OpenGL ES 2.0 绘制 RGB 渐变三角形和 UI。渲染路径：

`SDL3 window → SDL_MetalView / CAMetalLayer → EGL → ANGLE OpenGL ES → Metal`

ImGui 使用 `imgui_impl_sdl3` 输入后端及定义了 `IMGUI_IMPL_OPENGL_ES2` 的 `imgui_impl_opengl3` 渲染后端。SDL 不创建 OpenGL 上下文；SDL 的原生 OpenGL / OpenGL ES 后端在构建时禁用。

显式选择 `EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE`，初始化时校验 `GL_RENDERER` 包含 ANGLE 和 Metal。没有链接或调用 macOS 的 `OpenGL.framework`，也不使用 `NSOpenGLView` / CGL。ANGLE 的 Metal 窗口表面实现可参阅[上游源码](https://github.com/google/angle/blob/main/src/libANGLE/renderer/metal/SurfaceMtl.mm)。

## 编译和运行

需要 macOS 12+、Apple Command Line Tools（`xcode-select --install`）和 CMake 3.24+，无需完整 Xcode。SDL 3.4.18 和 Dear ImGui 1.93.0 的完整上游源码随项目保存在 `Vendor/SDL3/` 和 `Vendor/imgui/`，ANGLE 头文件和动态库保存在 `Vendor/ANGLE/`。工具链就绪后，首次配置及编译均可离线完成，无需下载缓存、用户 Downloads 目录、系统安装的 SDL / ImGui 或包管理器。构建不使用 FetchContent 或其他网络下载方式。依赖来源及归档校验和见 `Vendor/README.md`。

```sh
bash scripts/build.sh
open "build/SpinePreview.app"
```

产物为 arm64 / x86_64 Universal `.app`，内置两份 ANGLE 动态库，采用本地 ad-hoc 签名。可直接在本机双击运行；面向其他用户正式分发时需要 Developer ID 签名和公证。

默认同时构建 arm64 / x86_64；可用 `BUILD_JOBS=4 bash scripts/build.sh` 调整并行编译数。目前交付及验证的是 macOS 版本，Windows/Linux 平台接入尚未实现，CMake 会明确提示。

支持 Retina 和窗口缩放；三角形保持比例。关闭窗口或按 ⌘Q 退出，退出时按顺序释放 ImGui、GL / EGL、Metal view 和 SDL 资源。控制面板支持：

- 三角形显隐、缩放、背景颜色调整、一键重置。
- `Show render info` 开关：显示窗口逻辑尺寸、帧缓冲像素尺寸、像素比例、预览区和实际三角形 viewport 尺寸。使用 SDL3 主循环回调，拖动窗口边缘调整大小时也会持续更新渲染和信息。
- 英文文本输入测试；文件拖入后显示路径（尚不解析 Spine 数据）。
- 实际 GL renderer、SDL / ImGui 版本、FPS 显示。

本阶段使用英文 UI 和默认字体，尚未配置 CJK 字体或验证输入法。没有接入 Spine runtime。

## 验证

在图形登录会话中运行：

```sh
"build/SpinePreview.app/Contents/MacOS/SpinePreview" --smoke-test
```

自检创建真实 SDL 窗口，检查 Metal renderer、着色器编译和链接、三角形/背景/ImGui 面板像素值及 UI 顶点数据，并调整窗口大小后再次检查。成功输出 `PASS` 并退出；错误返回非零退出码。

检查直接链接依赖：

```sh
otool -L "build/SpinePreview.app/Contents/MacOS/SpinePreview"
otool -L "build/SpinePreview.app/Contents/Frameworks/"*.dylib
```

## 文件

- `Sources/main.cpp`：SDL3 窗口和事件、EGL 初始化、GLSL ES 着色器、VBO、ImGui 面板及渲染循环。
- `CMakeLists.txt`：从本地 Vendor 源码构建、裁剪 SDL 子系统、构建及打包依赖许可证。
- `Sources/Info.plist`：应用包元数据。
- `scripts/build.sh`：通用二进制编译、动态库嵌入、本地签名。
- `Vendor/ANGLE/`：用户提供压缩包的原始头文件、动态库和 commit 标识；库最低 macOS 版本为 12.0。
- `Vendor/SDL3/`、`Vendor/imgui/`：随仓库保存的固定版本源码，保留上游许可证。

ANGLE commit：`5a62a37561da7bd0a859cc54d6a0eb05672be458`。

SDL3 和 Dear ImGui 的许可证随应用放在 `Contents/Resources/Licenses/`。
