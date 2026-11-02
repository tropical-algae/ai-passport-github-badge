<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.


### GitHub badge fonts

`fonts/github-badge/` contains Silkscreen Regular, Noto Sans CJK SC Regular,
their SIL Open Font License 1.1 texts, and five generated LVGL bitmap fonts.
Silkscreen supplies printable ASCII at 8, 10, 14 and 22 px. Noto at 10 px
supplies the generated CJK/Latin/Greek/Cyrillic/punctuation fallback inventory;
unsupported characters, including emoji, retain visible missing-glyph boxes.
Source URLs, SHA-256 identities and Unicode ranges are in
[`sources.json`](fonts/github-badge/sources.json). Rebuild with
`python3 tools/generate_badge_fonts.py --converter /path/to/lv_font_conv`, using
version 1.5.3. The fonts are compiled by `main/CMakeLists.txt`, with one-bit
uncompressed glyphs in Flash. Chinese glyph coverage and mixed-text rendering
are checked in the native C renderer; physical display rendering still requires
on-device validation.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/github-badge-pages.png`](images/github-badge-pages.png) | 1080 × 712, PNG RGB | Seven native C/LVGL badge views with synthetic data, composed for the English/Chinese project README. Generated from this project's UI; distributed under the project license. This is a software preview, not a hardware photograph. |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Upstream AI Passport product image retained in the hardware platform documentation at `docs/README.md` and its Chinese pair. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the upstream repository's original `images/logo.png`; retained in the platform documentation for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the platform documentation's `<picture>` element for dark backgrounds. |

Regenerate the badge collage from the repository root after activating the build environment:

```bash
./tools/render_badge_ui.sh build/github-ui-preview
python3 tools/compose_badge_preview.py build/github-ui-preview/avatar assets/images/github-badge-pages.png
```

The fixture data and generated avatar come from `tests/ui_render/render.c` and
`tests/ui_render/fixtures.py`. The composition script uses only Python's standard
library and places each 240 × 320 page at its original pixel size. The image
contains no downloaded personal account data, real avatar or setup QR secret.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).

## Reference designs

[`reference/github_pixel_240x320_v20.html`](reference/github_pixel_240x320_v20.html)
is the unchanged HTML reference supplied by the user for the personal GitHub
badge. It is not embedded in the firmware. Its provenance and SHA-256 are recorded
in the [application guide](../docs/development/github-badge.md); no separate
redistribution license was supplied for this reference.
