#!/usr/bin/env python3
"""Expand the splash JPEG to the RGB565 buffer the firmware decodes.

The board has no JPEG peripheral: src/ui/splash_img.cpp hands the embedded
JPEG to the S3 mask ROM's TJpgDec, which returns RGB888 one MCU band at a
time, and converts each pixel with

    (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3

This does the same conversion with Pillow so the host renderer can show the
real splash art without the ROM. The two decoders are not bit-identical
(JPEG is lossy and the IDCTs differ), which is invisible on a photograph.

    python3 tools/sim/splash_raw.py assets/canvas-gauge-splash-466-base.jpg \\
        tools/sim/build/splash-466.rgb565
"""
import sys

from PIL import Image

WIDTH = 466
HEIGHT = 466


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-2].strip(), file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    img = Image.open(src).convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        print(f"{src} is {img.size[0]}x{img.size[1]}, want {WIDTH}x{HEIGHT}",
              file=sys.stderr)
        return 1

    out = bytearray(WIDTH * HEIGHT * 2)
    raw = img.tobytes()  # RGB, three bytes per pixel, row-major
    for i in range(WIDTH * HEIGHT):
        r = raw[3 * i]
        g = raw[3 * i + 1]
        b = raw[3 * i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[2 * i] = v & 0xFF          # little-endian, as LVGL holds RGB565
        out[2 * i + 1] = (v >> 8) & 0xFF

    with open(dst, "wb") as f:
        f.write(out)
    print(f"{dst}: {WIDTH}x{HEIGHT} RGB565, {len(out)} bytes", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
