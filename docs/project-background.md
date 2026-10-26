# 项目初衷

https://en.esotericsoftware.com/spine-skeleton-viewer#Download

在 Apple Silicon Mac 上运行旧版 Spine Skeleton Viewer 时，Java 应用依赖的 libGDX 原生动态库无法加载，预览器在启动阶段退出。这是创建 SpinePreview 的直接原因。

下面记录实际遇到的两个版本的报错。命令中的下载目录统一替换为 `<DOWNLOADS>`；系统临时目录、用户名相关缓存目录及缓存标识统一替换为 `<TEMP_DIR>/libgdx/<CACHE_ID>`。保留动态库文件名、目标架构及关键调用链，省略重复堆栈。

## Skeleton Viewer 2.1.27

```text
java -jar "<DOWNLOADS>/skeletonViewer-2.1.27.jar"

Exception in thread "main" com.badlogic.gdx.utils.SharedLibraryLoadRuntimeException:
Couldn't load shared library 'libgdxarm64.dylib' for target: Mac OS X, 64-bit
    at com.badlogic.gdx.utils.SharedLibraryLoader.load(SharedLibraryLoader.java:128)
    at com.badlogic.gdx.utils.GdxNativesLoader.load(GdxNativesLoader.java:30)
    at com.badlogic.gdx.backends.lwjgl.LwjglNativesLoader.load(LwjglNativesLoader.java:46)
    at com.badlogic.gdx.backends.lwjgl.LwjglApplication.<init>(LwjglApplication.java:84)
    at com.badlogic.gdx.backends.lwjgl.LwjglApplication.<init>(LwjglApplication.java:72)
    at com.esotericsoftware.spine.SkeletonViewer.main(SkeletonViewer.java:522)
Caused by: com.badlogic.gdx.utils.SharedLibraryLoadRuntimeException:
Unable to read file for extraction: libgdxarm64.dylib
    at com.badlogic.gdx.utils.SharedLibraryLoader.readFile(SharedLibraryLoader.java:136)
    at com.badlogic.gdx.utils.SharedLibraryLoader.loadFile(SharedLibraryLoader.java:292)
    at com.badlogic.gdx.utils.SharedLibraryLoader.load(SharedLibraryLoader.java:124)
    ... 5 more
```

这次运行尝试加载 `libgdxarm64.dylib`，但无法从应用资源中读取并提取该文件。

## Skeleton Viewer 3.8.99

```text
java -jar "<DOWNLOADS>/skeletonViewer-3.8.99.jar"

Exception in thread "main" com.badlogic.gdx.utils.GdxRuntimeException:
Couldn't load shared library 'libgdx64.dylib' for target: Mac OS X, 64-bit
    at com.badlogic.gdx.utils.SharedLibraryLoader.load(SharedLibraryLoader.java:126)
    at com.badlogic.gdx.utils.GdxNativesLoader.load(GdxNativesLoader.java:31)
    at com.badlogic.gdx.backends.lwjgl.LwjglNativesLoader.load(LwjglNativesLoader.java:46)
    at com.badlogic.gdx.backends.lwjgl.LwjglApplication.<init>(LwjglApplication.java:83)
    at com.badlogic.gdx.backends.lwjgl.LwjglApplication.<init>(LwjglApplication.java:71)
    at com.esotericsoftware.spine.SkeletonViewer.main(SkeletonViewer.java:1103)
Caused by: com.badlogic.gdx.utils.GdxRuntimeException: java.lang.UnsatisfiedLinkError:
<TEMP_DIR>/libgdx/<CACHE_ID>/libgdx64.dylib:
dlopen(<TEMP_DIR>/libgdx/<CACHE_ID>/libgdx64.dylib, 0x0001):
mach-o file, but is an incompatible architecture
(have 'x86_64', need 'arm64e' or 'arm64e.v1' or 'arm64' or 'arm64')
    at com.badlogic.gdx.utils.SharedLibraryLoader.loadFile(SharedLibraryLoader.java:322)
    at com.badlogic.gdx.utils.SharedLibraryLoader.load(SharedLibraryLoader.java:122)
    ... 5 more
Caused by: java.lang.UnsatisfiedLinkError:
<TEMP_DIR>/libgdx/<CACHE_ID>/libgdx64.dylib:
mach-o file, but is an incompatible architecture
(have 'x86_64', need 'arm64e' or 'arm64e.v1' or 'arm64' or 'arm64')
    at java.base/jdk.internal.loader.NativeLibraries.load(Native Method)
    at java.base/jdk.internal.loader.NativeLibraries$NativeLibraryImpl.open(NativeLibraries.java:388)
    at java.base/jdk.internal.loader.NativeLibraries.loadLibrary(NativeLibraries.java:232)
    at java.base/jdk.internal.loader.NativeLibraries.loadLibrary(NativeLibraries.java:174)
    at java.base/java.lang.ClassLoader.loadLibrary(ClassLoader.java:2394)
    at java.base/java.lang.Runtime.load0(Runtime.java:755)
    at java.base/java.lang.System.load(System.java:1970)
    at com.badlogic.gdx.utils.SharedLibraryLoader.loadFile(SharedLibraryLoader.java:328)
    at com.badlogic.gdx.utils.SharedLibraryLoader.loadFile(SharedLibraryLoader.java:297)
    ... 6 more
```

这次运行已提取 `libgdx64.dylib`，但动态库只有 `x86_64` 架构，当前进程需要 ARM64 架构，因此 `dlopen` 失败。

两次失败都发生在原生库加载阶段；这些日志本身无法判断具体动画资源是否兼容。

## SpinePreview 的目标

- 在 Apple Silicon Mac 上原生预览旧版 Spine 导出资源，同时构建包含 ARM64 / x86_64 的 Universal 应用。
- 针对不同 Spine runtime 分别生成应用，例如 `SpinePreview2.1.app`、`SpinePreview3.8.app`；界面和文件管理共用代码，各版本保留独立适配层。
- 使用 SDL3、Dear ImGui 和 ANGLE，提供轻量的窗口、交互和渲染基础；macOS 上由 ANGLE 转到 Metal，不依赖系统 OpenGL 框架。
- 依赖源码及所需库随项目保存，确保上游下载链接或版本变化后仍能离线构建。
- 支持拖入资源、多个纹理页、动画与皮肤切换、PMA、拖动预览及多实例，方便日常检查旧资源。

当前首先交付 macOS 应用；跨平台接入沿用这套共用结构。SpinePreview 的支持范围取决于所集成的 runtime，现阶段 2.1 的 C runtime 仅支持 JSON，尚不支持 `.skel` 二进制读取。
