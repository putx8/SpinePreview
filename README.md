# ANGLE Triangle for macOS

使用 `~/Downloads/angle-mac-universal.zip` 内的 Google ANGLE，以 OpenGL ES 2.0 绘制 RGB 渐变三角形。渲染路径：

`Cocoa NSWindow → CAMetalLayer → EGL → ANGLE OpenGL ES → Metal`

显式选择 `EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE`，初始化时校验 `GL_RENDERER` 包含 ANGLE 和 Metal。没有链接或调用 macOS 的 `OpenGL.framework`，也不使用 `NSOpenGLView` / CGL。ANGLE 的 Metal 窗口表面实现可参阅[上游源码](https://github.com/google/angle/blob/main/src/libANGLE/renderer/metal/SurfaceMtl.mm)。

## 编译和运行

需要 macOS 12+ 和 Apple Command Line Tools（`xcode-select --install`），无需完整 Xcode 或第三方构建工具。

```sh
bash scripts/build.sh
open "build/ANGLE Triangle.app"
```

产物为 arm64 / x86_64 Universal `.app`，内置两份 ANGLE 动态库，采用本地 ad-hoc 签名。可直接在本机双击运行；面向其他用户正式分发时需要 Developer ID 签名和公证。

支持 Retina、窗口缩放及跨显示器像素比例更新；三角形保持比例。关闭窗口或按 ⌘Q 退出，退出时释放 GL / EGL 资源。程序将实际 GL renderer 和版本输出到标准输出，并在窗口副标题显示 renderer。

## 验证

在图形登录会话中运行：

```sh
"build/ANGLE Triangle.app/Contents/MacOS/ANGLETriangle" --smoke-test
```

自检创建真实窗口，检查 Metal renderer、着色器编译和链接、中心三角形/角落背景的像素值，并调整窗口大小后再次读取像素，成功输出 `PASS` 并退出；错误返回非零退出码。

检查直接链接依赖：

```sh
otool -L "build/ANGLE Triangle.app/Contents/MacOS/ANGLETriangle"
otool -L "build/ANGLE Triangle.app/Contents/Frameworks/"*.dylib
```

## 文件

- `Sources/main.mm`：Cocoa 窗口、EGL 初始化、GLSL ES 着色器、VBO 和渲染循环。
- `Sources/Info.plist`：应用包元数据。
- `scripts/build.sh`：通用二进制编译、动态库嵌入、本地签名。
- `Vendor/ANGLE/`：用户提供压缩包的原始头文件、动态库和 commit 标识；库最低 macOS 版本为 12.0。

ANGLE commit：`5a62a37561da7bd0a859cc54d6a0eb05672be458`。
