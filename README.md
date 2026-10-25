# SpinePreview3.8

使用 SDL3 管理窗口和输入，Dear ImGui 绘制控制面板，通过用户提供的 Google ANGLE，以 OpenGL ES 2.0 绘制 Spine 3.8 动画和 UI。保留 RGB 三角形作为渲染测试模式。渲染路径：

`SDL3 window → SDL_MetalView / CAMetalLayer → EGL → ANGLE OpenGL ES → Metal`

ImGui 使用 `imgui_impl_sdl3` 输入后端及定义了 `IMGUI_IMPL_OPENGL_ES2` 的 `imgui_impl_opengl3` 渲染后端。SDL 不创建 OpenGL 上下文；SDL 的原生 OpenGL / OpenGL ES 后端在构建时禁用。

显式选择 `EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE`，初始化时校验 `GL_RENDERER` 包含 ANGLE 和 Metal。没有链接或调用 macOS 的 `OpenGL.framework`，也不使用 `NSOpenGLView` / CGL。ANGLE 的 Metal 窗口表面实现可参阅[上游源码](https://github.com/google/angle/blob/main/src/libANGLE/renderer/metal/SurfaceMtl.mm)。

## 编译和运行

需要 macOS 12+、Apple Command Line Tools（`xcode-select --install`）和 CMake 3.24+，无需完整 Xcode。SDL 3.4.18 和 Dear ImGui 1.93.0 的完整上游源码随项目保存在 `Vendor/SDL3/` 和 `Vendor/imgui/`，ANGLE 头文件和动态库保存在 `Vendor/ANGLE/`。工具链就绪后，首次配置及编译均可离线完成，无需下载缓存、用户 Downloads 目录、系统安装的 SDL / ImGui 或包管理器。构建不使用 FetchContent 或其他网络下载方式。依赖来源及归档校验和见 `Vendor/README.md`。

```sh
bash scripts/build.sh
open "build/SpinePreview3.8.app"
```

产物为 arm64 / x86_64 Universal `.app`，内置两份 ANGLE 动态库，采用本地 ad-hoc 签名。可直接在本机双击运行；面向其他用户正式分发时需要 Developer ID 签名和公证。

默认同时构建 arm64 / x86_64；可用 `BUILD_JOBS=4 bash scripts/build.sh` 调整并行编译数。目前交付及验证的是 macOS 版本，Windows/Linux 平台接入尚未实现，CMake 会明确提示。

支持 Retina 和窗口缩放；三角形保持比例。关闭窗口或按 ⌘Q 退出，退出时按顺序释放 ImGui、GL / EGL、Metal view 和 SDL 资源。控制面板支持：

- 点击面板标题左侧箭头或双击标题栏折叠/展开；拖动面板右下角调整宽度。展开时动画预览区位于面板右侧，折叠时扩展至标题栏下方，自动重新适配位置和比例，不与面板重叠。
- 默认播放官方 Spineboy 示例，可切换动画和皮肤，播放/暂停、循环、调整速度和重新播放。
- `Triangle test` 可切换回三角形渲染测试；支持缩放、背景颜色调整、一键重置。
- `Show render info` 开关：显示窗口逻辑尺寸、帧缓冲像素尺寸、像素比例、预览区和实际三角形 viewport 尺寸。使用 SDL3 主循环回调，拖动窗口边缘调整大小时也会持续更新渲染和信息。
- `Open files...`、`Folder...`、文件拖入、⌘O 打开文件、⌘R / `Reload` 重新加载。
- 实际 GL renderer、SDL / ImGui 版本、FPS 显示。

界面使用英文，macOS 上合并系统 Arial Unicode 字体显示资源名称；输入法尚未完整验证。渲染器已接入 Region、Mesh、裁剪附件，以及 Normal / Additive / Multiply / Screen 混合。双颜色 tint 尚未接入。

## 打开自己的资源

- 支持 Spine 3.8 导出的 `.json`、`.skel`，以及 `.json.txt`、`.skel.bytes` 包装文件名；`.spine` 工程需要先导出。
- 可以拖入骨骼文件、Atlas、纹理图片、文件夹，或同时选择多个文件。文件夹扫描最多三层、10,000 个目录项；有多个骨骼或无法唯一确定 Atlas 时弹出选择窗口，不会直接加载第一个候选。
- 优先匹配同名 Atlas，再尝试去掉 `-pro`、`-ess`、`_pro`、`_ess`、`-skeleton` 等后缀，例如 `coin-pro.skel` 可自动匹配 `coin.atlas`。只有一个 Atlas 时也可匹配不同名称；多个候选可手动指定。
- 纹理按 Atlas 中每一页的路径加载，支持多个图片、相对图片子目录、Windows 路径分隔符和唯一的 ASCII 大小写回退。图片名无需与骨骼或 Atlas 同名。只有图片输入时会尝试找出引用该图片的 Atlas。
- 在 `Resource details...` 或资源选择窗口可以修改骨骼/Atlas 路径、查看每个纹理页，用 `Browse textures...` 添加图片并逐页覆盖。纹理被改名、移到其他目录时不需要修改原始 Atlas。编辑 Atlas 路径后可点 `Read texture pages` 刷新页面。
- `Texture alpha` 支持自动识别 Atlas 的 `pma` 或 `-pma` 名称，也可以强制 Straight / Premultiplied。纹理解码后统一转换为 straight alpha。原始 3.8 Atlas 和较新的 `bounds` / `offsets` 写法都会规范化为 3.8 可读格式。
- 无动画的资源显示 setup pose。缺失图片、Atlas 尺寸不符、缺失 region、错误版本、空文件和解析失败都会显示原因，并保留上一个成功加载的动画。
- 损坏的旧格式二进制由独立无窗口进程校验，15 秒超时后取消，避免旧 runtime 解析失败导致预览器退出。校验期间动画继续播放；文件在校验中被修改会要求重新加载。

当前纹理由 macOS ImageIO 解码，优先使用 PNG；不支持的压缩纹理会明确报错。单个骨骼上限 64 MiB、Atlas 8 MiB、64 个纹理页；单页解码上限 128 MiB、总计 512 MiB，且需满足 GPU 纹理尺寸限制。3.8 runtime 原生拒绝的 3.8.75 导出版本会提示重新导出。

## Spine 3.8 runtime

从本机 `~/Documents/repos/spine-runtimes` 的 `origin/3.8` 提取 spine-cpp 和官方 Spineboy 导出示例，固定提交为 `8b4844bd4b193ba9e54487ed397a777993cbad56`。源码随仓库保存在 `Vendor/Spine38/`，静态编译进应用，无需外部仓库或网络；导入使用 `git archive`，不切换或修改外部仓库的检出状态。

应用包和 Bundle ID 分别为 `SpinePreview3.8.app`、`local.spinepreview.v38`，为后续其他 runtime 版本并存留出空间。示例导出版本为 3.8.55，包含 11 个动画。CMake 的 `SPINE_VERSION` 当前只接受 `3.8`。

PNG 纹理解码暂由 macOS ImageIO 完成，转换为 ImGui shader 使用的 straight-alpha RGBA；几何图形通过 ImGui draw list 提交，实际绘制仍使用 ANGLE。Spine runtime 的原始许可证随源码及应用保留，授权条款见 `Vendor/Spine38/LICENSE`。

## 验证

在图形登录会话中运行：

```sh
"build/SpinePreview3.8.app/Contents/MacOS/SpinePreview3.8" --smoke-test
```

自检创建真实 SDL 窗口，检查 Metal renderer、着色器编译和链接、三角形/背景/ImGui 面板像素值及 UI 顶点数据，并调整窗口大小后再次检查。然后切换到 Spineboy，检查纹理几何的可见像素及不同动画时间的帧缓冲变化。成功输出 `PASS` 并退出；错误返回非零退出码。

文件加载回归测试：

```sh
cmake -S . -B build/cmake -DSPINE_BUILD_TESTS=ON
cmake --build build/cmake --parallel 8
ctest --test-dir build/cmake --output-on-failure
```

覆盖 21 个场景，包括二进制与 JSON、多页纹理、资源改名、手动映射、中文路径、大小写、无动画、PMA、格式错误和损坏二进制隔离。

检查直接链接依赖：

```sh
otool -L "build/SpinePreview3.8.app/Contents/MacOS/SpinePreview3.8"
otool -L "build/SpinePreview3.8.app/Contents/Frameworks/"*.dylib
```

## 文件

- `Sources/main.cpp`：SDL3 窗口和事件、EGL 初始化、GLSL ES 着色器、VBO、ImGui 面板及渲染循环。
- `Sources/SpinePreview.h`、`Sources/SpinePreview.cpp`：Spine 3.8 runtime 实例、示例加载、纹理解码和骨骼几何绘制。
- `Sources/ResourceFiles.*`：资源发现、Atlas 配对、版本及图片校验、Atlas 格式规范化。
- `Sources/FileLoader.*`：原生文件选择、配对界面、校验进程和事务式加载。
- `tests/resource_files_tests.cpp`：文件加载边界回归测试。
- `CMakeLists.txt`：从本地 Vendor 源码构建、裁剪 SDL 子系统、构建及打包依赖许可证。
- `Sources/Info.plist`：应用包元数据。
- `scripts/build.sh`：通用二进制编译、动态库嵌入、本地签名。
- `Vendor/ANGLE/`：用户提供压缩包的原始头文件、动态库和 commit 标识；库最低 macOS 版本为 12.0。
- `Vendor/SDL3/`、`Vendor/imgui/`：随仓库保存的固定版本源码，保留上游许可证。
- `Vendor/Spine38/`：3.8 分支的 spine-cpp、官方 Spineboy 示例和原始许可证。

ANGLE commit：`5a62a37561da7bd0a859cc54d6a0eb05672be458`。

SDL3、Dear ImGui 和 Spine 的许可证随应用放在 `Contents/Resources/Licenses/`。
