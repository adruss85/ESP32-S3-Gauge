#!/usr/bin/env python3
"""Turn the host renderer's PPMs into the PNGs the README shows.

The renderer writes one PPM per image; this crops nothing, composites nothing
into the screen images and only rescales the contact sheet, so what lands in
assets/ is what LVGL drew.

    ./tools/sim/build/render /tmp/gauge-render
    ./tools/sim/preview.py /tmp/gauge-render assets
"""
import os
import sys

from PIL import Image

SCHEMES = ["midnight", "orange", "green", "blue", "purple", "white", "red"]

# PPM name in the render directory -> PNG name in assets/.
SINGLE = [
    ("boost_midnight", "screen-boost"),
    ("oil-temp", "screen-oil-temp"),
    ("oil-pressure", "screen-oil-pressure"),
    ("coolant", "screen-coolant"),
    ("gmeter", "screen-gmeter"),
    ("haldex", "screen-haldex"),
    ("splash", "screen-splash"),
]

GAP = 16
SHEET_COLS = 4
SHEET_WIDTH = 1440  # the pre-0.7 sheet was 1436 px, so README width is kept


def load(src_dir: str, name: str) -> Image.Image:
    path = os.path.join(src_dir, name + ".ppm")
    img = Image.open(path)
    if img.size != (466, 466):
        raise SystemExit(f"{path} is {img.size[0]}x{img.size[1]}, want 466x466")
    return img.convert("RGB")


def contact_sheet(src_dir: str) -> Image.Image:
    tiles = [load(src_dir, f"boost_{s}") for s in SCHEMES]
    rows = (len(tiles) + SHEET_COLS - 1) // SHEET_COLS
    size = 466
    width = SHEET_COLS * size + (SHEET_COLS + 1) * GAP
    height = rows * size + (rows + 1) * GAP

    sheet = Image.new("RGB", (width, height), (0, 0, 0))
    for i, tile in enumerate(tiles):
        x = GAP + (i % SHEET_COLS) * (size + GAP)
        y = GAP + (i // SHEET_COLS) * (size + GAP)
        sheet.paste(tile, (x, y))

    scale = SHEET_WIDTH / width
    return sheet.resize((SHEET_WIDTH, round(height * scale)), Image.LANCZOS)


def main() -> int:
    src_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/gauge-render"
    dest_dir = sys.argv[2] if len(sys.argv) > 2 else "assets"
    os.makedirs(dest_dir, exist_ok=True)

    for ppm, png in SINGLE:
        path = os.path.join(dest_dir, png + ".png")
        load(src_dir, ppm).save(path, optimize=True)
        print(f"{path}")

    path = os.path.join(dest_dir, "gauge-schemes.png")
    contact_sheet(src_dir).save(path, optimize=True)
    print(f"{path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
