# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE.
"""Ranked most-wanted DXCC entities, scraped from dxnews.

Kept in its own module so the regular expressions live in a file written as a
file, rather than inside a string that another script assembles — the escaping
does not survive that round trip.

dxnews republishes the Club Log most-wanted standings, last updated September
2025, ranking all 340 current entities. Lines read:

    1.    P5    DPRK (NORTH KOREA)
"""
import html
import re
import urllib.request

WANTED_URL = "https://dxnews.com/dxcc-2017/"

_ROW = re.compile(r"^(\d{1,3})\.\s+(\S+)\s+(.+)$")
_TAG = re.compile(r"<[^>]+>")
_SCRIPT = re.compile(r"<(script|style)[^>]*>.*?</\1>", re.S)


def fetch_ranking():
    """Return [(rank, prefix, name)], most wanted first."""
    req = urllib.request.Request(WANTED_URL, headers={"User-Agent": "Mozilla/5.0"})
    page = urllib.request.urlopen(req, timeout=60).read().decode("utf-8", "replace")
    page = html.unescape(_SCRIPT.sub("", page))

    out = []
    for line in _TAG.sub("\n", page).split("\n"):
        m = _ROW.match(line.strip())
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3).strip()))
    out.sort()
    return out


if __name__ == "__main__":
    rows = fetch_ranking()
    print("%d ranked entities" % len(rows))
    for r in rows[:10]:
        print("  %3d  %-8s %s" % r)
