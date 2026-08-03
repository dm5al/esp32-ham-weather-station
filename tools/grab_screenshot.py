#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE.
"""Pull a screenshot off the device over the serial console.

Reads the frame buffer rather than photographing the panel, so the result is
pixel-exact: no glare, no colour shift, no moire, no camera angle.

    python tools/grab_screenshot.py COM15 docs/img/home.png
    python tools/grab_screenshot.py COM15 docs/img/map.png --before "map 6 0"

The optional --before runs a console command first and waits for the screen to
settle, so a single invocation can navigate and then capture.

Needs pyserial and Pillow, both of which ship with the ESP-IDF environment.
"""
import argparse
import re
import sys
import time

import serial
from PIL import Image

RUN = re.compile(rb"^([0-9A-F]{4})([0-9A-F]{4})$")


def drain(port, seconds):
    """Read and discard, so a stale log does not look like a reply."""
    end = time.time() + seconds
    while time.time() < end:
        port.read(4096)


def send(port, cmd):
    port.write((cmd + "\r\n").encode())
    port.flush()


def wait_for_prompt(port, tries=25):
    """Poke until the REPL answers, so nothing is sent into the boot log."""
    seen = b""
    for _ in range(tries):
        send(port, "")
        end = time.time() + 1.0
        while time.time() < end:
            seen += port.read(2048)
        if b"weather>" in seen[-300:]:
            return True
    return False


def open_quietly(port_name):
    """Open without resetting the board.

    DTR and RTS are wired to EN and BOOT on these panels, so opening the port
    the ordinary way pulses them and reboots the device — which is fatal here:
    every screenshot then catches a machine fifteen seconds into a cold start,
    with no clock and no data yet. Setting both lines false before open avoids
    the pulse.
    """
    port = serial.Serial()
    port.port = port_name
    port.baudrate = 115200
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    port.open()
    port.dtr = False
    port.rts = False
    return port


def capture(port_name, out_path, before=None, settle=3.0, timeout=180.0):
    port = open_quietly(port_name)

    # Wait for a live prompt rather than assuming one. The console scrolls away
    # under log output, and a command sent into the boot sequence is swallowed.
    if not wait_for_prompt(port):
        print("!! no console prompt on %s" % port_name)
        port.close()
        return 1

    if before:
        print("running: %s" % before)
        send(port, before)
        time.sleep(settle)
        drain(port, 0.5)

    send(port, "shot")

    width = height = 0
    pixels = []
    buf = b""
    deadline = time.time() + timeout
    started = False

    while time.time() < deadline:
        buf += port.read(8192)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip().rstrip(b"\r")

            if line.startswith(b"#SHOT"):
                parts = line.split()
                width, height = int(parts[1]), int(parts[2])
                pixels = []
                started = True
                print("receiving %dx%d ..." % (width, height))
                continue

            if line.startswith(b"#ENDSHOT"):
                port.close()
                total = width * height
                if len(pixels) != total:
                    print("!! got %d pixels, expected %d" % (len(pixels), total))
                    return 1
                return write_png(pixels, width, height, out_path)

            if not started:
                continue
            m = RUN.match(line)
            if m:
                colour = int(m.group(1), 16)
                count = int(m.group(2), 16)
                pixels.extend([colour] * count)

    port.close()
    print("!! timed out after %.0fs with %d pixels" % (timeout, len(pixels)))
    return 1


def write_png(pixels, width, height, out_path):
    img = Image.new("RGB", (width, height))
    out = []
    for c in pixels:
        # RGB565 -> RGB888, replicating the high bits into the low ones so
        # white stays white rather than landing at 248,252,248.
        r = (c >> 11) & 0x1F
        g = (c >> 5) & 0x3F
        b = c & 0x1F
        out.append((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
    img.putdata(out)
    img.save(out_path)
    print("wrote %s (%dx%d)" % (out_path, width, height))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port")
    ap.add_argument("output")
    ap.add_argument("--before", help="console command to run first, e.g. 'map 6 0'")
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds to wait after --before (default 3)")
    args = ap.parse_args()
    return capture(args.port, args.output, args.before, args.settle)


if __name__ == "__main__":
    sys.exit(main())
