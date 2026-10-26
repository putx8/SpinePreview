# GitHub 自动发布

`.github/workflows/release.yml` 在推送 `v*` tag 时启动，实际发布 tag 必须是 `vMAJOR.MINOR.PATCH` 或带预发布后缀的形式，例如 `v1.0.0`、`v1.1.0-rc.1`。带后缀的版本标为 GitHub Prerelease，不标为 Latest。

## 首次设置

1. 把包含 workflow 和发布脚本的提交推送到 GitHub 仓库，并配置本地 `origin`。tag 必须指向包含这些文件的提交。
2. 确保仓库启用了 GitHub Actions，并允许该 workflow 使用 `contents: write` 发布 Release。使用 GitHub 自动提供的 `GITHUB_TOKEN`，无需个人访问令牌。
3. 确认 `Vendor/` 中的依赖、二进制库和许可证均已提交。构建不下载 SDL、ImGui、Spine 或 ANGLE。

例如发布 `v1.0.0`（版本号仅为示例，应选择尚未发布的版本）：

```sh
git push origin main
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

创建本地 tag 不会触发 GitHub，推送 tag 后才会运行。若自动触发未启动，或需要对已有 tag 重跑，可在 Actions → Release → Run workflow 输入已有 tag，也可以运行 `gh workflow run release.yml --ref main -f tag=v0.0.1`。手动入口仍检出该 tag 的源码，不改变 tag 指向。源码包来自该 tag 对应提交，不包含本地未提交文件、Git 元数据或构建目录。

## 发布附件

以 `v1.0.0` 为例，一个 Release 提供：

| 文件 | 内容 |
| --- | --- |
| `SpinePreview2.1-v1.0.0-macos-universal.zip` | `SpinePreview2.1.app`，Spine 2.1.25 C runtime，仅支持 JSON |
| `SpinePreview3.8-v1.0.0-macos-universal.zip` | `SpinePreview3.8.app`，Spine 3.8 runtime，支持 JSON / 二进制 |
| `SpinePreview-v1.0.0-source.zip` | 完整项目源码、离线依赖和许可证 |
| `SpinePreview-v1.0.0-source.tar.gz` | 同一提交的 TAR.GZ 源码包 |
| `SHA256SUMS` | 上述四个压缩包的 SHA-256 校验值 |

两个应用均为 ARM64 / x86_64 Universal，最低 macOS 12，使用 ANGLE Metal。ZIP 保留 `.app` 包结构和文件权限，解压后可得到独立应用。当前采用 ad-hoc 签名，尚未配置 Developer ID 签名和 Apple 公证。

应用发布版本与 Spine runtime 版本独立：例如 `v1.2.3-rc.1` 发布的两个应用仍分别叫 `SpinePreview2.1.app`、`SpinePreview3.8.app`。`CFBundleShortVersionString` 为 `1.2.3`，完整版本写入 `SpinePreviewReleaseVersion`，`CFBundleVersion` 使用 GitHub Actions 的运行序号。

## 自动流程

1. 在固定 `macos-15` runner 上检出 tag 提交，验证 tag 和发布脚本。
2. 使用仓库内依赖构建两个 Universal 应用，分别附带对应 runtime 示例与许可证，并执行本地 ad-hoc 签名。
3. 运行两个 runtime 的资源回归测试。打包时再次验证应用签名，以及可执行文件和 ANGLE 库都包含 ARM64 / x86_64。
4. 使用 `ditto` 打包应用，使用 `git archive` 导出源码，生成校验文件、内部 manifest 和发布说明。
5. 把完整发布包作为 Actions artifact 保存 14 天；在独立发布 job 校验附件完整性后创建 Release 草稿，上传全部附件，最后发布。

应用构建 job 仅有读取权限，发布 job 有 `contents: write`。Actions 固定到具体提交。发布同一 tag 的 workflow 串行执行。

任何构建、测试或打包失败都会阻止发布。附件上传失败会留下草稿；在 Actions 中重跑可续传。已经公开发布的同名 Release 不会被重跑覆盖，应使用新 tag 发布修复版本。

CI 不依赖图形桌面或 Metal GPU 运行图形自检；它运行无窗口资源校验。真实窗口渲染与拖动仍可在本机使用 `--smoke-test` 验证。

## 本地验证

发布工具只依赖 Python 3 标准库、Git 及 macOS 自带的 `ditto` / `codesign` / `lipo`。发布时另使用 GitHub CLI。

```sh
python3 -B -m unittest discover -s tests -p release_tests.py -v
bash -n scripts/build.sh scripts/publish-release.sh
```

本地试打包已有提交（不会创建 tag 或发布到 GitHub）：

```sh
SPINE_RELEASE_VERSION=1.0.0 SPINE_BUILD_NUMBER=1 bash scripts/build.sh
python3 scripts/release.py package --tag v1.0.0 --revision HEAD --output build/release-local
python3 scripts/release.py verify --tag v1.0.0 --output build/release-local
```

输出目录需要为空，避免混入旧版本附件。`--revision HEAD` 只用于从当前提交试打包；正式 workflow 使用 tag 对应的提交 SHA。未提交的源码改动不会进入源码压缩包。

下载后的校验方法：在四个压缩包和 `SHA256SUMS` 所在目录运行 `shasum -a 256 -c SHA256SUMS`。

## 参考

- [GitHub：push 事件与 tag 触发](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#push)
- [GitHub CLI：创建 Release 与草稿](https://cli.github.com/manual/gh_release_create)
- [GitHub CLI：发布已有草稿](https://cli.github.com/manual/gh_release_edit)
- [GitHub 官方 runner 镜像与标签](https://github.com/actions/runner-images)
