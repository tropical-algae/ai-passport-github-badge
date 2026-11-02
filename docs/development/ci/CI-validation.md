<p align="right"><a href="CI-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Validation workflow

This application uses a single manual build-and-release workflow. Pull requests,
pushes and tags do not run automatic checks. See the authoritative
[manual release guide](CI-build-and-release.md) for triggering, version rules,
permissions and artifacts.

Local checks remain available through `./tools/validate.sh --static`,
`./tools/validate.sh --firmware`, or the complete `./tools/validate.sh` gate.
The firmware gate needs ESP-IDF 5.5.3, CMake and Ninja; it also runs native LVGL
rendering checks. Do not configure retired automatic workflow names as required
branch-protection status checks.
