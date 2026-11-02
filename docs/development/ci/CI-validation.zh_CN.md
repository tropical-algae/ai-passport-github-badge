<p align="right"><strong>简体中文</strong> · <a href="CI-validation.md">English</a></p>

# 验证工作流

本应用只保留一个手动构建发布工作流。Pull Request、push 和 tag 都不会自动执行检查。
触发方式、版本规则、权限与产物见权威的[手动发布指南](CI-build-and-release.zh_CN.md)。

本地仍可执行 `./tools/validate.sh --static`、`./tools/validate.sh --firmware`，
或完整 `./tools/validate.sh`。固件验证需要 ESP-IDF 5.5.3、CMake 和 Ninja，
也会运行原生 LVGL 渲染检查。不要将已经删除的自动工作流名称配置为分支保护的必需检查。
