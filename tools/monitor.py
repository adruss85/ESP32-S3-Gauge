#!/usr/bin/env python3
"""Read the gauge serial console, pulsing RTS first so the boot log is included.

Usage: python tools/monitor.py [PORT] [SECONDS]

The display board is native USB CDC on /dev/ttyACM0; pass /dev/ttyUSB0 for
the WROOM bench listener.
"""
import sys
import time

import serial


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 0  # 0 = forever

    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False
    time.sleep(0.05)

    deadline = time.time() + seconds if seconds else None
    try:
        while deadline is None or time.time() < deadline:
            data = ser.read(4096)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
