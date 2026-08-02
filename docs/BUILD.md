# Building

## Requirements

- **ESP-IDF v5.5.4.** Other 5.x versions will probably work; this is the one it
  is developed and tested against.
- Python 3 (comes with ESP-IDF) for the generator scripts.
- Node, only if you want to regenerate the fonts.

## Quick start

```bash
. $IDF_PATH/export.sh              # or the Windows PowerShell profile
idf.py build                       # Waveshare, the default board
idf.py -p /dev/ttyUSB0 flash monitor
```

## Both boards at once

```bash
python tools/build_all.py                  # both
python tools/build_all.py sunton           # just one
```

Each board gets its own build tree and its own `sdkconfig`, and the finished
binaries are collected into `firmware/<board>/`. The separation is not
cosmetic: the pin assignments share no numbers and the panel timings differ, so
building one on top of the other's configuration produces a firmware that
flashes cleanly and displays nothing.

To build a single board by hand:

```bash
idf.py -B build-sunton -D BOARD=SUNTON_5 -D SDKCONFIG=sdkconfig.sunton build
```

`BOARD` is `WAVESHARE_7` or `SUNTON_5`, and defaults to the former.

## Generated sources

Three files are generated and committed, so a normal build needs no network.
Re-run these only when the upstream data changes.

```bash
python tools/gen_dxcc.py         # main/lib/dxcc.{c,h}
python tools/gen_landmask.py     # main/lib/landmask.{c,h}
./tools/gen_fonts.sh             # main/ui/fonts/*.c
```

**`gen_dxcc.py`** fetches AD1C's `cty.csv` and the Club Log most-wanted ranking
from dxnews, and emits 340 DXCC entities, ~8300 callsign prefixes and the top 60
most-wanted as entity codes. Re-run it after a DXpedition changes what is rare —
the cutoff is `MOST_WANTED_TOP` at the top of the script.

Note the resolution is deliberately careful. A ranking prefix like `3Y/B` or
`SV/A` denotes a sub-entity that *shares* its parent's prefix, so stripping the
slash gives the wrong answer: Bouvet resolves to Antarctica, Mount Athos to
Greece, Clipperton to French Polynesia. The script uses the prefix only when it
is unambiguous and distinctive-word matching otherwise, and prints anything it
cannot resolve rather than guessing.

**`gen_landmask.py`** rasterises Natural Earth coastlines into a 1-bit
1024×512 mask, 64 KB. Both map projections sample it — the azimuthal view by
inverse-projecting each screen pixel — so there is no per-QTH bitmap and no
polygon filling. LVGL has no concave polygon fill, and the fan triangulation
that preceded this visibly distorted anything not roughly convex.

**`gen_fonts.sh`** builds Montserrat at sizes 12–28 with Latin-1 and Cyrillic
(so German, French, Italian, Spanish and Russian all render from one typeface),
plus 36 and 48 px ASCII-only faces for the callsign and clocks.

## Self-tests

The firmware runs its own tests at boot and prints the result over serial:

```
I (1855) selftest: PASS  every string translated   108 strings x 6 languages
I (1855) selftest: ==== ALL PASS ====
```

They cover Maidenhead round-tripping, great-circle bearing and distance against
known paths, solar geometry, sunrise/sunset with atmospheric refraction, unit
conversions and translation coverage. There is no host-side test build — only
Xtensa cross-compilers are available — so the target runs them instead.

## Diagnostic console

Serial console at 115200, prompt `weather>`:

```
lcd                  panel timings; lcd set <field> <value> to change
touch [seconds]      poll the GT911 and report raw touch data
station              show or correct callsign, QTH and locator
dx [mask]            fetch cluster spots; 1 = HamQTH, 2 = DXWatch, 3 = both
map <band> [proj]    open a band's map and report its repaint time
```

`station set DM5AL UNNAU JO30WP` is the quickest way to fix an identity typed
wrongly on the panel.
