#!/usr/bin/env python3
"""Capture the gauge framebuffer from a GAUGE_CAPTURE build.

The firmware renders once per colour scheme and streams each 466x466 RGB565
frame over the USB CDC port after a header line:

    [shot] <index> <width> <height> <stride> <bytes>

This script reads those frames and writes one PPM per scheme. Build with:

    PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
        ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0

Usage:
    python tools/capture_frame.py [PORT] [OUT_PREFIX] [COUNT]

Defaults: /dev/ttyACM0, gauge, 7 (one per scheme).
"""
import re
import sys
import time

import serial

SHOT = re.compile(rb"\[shot\] (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n")
SCHEMES = ["midnight", "orange", "green", "blue", "purple", "white", "red"]


def write_ppm(path: str, raw: bytes, width: int, height: int) -> None:
    out = bytearray(width * height * 3)
    for i in range(width * height):
        v = raw[2 * i] | (raw[2 * i + 1] << 8)
        out[3 * i] = ((v >> 11) & 0x1F) << 3
        out[3 * i + 1] = ((v >> 5) & 0x3F) << 2
        out[3 * i + 2] = (v & 0x1F) << 3
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (width, height))
        f.write(out)


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    prefix = sys.argv[2] if len(sys.argv) > 2 else "gauge"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else len(SCHEMES)

    # Do not pulse RTS: the capture happens after boot, and a reset would
    # restart the seven-frame sequence.
    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = False

    buf = b""
    header = None
    frames = 0
    deadline = time.time() + 90

    try:
        while frames < count and time.time() < deadline:
            chunk = ser.read(16384)
            if chunk:
                buf += chunk

            while True:
                if header is None:
                    m = SHOT.search(buf)
                    if m is None:
                        break
                    idx, w, h, stride, size = (int(x) for x in m.groups())
                    header = (idx, w, h, stride, size)
                    buf = buf[m.end():]
                else:
                    idx, w, h, stride, size = header
                    need = w * h * 2
                    if len(buf) < need:
                        break
                    name = SCHEMES[idx] if idx < len(SCHEMES) else str(idx)
                    path = f"{prefix}_{idx}_{name}.ppm"
                    write_ppm(path, buf[:need], w, h)
                    print(f"{path}: {w}x{h}, stride {stride}, {size} bytes")
                    sys.stdout.flush()
                    buf = buf[need:]
                    header = None
                    frames += 1
                    if frames >= count:
                        break
    finally:
        ser.close()

    if frames < count:
        print(f"only {frames}/{count} frames captured before timeout",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
