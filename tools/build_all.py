#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE.
"""Build the firmware for every supported board, into separate directories.

Each board gets its own build tree and its own sdkconfig, because they are not
interchangeable: the pin assignments share no numbers and the panel timings
differ. Building one on top of the other's configuration produces a firmware
that flashes cleanly and shows nothing.

    python tools/build_all.py            # both boards
    python tools/build_all.py waveshare  # just one

Run it from an ESP-IDF environment. The finished binaries are collected into
firmware/<board>/ with a note saying which is which, because two files called
ham_weather_station.bin are indistinguishable a week later.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)

BOARDS = {
    "waveshare": {
        "define": "WAVESHARE_7",
        "desc": "Waveshare ESP32-S3-Touch-LCD-7, 7 inch, CH422G expander",
    },
    "sunton": {
        "define": "SUNTON_5",
        "desc": "Sunton ESP32-8048S050C, 5 inch, no expander, PWM backlight",
    },
}


def build(name, spec):
    build_dir = os.path.join(PROJECT, "build-%s" % name)
    sdkconfig = os.path.join(PROJECT, "sdkconfig.%s" % name)

    print("\n=== %s: %s" % (name, spec["desc"]))
    cmd = [
        "idf.py",
        "-B", build_dir,
        "-D", "BOARD=%s" % spec["define"],
        "-D", "SDKCONFIG=%s" % sdkconfig,
        "build",
    ]
    # shell=True on Windows: idf.py is a .bat wrapper there and will not exec.
    res = subprocess.run(cmd, cwd=PROJECT, shell=(os.name == "nt"))
    if res.returncode != 0:
        print("!! %s FAILED" % name)
        return False

    out = os.path.join(PROJECT, "firmware", name)
    os.makedirs(out, exist_ok=True)
    for f in ("ham_weather_station.bin", "bootloader/bootloader.bin",
              "partition_table/partition-table.bin"):
        src = os.path.join(build_dir, f)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(out, os.path.basename(f)))

    with open(os.path.join(out, "README.txt"), "w", encoding="utf-8") as fh:
        fh.write("%s\n\n%s\n\nFlash with:\n"
                 "  esptool.py --chip esp32s3 -p <PORT> --baud 460800 \\\n"
                 "    write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \\\n"
                 "    0x0 bootloader.bin 0x8000 partition-table.bin \\\n"
                 "    0x10000 ham_weather_station.bin\n"
                 % (spec["desc"], "Built from BOARD=%s" % spec["define"]))

    size = os.path.getsize(os.path.join(out, "ham_weather_station.bin"))
    print("=== %s ok, %d bytes -> firmware/%s/" % (name, size, name))
    return True


def main():
    wanted = sys.argv[1:] or list(BOARDS)
    unknown = [w for w in wanted if w not in BOARDS]
    if unknown:
        print("unknown board(s): %s\nknown: %s" % (", ".join(unknown), ", ".join(BOARDS)))
        return 1

    results = {name: build(name, BOARDS[name]) for name in wanted}
    print("\n---")
    for name, ok in results.items():
        print("  %-10s %s" % (name, "ok" if ok else "FAILED"))
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
