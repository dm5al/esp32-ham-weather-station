# Ham Weather Station

A standalone propagation and space-weather display for amateur radio operators,
running on an 800×480 ESP32-S3 touch panel.

It shows what the bands are doing **right now**, measured rather than modelled,
and it does so without telling anybody who you are.

```
DM5AL                    UTC  16:24:31    LOCAL  18:24:31      DX   ⚙
JO30WP · UNNAU           02.08.2026       02.08.2026        updated: 18:24
```

Screenshots are in [`docs/img/`](docs/img/). They are frame-buffer captures read
off a running device over the serial console rather than photographs, so they are
pixel-exact — no glare, no colour shift, no camera angle. Earlier concept renders
were deleted rather than kept with a disclaimer: an image that shows something
the firmware does not do is worse than no image.

**Photographs of real panels are still wanted** — a square-on shot in even light,
no flash, 1600 px or better, as a pull request adding to `docs/img/`.

---

## The privacy rule

**Your callsign never leaves the device.**

This is the constraint the whole design is built around, not a feature added
afterwards. Every conventional route to DX cluster data — telnet to a DX Spider
node, or the Reverse Beacon Network — requires logging in with your callsign,
which is then visible to the entire network to anyone typing `sh/users`. Those
routes are therefore unavailable, and the project uses HTTPS feeds that need no
identifier at all.

The single piece of location data ever transmitted is your **two-character
Maidenhead field** (`JO`), which is roughly 1000 km across and is needed to ask
"what has been heard from this part of the world". Nothing else is sent —
no callsign, no six-character locator, no IP-identifying account.

---

## What it shows

### Home
- Local weather, sunrise, sunset and time to the next grey line
- Solar indices from NOAA SWPC: SFI, A, K, X-ray class, solar wind, Bt, Bz
- R/S/G scale levels and a three-day planetary Kp forecast
- An **info panel** with three severities — red for a thunderstorm overhead or
  a gale threatening the antennas, amber for degrading conditions, green when a
  most-wanted DXCC entity appears on the cluster
- Twelve band buttons showing measured conditions; tap one for its map

### Propagation maps
Per band, from **real WSPR receptions in the last three hours** — every dot is a
grid square that actually heard a signal from your region.

- **Azimuthal equidistant**, centred on your QTH, with bearing spokes every 30°.
  Straight up is true north and the angle to any dot is its great-circle initial
  bearing — the number you turn the rotator to.
- **Grey line** (equirectangular) with a smoothly shaded terminator.
- MUF from the nearest ionosonde, named, because a MUF is a measurement from one
  place and not a property of the sky everywhere.

### DX cluster
Live spots with band, mode, country and comment, up to eight pages. Callsigns
are coloured by category: **most wanted**, **another continent**, or near.
Filter by band, mode, category and source.

### Settings
Language (6), units (EU/UK/USA), Wi-Fi, station identity, factory reset.

---

## Supported hardware

| Board | Size | Notes |
|---|---|---|
| [Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm) | 7″ | CH422G expander, backlight on/off |
| [Sunton ESP32-8048S050C](https://www.openhasp.com/0.7.0/hardware/sunton/esp32-8048s0xx/) | 5″ | No expander, PWM backlight, touch polls (INT unwired) |

Printable desk stands: [Waveshare 7″](https://www.thingiverse.com/thing:7273339)
· [Sunton](https://cults3d.com/en/3d-model/various/desk-stand-for-esp32-8048s043-display)
(drawn for the 4.3″ board — check the fit).

Both are 800×480 RGB565 with GT911 capacitive touch and 8 MB octal PSRAM.
Everything above the BSP is identical; see [`docs/HARDWARE.md`](docs/HARDWARE.md).

---

## Install

Pre-built binaries for both boards are in [`firmware/`](firmware/) with flashing
instructions. Nothing but `esptool` is needed.

## Build from source

Requires ESP-IDF **v5.5.4**.

```bash
idf.py build                                  # Waveshare (default)
python tools/build_all.py                     # both boards, into firmware/
```

Full instructions in [`docs/BUILD.md`](docs/BUILD.md).

---

## Where the data comes from

Every source is public, needs no account, and is used anonymously.

| Source | Provides |
|---|---|
| [NOAA SWPC](https://www.swpc.noaa.gov/) | Solar indices, R/S/G scales, Kp forecast |
| [wspr.live](https://wspr.live/) | Measured band conditions and map spots |
| [prop.kc2g.com](https://prop.kc2g.com/) | Ionosonde MUF |
| [Open-Meteo](https://open-meteo.com/) | Local weather |
| [HamQTH](https://www.hamqth.com/) | DX cluster spots |
| [DXWatch](https://dxwatch.com/) | DX cluster spots |
| [AD1C cty.csv](https://www.country-files.com/) | DXCC entities and callsign prefixes |
| [dxnews.com](https://dxnews.com/dxcc-2017/) | Most-wanted ranking (Club Log) |

Sources were chosen for durability as well as privacy. HamClock stopped working
in June 2026 when its central backend went offline — a reminder that a single
maintainer's server is a *when*, not an *if*. Every page here degrades to an
explicit "unavailable" rather than showing stale data as current, and the
device keeps working when any one source disappears.

---

## Licence

**PolyForm Noncommercial 1.0.0** — see [LICENSE](LICENSE).

Free for any noncommercial use: personal, hobby, club, educational, public
research. Selling it, in any form, is not permitted. Attribution to the authors
must be retained in any redistribution, and in firmware builds that credit is on
the device's About screen.

This makes the project *source-available*, not open source in the formal sense —
the OSI and FSF definitions both forbid restricting the field of endeavour,
commercial use included.

That is a deliberate choice rather than an oversight, and GPL-3.0 was considered
and rejected on a specific ground: copyleft stops a derivative being made
*proprietary*, but not being *sold*. A manufacturer may build a device like this
by the thousand and comply with the GPL simply by publishing the source.

Every figure this device shows comes from a service that is free, anonymous and
largely volunteer-run — NOAA SWPC, wspr.live, prop.kc2g.com, Open-Meteo, HamQTH,
DXWatch, AD1C's `cty.csv`. A single hobbyist station costs them nothing worth
counting. A production run would, and it would be their bandwidth underwriting
someone else's margin. The restriction exists to prevent that, not to protect
revenue — there is none, and none is intended.

## Authors

- **Dmitriy Aleksandrov, DM5AL** — reachable via GitHub issues
- Developed with **Claude Opus 5** (Anthropic)

No warranty. See the LICENSE for the full disclaimer.
