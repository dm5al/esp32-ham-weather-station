#!/usr/bin/env python3
"""Render design mockups for the Ham Weather Station into docs/img/.

No firmware exists yet — these are concept renders to argue about before code.

The propagation maps plot REAL measured WSPR data (tools/wspr_sample.py: 184
receiving grids that heard signals from JO** on 20 m over three hours), so the
dot pattern is a genuine band opening rather than a plausible-looking invention.
Beacon bearings and distances are computed from the QTH, not made up.

    python tools/gen_mockups.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from prelude import set_palette  # noqa: E402
import screens  # noqa: E402

OUT = os.path.join(os.path.dirname(HERE), "docs", "img")

JOBS = [
    ("hws-setup-station",   lambda: screens.setup_station(),   False),
    ("hws-home-day",        lambda: screens.home(),            False),
    ("hws-home-night",      lambda: screens.home(),            True),
    ("hws-map-azimuthal",   lambda: screens.map_page(False),   False),
    ("hws-map-greyline",    lambda: screens.map_page(True),    False),
    ("hws-settings",        lambda: screens.settings(),        False),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    for name in os.listdir(OUT):
        if name.startswith("hws-") and name.endswith(".svg"):
            os.remove(os.path.join(OUT, name))
    for name, fn, night in JOBS:
        set_palette(night)
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(fn().out())
        print("wrote %-26s %6d bytes" % (name + ".svg", os.path.getsize(path)))


if __name__ == "__main__":
    main()
