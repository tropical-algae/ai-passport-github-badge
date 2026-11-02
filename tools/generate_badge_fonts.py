#!/usr/bin/env python3
"""Rebuild licensed, pinned GitHub badge glyph assets; never download implicitly."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = ROOT / "assets/fonts/github-badge"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", required=True, help="lv_font_conv 1.5.3 executable")
    args = parser.parse_args()
    manifest = json.loads((FONT_DIR / "sources.json").read_text())
    for name, expected in manifest["sha256"].items():
        actual = hashlib.sha256((FONT_DIR / name).read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f"Source identity mismatch: {name}")
    version = subprocess.check_output([args.converter, "--version"], text=True).strip()
    if version != "1.5.3":
        raise SystemExit(f"Expected lv_font_conv 1.5.3; found {version}")
    for size in (8, 10, 14, 22, 0):
        name = f"badge_font_{size}" if size else "badge_font_cjk_10"
        source = "Silkscreen-Regular.ttf" if size else "NotoSansCJKsc-Regular.otf"
        command = [args.converter, "--font", str(FONT_DIR / source), "--range",
                   "0x20-0x7E" if size else manifest["cjk_range"],
                   "--size", str(size or 10), "--bpp", "1", "--format", "lvgl",
                   "--no-compress", "--no-kerning", "--lv-font-name", name,
                   "--lv-include", "lvgl.h", "--output", str(FONT_DIR / f"{name}.c")]
        if size:
            command += ["--lv-fallback", "badge_font_cjk_10"]
        subprocess.run(command, check=True)
    print("GitHub badge fonts generated from verified sources")


if __name__ == "__main__":
    main()
