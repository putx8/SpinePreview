# Bundled dependencies

所有依赖随仓库保存；构建只读取本地文件，不访问上游服务器。下面的链接仅记录来源，不用于构建。

| 依赖 | 版本 | 本地位置 | 来源 |
| --- | --- | --- | --- |
| SDL3 | 3.4.18 | `SDL3/` | https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18 |
| Dear ImGui | 1.93.0 | `imgui/` | https://github.com/ocornut/imgui/releases/tag/v1.93.0 |
| ANGLE | `5a62a37561da7bd0a859cc54d6a0eb05672be458` | `ANGLE/` | 用户提供的 `angle-mac-universal.zip` |
| Spine C++ | 3.8 / `8b4844bd4b193ba9e54487ed397a777993cbad56` | `Spine38/` | 本机 `~/Documents/repos/spine-runtimes` 的 `origin/3.8` |

SDL3 和 Dear ImGui 保留完整上游源码及许可证，不依赖 Git 子模块、Git LFS 或构建时下载。上游链接或 tag 删除不影响构建。后续升级需显式替换本地源码并更新此记录。

导入时仅调整了两处上游 `.gitignore`，确保 SDL 的 HLSL 源码和 ImGui Android 示例的 `libs.versions.toml` 也能被 Git 跟踪；未修改依赖实现。

导入时使用的上游归档 SHA-256（用于追溯来源，无需这些归档即可构建）：

```text
SDL3-3.4.18.tar.gz
9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3

imgui v1.93.0.tar.gz
f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
```

许可证：`SDL3/LICENSE.txt`（zlib）、`imgui/LICENSE.txt`（MIT）。构建时也会复制到应用内的 `Contents/Resources/Licenses/`。

Spine 从指定提交原样提取 `spine-cpp/`、`examples/spineboy/export/`、根目录 `LICENSE` 和 `README.md`，另附 `Spine38/commit.txt` 记录来源。保留 Spine Runtimes License，构建时复制到应用内 `Contents/Resources/Licenses/Spine.txt`。
