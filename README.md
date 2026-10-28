# SpinePreview

项目起因是在 Apple Silicon Mac 上启动旧版 Spine Skeleton Viewer 时，遇到缺少 ARM64 原生库或原生库架构不匹配，无法进入预览界面。因此构建轻量的原生动画预览应用，按 Spine runtime 版本分别发布，并保留离线构建能力。脱敏后的报错与背景见 [项目初衷](docs/project-background.md)。

使用 SDL3 管理窗口和输入，Dear ImGui 绘制控制面板，通过用户提供的 Google ANGLE，以 OpenGL ES 2.0 绘制 Spine 2.1 / 3.8 / 4.0 动画和 UI。保留 RGB 三角形作为渲染测试模式。渲染路径：

`SDL3 window → SDL_MetalView / CAMetalLayer → EGL → ANGLE OpenGL ES → Metal`

ImGui 使用 `imgui_impl_sdl3` 输入后端及定义了 `IMGUI_IMPL_OPENGL_ES2` 的 `imgui_impl_opengl3` 渲染后端。SDL 不创建 OpenGL 上下文；SDL 的原生 OpenGL / OpenGL ES 后端在构建时禁用。

显式选择 `EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE`，初始化时校验 `GL_RENDERER` 包含 ANGLE 和 Metal。没有链接或调用 macOS 的 `OpenGL.framework`，也不使用 `NSOpenGLView` / CGL。ANGLE 的 Metal 窗口表面实现可参阅[上游源码](https://github.com/google/angle/blob/main/src/libANGLE/renderer/metal/SurfaceMtl.mm)。

## 编译和运行

需要 macOS 12+、Apple Command Line Tools（`xcode-select --install`）和 CMake 3.24+，无需完整 Xcode。SDL 3.4.18 和 Dear ImGui 1.93.0 的完整上游源码随项目保存在 `Vendor/SDL3/` 和 `Vendor/imgui/`，ANGLE 头文件和动态库保存在 `Vendor/ANGLE/`。工具链就绪后，首次配置及编译均可离线完成，无需下载缓存、用户 Downloads 目录、系统安装的 SDL / ImGui 或包管理器。构建不使用 FetchContent 或其他网络下载方式。依赖来源及归档校验和见 `Vendor/README.md`。

```sh
bash scripts/build.sh
open "build/SpinePreview2.1.app"
open "build/SpinePreview3.8.app"
open "build/SpinePreview4.0.app"
```

默认同时生成 `SpinePreview2.1.app` 、`SpinePreview3.8.app` 和 `SpinePreview4.0.app`，两者具有独立 Bundle ID，可同时运行。产物为 arm64 / x86_64 Universal `.app`，内置两份 ANGLE 动态库，采用本地 ad-hoc 签名。可直接在本机双击运行；面向其他用户正式分发时需要 Developer ID 签名和公证。

使用 `SPINE_VERSION=2.1 bash scripts/build.sh` 或 `SPINE_VERSION=3.8 bash scripts/build.sh` 可只构建对应版本。默认同时构建 arm64 / x86_64；可用 `BUILD_JOBS=4 bash scripts/build.sh` 调整并行编译数。目前交付及验证的是 macOS 版本，Windows/Linux 平台接入尚未实现，CMake 会明确提示。

默认窗口内容区为 `1136×640`，参考 iPhone 5s 横屏像素分辨率；这里用作 macOS 窗口的逻辑尺寸，Retina 帧缓冲按显示器像素比例扩展。支持 Retina 和窗口缩放；三角形保持比例。关闭窗口或按 ⌘Q 退出，退出时按顺序释放 ImGui、GL / EGL、Metal view 和 SDL 资源。控制面板支持：

- 点击面板标题左侧箭头或双击标题栏折叠/展开；拖动面板右下角调整宽度。展开时动画预览区位于面板右侧，折叠时扩展至标题栏下方，自动重新适配位置和比例，不与面板重叠。
- 默认播放官方 Spineboy 示例，可切换动画和皮肤，播放/暂停、循环、调整速度和重新播放。
- `Triangle test` 可切换回三角形渲染测试；支持缩放、背景颜色调整、一键重置。
- 在动画预览区按住鼠标左键拖动可平移动画，播放与暂停时都可操作；拖动不会改变骨骼数据。切换动画、皮肤或重新加载同一资源保留位置，加载新资源或点击 `Reset` 恢复居中。
- `Show render info` 开关：显示窗口逻辑尺寸、帧缓冲像素尺寸、像素比例、预览区和实际三角形 viewport 尺寸。使用 SDL3 主循环回调，拖动窗口边缘调整大小时也会持续更新渲染和信息。
- `Open files...`、`Folder...`、文件拖入、⌘O 打开文件、⌘R / `Reload` 重新加载。
- ⌘N / macOS `File → New Instance` / `New instance (Cmd+N)` 启动同一 runtime 版本的独立实例，继承原实例当前窗口宽高，默认打开示例，在原窗口 `(x+50, y+50)` 处显示，首帧就绪后自动置前并获得焦点，可分别加载不同资源；靠近屏幕边缘时调整到可见范围，右下角无法继续偏移时向左上错开。关闭原实例不会关闭新实例。快捷键由原生菜单处理，新实例通过 macOS LaunchServices 启动，异步启动失败会在原实例显示原因。
- 主面板的 `Premultiplied alpha (PMA)` 开关：开启按预乘纹理解码，关闭按 straight alpha 解码，切换后重新加载当前纹理，保留动画、皮肤、播放时间、暂停状态及视图缩放。失败时保留原预览。自动识别模式仍可在 `Resource details...` 中选择；自动模式下多页混合使用不同 alpha 时会显示说明。
- 实际 GL renderer、SDL / ImGui 版本、FPS 显示。

界面使用英文，macOS 上合并系统 Arial Unicode 字体显示资源名称；输入法尚未完整验证。渲染器已接入 Region、Mesh、裁剪附件，以及 Normal / Additive / Multiply / Screen 混合。双颜色 tint 尚未接入。2.1 根据其 runtime 能力支持 Region、Mesh、SkinnedMesh，以及 Normal / Additive 混合。

## 打开自己的资源

- 3.8 应用支持对应版本导出的 `.json`、`.skel`，以及 `.json.txt`、`.skel.bytes` 包装文件名；2.1 应用支持 2.1 JSON（含 `.json.txt`），其 C runtime 没有二进制读取器，打开 `.skel` 会明确报错。`.spine` 工程需要先导出。
- 导出版本与 runtime 不一致时继续尝试加载，在面板显示提示。`Dev` 和无版本 JSON 也继续尝试，原样显示标记或 `unknown (legacy)`；具体兼容性由隔离进程中的 runtime 解析器判断。解析失败保留原预览。
- 可以拖入骨骼文件、Atlas、纹理图片、文件夹，或同时选择多个文件。文件夹扫描最多三层、10,000 个目录项；有多个骨骼或无法唯一确定 Atlas 时弹出选择窗口，不会直接加载第一个候选。
- 优先匹配同名 Atlas，再尝试去掉 `-pro`、`-ess`、`_pro`、`_ess`、`-skeleton` 等后缀，例如 `coin-pro.skel` 可自动匹配 `coin.atlas`。只有一个 Atlas 时也可匹配不同名称；多个候选可手动指定。
- 纹理按 Atlas 中每一页的路径加载，支持多个图片、相对图片子目录、Windows 路径分隔符和唯一的 ASCII 大小写回退。图片名无需与骨骼或 Atlas 同名。只有图片输入时会尝试找出引用该图片的 Atlas。
- 在 `Resource details...` 或资源选择窗口可以修改骨骼/Atlas 路径、查看每个纹理页，用 `Browse textures...` 添加图片并逐页覆盖。纹理被改名、移到其他目录时不需要修改原始 Atlas。编辑 Atlas 路径后可点 `Read texture pages` 刷新页面。
- `Texture alpha` 支持自动识别 Atlas 的 `pma` 或 `-pma` 名称，也可以强制 Straight / Premultiplied。纹理解码后统一转换为 straight alpha。原始 3.8 Atlas 和较新的 `bounds` / `offsets` 写法都会规范化为当前 runtime 可读格式；2.1 的旋转属性输出为 `true/false`，页尺寸使用图片实际像素（与旧官方加载器一致），并按实际尺寸检查 region 越界。
- 无动画的资源显示 setup pose。缺失图片、Atlas 尺寸不符、缺失 region、空文件和解析失败都会显示原因，并保留上一个成功加载的动画。
- 损坏的旧格式二进制由独立无窗口进程校验，15 秒超时后取消，避免旧 runtime 解析失败导致预览器退出。校验期间动画继续播放；文件在校验中被修改会要求重新加载。

当前纹理由 macOS ImageIO 解码，优先使用 PNG；不支持的压缩纹理会明确报错。单个骨骼上限 64 MiB、Atlas 8 MiB、64 个纹理页；单页解码上限 128 MiB、总计 512 MiB，且需满足 GPU 纹理尺寸限制。3.8.75 等由 runtime 原生拒绝的数据仍会报告解析错误，应用不预先按版本号拒绝。

## Spine 3.8 runtime

从本机 `~/Documents/repos/spine-runtimes` 的 `origin/3.8` 提取 spine-cpp 和官方 Spineboy 导出示例，固定提交为 `8b4844bd4b193ba9e54487ed397a777993cbad56`。源码随仓库保存在 `Vendor/Spine38/`，静态编译进应用，无需外部仓库或网络；导入使用 `git archive`，不切换或修改外部仓库的检出状态。

应用包和 Bundle ID 分别为 `SpinePreview3.8.app`、`local.spinepreview.v38`，与 2.1 应用分别命名，可并存安装。示例导出版本为 3.8.55，包含 11 个动画。CMake 的 `SPINE_VERSION` 接受 `all`（默认）、`2.1`、`3.8`、`4.0`。

PNG 纹理解码暂由 macOS ImageIO 完成，转换为 ImGui shader 使用的 straight-alpha RGBA；几何图形通过 ImGui draw list 提交，实际绘制仍使用 ANGLE。Spine runtime 的原始许可证随源码及应用保留，授权条款见 `Vendor/Spine38/LICENSE`。

## 多版本共用代码

Spine 2.1.25 的 C runtime 离线保存在 `Vendor/Spine21/`，固定提交 `142e770e5b4b42395a3a0a68ff3c8a21ba2774eb`，应用内置该版本的官方 Spineboy 示例（8 个动画）。

三个应用共用 `main.cpp`、`FileLoader.*`、`ResourceFiles.*` 和纹理加载器。`SpinePreview.h` 定义统一接口，构建时选择 `Sources/Runtimes/Spine21.cpp` 或 `Spine38.cpp`；每个应用只链接自己的 runtime，分别打包对应示例与许可证。多版本维护及接入差异见 [runtime 版本方案](docs/runtime-versions.md)。

## 应用图标

各 runtime 应用共用骨骼关节与播放符号图标。资源保存在 `Assets/AppIcon/`：原始生成图、1024×1024 PNG、完整尺寸的 macOS ICNS 和生成提示词。构建直接使用仓库中的 ICNS；更换原图后可运行 `bash scripts/build-icon.sh` 通过 macOS 系统工具重新生成，无需在线生成服务。

## GitHub 自动发布

推送 `v1.0.0` 这类版本 tag 后，GitHub Actions 自动构建并发布 `SpinePreview2.1`、`SpinePreview3.8` 的 macOS Universal ZIP，以及包含离线依赖的 ZIP / TAR.GZ 源码包和 SHA-256 校验文件。预发布 tag（如 `v1.1.0-rc.1`）生成 Prerelease。详细设置、发布步骤与失败重试见 [发布文档](docs/releasing.md)。

## Spine 4.0 runtime

离线源码位于 `Vendor/Spine40/`，固定提交 `425ce416bb218b28caeec47b317aa57cd7140375`。`Sources/Runtimes/Spine40.cpp` 使用独立 runtime 实现统一预览接口，支持 JSON、二进制、Region/Mesh 和裁剪，沿用现有混合、PMA、拖动、动画/皮肤切换和多实例功能；双颜色 tint 仍未接入。

默认构建额外生成 `SpinePreview4.0.app`，也可使用 `SPINE_VERSION=4.0 bash scripts/build.sh` 单独构建。发布流程会同时打包 2.1、3.8、4.0。4.0 的二进制使用 8 字节 hash 头，与 3.8 分开处理。新增资源回归测试验证官方 Essential/Professional 的 JSON、二进制、包装文件名、PMA、纹理缺失和损坏二进制隔离。

## 验证

在图形登录会话中运行：

```sh
"build/SpinePreview2.1.app/Contents/MacOS/SpinePreview2.1" --smoke-test
"build/SpinePreview3.8.app/Contents/MacOS/SpinePreview3.8" --smoke-test
```

可用 `--smoke-test /完整路径/raptor.json /完整路径/raptor.atlas` 对指定资源进行图形自检。

自检创建真实 SDL 窗口，检查 Metal renderer、着色器编译和链接、三角形/背景/ImGui 面板像素值及 UI 顶点数据，并调整窗口大小后再次检查。然后切换到 Spineboy，检查纹理几何的可见像素及不同动画时间的帧缓冲变化。成功输出 `PASS` 并退出；错误返回非零退出码。

文件加载回归测试：

```sh
cmake -S . -B build/cmake -DSPINE_VERSION=all -DSPINE_BUILD_TESTS=ON
cmake --build build/cmake --parallel 8
ctest --test-dir build/cmake --output-on-failure
```

3.8 覆盖 21 个场景，包括二进制与 JSON、多页纹理、资源改名、手动映射、中文路径、大小写、无动画、PMA、版本不匹配继续解析、格式错误和损坏二进制隔离。2.1 另外验证无版本旧 JSON、官方 Dev Raptor、2.1.15、缺少 hash 元数据、版本提示、二进制拒绝、旋转、多页/PMA、静态骨骼、普通/加权 Mesh 和异常隔离。

检查直接链接依赖：

```sh
otool -L "build/SpinePreview3.8.app/Contents/MacOS/SpinePreview3.8"
otool -L "build/SpinePreview3.8.app/Contents/Frameworks/"*.dylib
```

## 文件

- `Sources/main.cpp`：SDL3 窗口和事件、EGL 初始化、GLSL ES 着色器、VBO、ImGui 面板及渲染循环。
- `Sources/SpinePreview.h`：共用 runtime 适配接口。
- `Sources/Runtimes/Spine21.cpp`、`Spine38.cpp`：各版本实例、动画控制、骨骼几何绘制。
- `Sources/AppProcess.*`：共用可执行文件定位与独立实例启动。
- `Sources/PreviewTexture.*`：共用 ImageIO 解码、PMA 转换和 ANGLE 纹理上传。
- `Sources/ResourceFiles.*`：资源发现、Atlas 配对、版本提示及图片校验、Atlas 格式规范化。
- `Sources/FileLoader.*`：原生文件选择、配对界面、校验进程和事务式加载。
- `tests/resource_files_tests.cpp`、`tests/runtime21_tests.cpp`：各版本文件加载和解析回归测试。
- `CMakeLists.txt`：从本地 Vendor 源码构建、裁剪 SDL 子系统、构建及打包依赖许可证。
- `Sources/Info.plist`：应用包元数据。
- `scripts/build.sh`：通用二进制编译、动态库嵌入、本地签名。
- `Vendor/ANGLE/`：用户提供压缩包的原始头文件、动态库和 commit 标识；库最低 macOS 版本为 12.0。
- `Vendor/SDL3/`、`Vendor/imgui/`：随仓库保存的固定版本源码，保留上游许可证。
- `Vendor/Spine21/`：2.1.25 标签的 spine-c、官方 Spineboy 示例和原始许可证。
- `Vendor/Spine38/`：3.8 分支的 spine-cpp、官方 Spineboy 示例和原始许可证。

ANGLE commit：`5a62a37561da7bd0a859cc54d6a0eb05672be458`。

SDL3、Dear ImGui 和 Spine 的许可证随应用放在 `Contents/Resources/Licenses/`。
