<p align="right"><strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a></p>

# 手动构建与发布铭牌固件

本应用只有一个工作流：[release.yml](../../../.github/workflows/release.yml)。
打开 **Actions → Build and publish badge firmware → Run workflow**，选择仓库当前默认分支，
可选填写版本号。此次手动操作会构建并发布 GitHub Release。
Push、Pull Request、tag 和定时任务均不触发工作流，也不会自动同步上游。

## 版本选择

版本留空时自动计算。尚无语义版本标签时，首次版本来自 `VERSION`，目前是 `0.2.5`。
之后以当前提交可达的最高 `vX.Y.Z` 标签为基线，分析其后的提交标题和破坏性变更 footer：

| 变更 | 版本递增 |
| --- | --- |
| `feat:` 或 `feat(scope):` | 次版本 |
| `fix:` 或 `perf:` | 补丁版本 |
| `type!:` / `type(scope)!:`，或 `BREAKING CHANGE:` / `BREAKING-CHANGE:` footer | 主版本 |
| 没有上述标记的文档、测试、重构与维护提交 | 不发布 |

采用最大的递增级别；递增主版本时次版本和补丁归零，递增次版本时补丁归零。
即使当前小于 `1.0.0`，破坏性变更仍递增主版本。使用 squash merge 时，应在合并提交标题中
填写合适的 Conventional Commit 类型。发布说明列出本次功能、修复、性能及破坏性变更标题。

手动填写 `X.Y.Z`（允许前缀 `v`）会覆盖自动计算，也可以发布纯维护改动。
不接受前导零、预发布版本或后缀。指定版本必须大于所有现有语义版本标签。
重复版本别名、浅克隆，以及最高发布标签无法从当前提交追溯等情况会明确拒绝，不会移动标签。

本地构建使用 `VERSION`。CI 将选定版本通过 `BADGE_VERSION` 传入 CMake，固件中写入
`github-badge-X.Y.Z`；打包时再次核对该版本后生成文件名。
`VERSION` 是首次发布及本地构建的基线，工作流不会在每次发布后自动提交修改该文件。

## 验证与权限

只读构建任务检出手动触发时的准确提交及完整历史，先执行 `tools/validate.sh --static`，
再在 ESP-IDF 5.5.3 环境中为 ESP32-C3 执行固件验证。
固件验证包括同步采集器集成测试、隔离构建、真实 LVGL 原生渲染及头像测试（内置分配器）、
合并镜像校验及按内容哈希归档。所有 Action 均固定到完整提交 SHA。

只有独立发布任务获得 `contents: write`。它先校验发布资产的哈希，再在准确的触发提交 SHA
上原子创建 `refs/tags/vX.Y.Z`，随后通过 `gh release create --draft --verify-tag --target <sha>`
和准备好的说明创建草稿，上传已验证资产后才公开发布。不会 force-push 或自动合并，并发发布会排队。
工作流不会给设备刷写固件。

若上传或发布失败，请在 30 天内对同一次 Actions 运行选择 **Re-run failed jobs**。
构建产物保留 30 天，以支持该重试窗口。
发布任务会获取原构建尝试的不可变产物；只有现有轻量标签的 SHA 与该构建完全一致时才继续，
并恢复尚未公开的草稿。只有草稿中的资产允许替换。已发布 Release 或指向其他提交的标签
都不会被覆盖。新开一次工作流仍会拒绝重复版本，不会偷偷重发旧版本。

## 发布资产

- `github-pixel-passport-vX.Y.Z-app.bin`：已安装兼容铭牌固件时，从 `0x10000` 升级，
  保留 NVS 和布局不变的铭牌缓存。
- `github-pixel-passport-vX.Y.Z-full.bin`：首次安装从 `0x0` 写入；完整镜像可能重置
  Wi-Fi 与设置，升级前先导出备份。
- `github-pixel-passport-vX.Y.Z-manifest.json`：版本、源码 SHA、大小、哈希与烧录偏移，
  不含本地路径或调试二进制文件。
- `SHA256SUMS.txt`、`FLASHING.txt`、`release-notes.md`、`LICENSE`、`LICENSE-FoloToy`，
  以及含依赖组件和字体完整许可文字的 `THIRD-PARTY-LICENSES.txt`。

应用镜像不得写入 `0x0`。GitHub 自动提供的标签源码归档对应本次固件的准确源码。
ELF/MAP、原始日志、本地配置和账号缓存不上传为公开资产；匹配的调试文件只保留于
构建时生成的本地内容哈希归档中。

## 本地复现

```bash
./tools/validate.sh --static
source <path-to-esp-idf-v5.5.3>/export.sh
BADGE_VERSION=0.2.5 ./tools/validate.sh --firmware
python3 tools/release_badge.py plan --version 0.2.5 --output build/release-plan.json
python3 tools/release_badge.py package --plan build/release-plan.json --output build/release-assets
```

规划版本需要完整本地 Git 历史和标签。打包目标目录必须尚不存在，并要求存在与
`build/FoloToy-AI-Passport-full.bin` 匹配的已验证归档。打包会再次核对归档与应用版本。验证流程会记录源码提交、完整镜像哈希，以及构建前后源码是否干净；
打包拒绝缺失、过期或来自脏工作区的构建记录，并要求当前工作区干净。它不代替完整验证流程，也不会自行发布。

CI 分别报告 Build、Host tests 和 Device tests，后者始终为 **NOT RUN**。
实际启动、按键、网络行为和刷写仍需真机验证，不能把构建成功称为设备验证通过。
