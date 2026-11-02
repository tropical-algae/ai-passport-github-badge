<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。


### GitHub 铭牌字体

`fonts/github-badge/` 保存 Silkscreen Regular、Noto Sans CJK SC Regular、
各自的 SIL Open Font License 1.1 许可文件，以及五份生成的 LVGL 点阵字体。
Silkscreen 提供 8、10、14、22 像素的可打印 ASCII；10 像素 Noto 提供所生成范围
内的中日韩文字、拉丁字母、希腊字母、西里尔字母及标点回退。范围外字符（包括
emoji）保留可见的缺字方框。源地址、SHA-256 和 Unicode 范围见
[`sources.json`](fonts/github-badge/sources.json)。使用 1.5.3 版转换器运行
`python3 tools/generate_badge_fonts.py --converter /path/to/lv_font_conv`
可重新生成。`main/CMakeLists.txt` 将 1 位未压缩字形编入 Flash。
本机 C 渲染测试检查中文覆盖与混合文本；实际屏幕效果仍需真机验证。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/github-badge-pages.png`](images/github-badge-pages.png) | 1080 × 712，PNG RGB | 使用合成数据的七页原生 C/LVGL 铭牌视图，为中英文项目 README 拼合。由本项目 UI 生成，按项目许可证分发；这是软件预览，不是硬件实拍。 |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 上游 AI Passport 产品图，保留在 `docs/README.md` 及对应中文版的硬件平台文档中。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从上游仓库原始 `images/logo.png` 裁切并去除背景的黑色字标；保留在平台文档中用于浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；平台文档通过 `<picture>` 在深色主题下显示。 |

激活构建环境后，在仓库根目录重新生成铭牌拼图：

```bash
./tools/render_badge_ui.sh build/github-ui-preview
python3 tools/compose_badge_preview.py build/github-ui-preview/avatar assets/images/github-badge-pages.png
```

合成数据与生成头像来自 `tests/ui_render/render.c` 和
`tests/ui_render/fixtures.py`。拼图脚本仅使用 Python 标准库，将每个 240 × 320
页面按原始像素尺寸排列。图片不含下载的个人账号数据、真实头像或配网二维码秘密。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。

## 参考设计

[`reference/github_pixel_240x320_v20.html`](reference/github_pixel_240x320_v20.html)
是用户提供的个人 GitHub 铭牌 HTML 参考稿，保留原样，不嵌入固件。来源与 SHA-256
记录在[应用说明](../docs/development/github-badge.zh_CN.md)中；用户未随参考稿提供
单独的再分发许可证。
