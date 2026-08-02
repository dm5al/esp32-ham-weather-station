# Ham Weather Station — Concept

A shack display for the **Waveshare ESP32-S3-Touch-LCD-7** that answers two
questions at a glance: *what is the weather doing outside*, and *what is the
ionosphere doing above me*.

It is the amateur-radio sibling of the existing
[esp32-weather-station](https://github.com/dm5al/esp32-weather-station), and
reuses most of its foundation.

Status: **Phase 1 firmware runs on hardware.** See §10.

---

## 1. Hard constraints

These are not preferences. Every feature below is judged against them, and
several otherwise-attractive ideas are rejected purely on these grounds.

| # | Constraint | Consequence |
|---|---|---|
| **C1** | **The operator's callsign is never sent to any service.** | Kills every "who heard *me*" feature. See §6. |
| **C2** | All data sources must accept **anonymous** access — no account, no API key, no registration. | Kills QRZ lookup, anything with a token. |
| **C3** | Sources must be **durable**. A source that can vanish with one person may not be load-bearing. | HamClock's backend died with its author. Nothing here may repeat that. |
| **C4** | **Non-commercial, open source.** | Compatible with every source chosen; some (wspr.live) *require* non-commercial. |
| **C5** | Must fit the hardware in §2. | Kills full-resolution NOAA products, heavy animation, audio. |

### C1 in practice

The callsign is display-only. It is shown on screen, stored in NVS, and used
for nothing else. It is never placed in a URL, header, query or login.

This is a genuine functional loss and should be understood up front: the
"thirty seconds after you key up, see who copied you" feature — the most
personally compelling thing in this whole space — **is not available to us**,
because RBN and every DX cluster node authenticate by callsign.

There is one subtler leak worth naming: **IP geolocation** tells a third party
roughly where the device is. It does not reveal the callsign, but for an
operator whose callsign is publicly tied to an address, location *is* identity.
Manual grid entry must therefore be offered and should be the recommended
setting; IP geolocation is a convenience default, not the only path.

Propagation queries are made **by grid field** (e.g. `JO`), never by callsign.
A grid field is roughly 1000 km across — it identifies a region, not a station.

---

## 2. Hardware envelope

Measured on the real board during the weather-station build, not from a
datasheet.

| | |
|---|---|
| MCU | ESP32-S3R8, dual LX7 @ 240 MHz |
| PSRAM | 8 MB octal @ 80 MHz |
| Flash | 8 MB — 4 MB app partition, **~2.2 MB free** after the weather app |
| Internal RAM | ~247 KB free at boot with LVGL + WiFi up |
| Display | 800×480 RGB565, no GPU, LVGL software rendering |
| Input | **Capacitive touch only.** No buttons, no encoder, no keyboard. |
| Audio | **None.** No codec, no I2S, no amplifier. |
| Clock | **No battery-backed RTC.** Time is wrong until SNTP succeeds. |
| Free GPIO | Very few — the RGB panel takes 20, plus I²C, touch INT, UART, USB, CAN (19/20), RS485 (15/16) |
| Storage | microSD slot (CS on the CH422G expander) — unused, available for map data |

### The binding constraint: PSRAM bandwidth

The LCD DMA reads the frame buffer continuously at roughly **42 MB/s**. Starving
it produces a permanently shifted image — this cost real debugging time on the
weather station. The working configuration is bounce buffers (10 lines, 32 KB
internal SRAM) with `CONFIG_LCD_RGB_RESTART_IN_VSYNC` **off** and code/rodata
kept **out** of PSRAM.

Practical rules that follow:

- **One animated element at a time**, and only in a small area. Seven animated
  icons were measured to be a bad idea.
- Full-screen redraws are affordable occasionally, not continuously.
- A full-screen world map is fine as a *static* backdrop with dots drawn over
  it; a continuously animating map is not.

### Payload budget

Every HTTPS fetch costs roughly 40–50 KB of transient heap for the TLS session.
Responses must be small or streamed. This immediately disqualifies:

| Product | Size | Verdict |
|---|---|---|
| `ovation_aurora_latest.json` | **918 KB** | Too large — reject or find a summary |
| `xrays-1-day.json` | **655 KB** | Too large — use a summary product |
| `drap_global_frequencies.txt` | 42 KB | Borderline — parse incrementally |
| `kc2g stations.json` | ~40 KB | Acceptable |

### Touch-only implications

- Minimum touch target **44×44 px**. No hover states, no tooltips, no right-click.
- Text entry only via the on-screen keyboard (already built).
- Navigation must be **visible**, not gestural — a persistent tab bar, not swipes,
  because a swipe-only UI has no affordance telling the user it exists.

---

## 3. Data sources

Verified working, anonymous, on 2026-08-01.

### Tier 1 — institutional, safe to depend on

| Source | Product | Size |
|---|---|---|
| **NOAA SWPC** | `summary/10cm-flux.json` (SFI) | **47 B** |
| | `summary/solar-wind-speed.json` | **59 B** |
| | `summary/solar-wind-mag-field.json` (Bt, Bz) | **60 B** |
| | `noaa-planetary-k-index.json` (Kp history) | 4.7 KB |
| | `text/3-day-forecast.txt` (Kp forecast + rationale) | 1.8 KB |
| | `noaa-scales.json` (R/S/G scales) | 1.1 KB |
| | `drap_global_frequencies.txt` (absorption) | 42 KB |
| **Open-Meteo** | weather, forecast, sunrise/sunset | ~4 KB |

NOAA SWPC is a US government agency publishing operational space weather. It is
about as durable as a public data source gets. Open-Meteo is already proven in
the weather station.

### Tier 2 — community, established, but must degrade gracefully

| Source | Product | Notes |
|---|---|---|
| **wspr.live** | measured band openings, aggregated server-side | 1–3 KB per band. Non-commercial only, 20 req/min, volunteer-run |
| **prop.kc2g.com** | 101 live ionosondes, foF2 / MUF | Nearest to JO30 is Dourbes at 245 km |
| **DXSummit** | DX spots with **both ends as lat/lon** | ⚠️ **HTTP only — no working HTTPS** |
| **date.nager.at** | public holidays by ISO subdivision | Already used in the weather station |

### Tier 3 — useful but fragile, never load-bearing

| Source | Why fragile |
|---|---|
| **hamqsl.com** (N0NBH) | Precomputed band conditions, but it is **one person's server** — the exact failure mode that killed HamClock |

**Design rule:** band conditions must be *derivable* from Tier 1 + kc2g (MUF for
the ceiling, DRAP for absorption). hamqsl may be used as a convenience when
available, never as the only path.

### Zero-network

The **NCDXF/IARU beacon cycle** is pure UTC arithmetic — 18 beacons, 5 bands,
3-minute rotation. No API, nothing to fail, no privacy exposure.

### Notes on specific sources

- **DXSummit is plain HTTP.** No credentials are sent so nothing is exposed, but
  the data is unauthenticated and interceptable. Acceptable for spots; it must
  never be trusted for anything security-relevant, and the UI should not imply
  it is verified.
- **wspr.live** asks for bounded queries and ≤20 req/min. One request per band
  change plus a 15-minute refresh sits far inside that.

---

## 4. Feature triage

### MUST — the product is pointless without these

| Feature | Source | Cost | Notes |
|---|---|---|---|
| UTC + local clock, date | SNTP | Trivial | UTC is the reference time for radio. Must show `--:--` until synced, never a plausible wrong time. |
| Maidenhead grid | local maths | Trivial | From lat/lon or manual entry |
| Local weather: current + 7-day | Open-Meteo | **Already built** | This is a *weather* station too |
| Sunrise / sunset / grey line | Open-Meteo + local maths | Trivial | Grey line is a genuine propagation tool |
| Solar indices: SFI, Kp, A, solar wind, Bz | NOAA | Low | Under 6 KB total |
| Kp 3-day forecast | NOAA | Low | The direct analogue of a weather forecast |
| Band conditions, 80–10 m | derived from MUF + DRAP | Medium | Must not depend on hamqsl alone |
| WiFi setup, settings, language | — | **Already built** | |

### SHOULD — the reason to build it rather than buy a clock

| Feature | Source | Cost | Notes |
|---|---|---|---|
| **Measured MUF from nearest ionosonde** | kc2g | Low | The differentiator: *measured*, 245 km away, minutes old — not a global index |
| **WSPR propagation map per band** | wspr.live | **High** | The flagship. 184 real grids out to 18 593 km on 20 m. Needs map rendering. |
| **NCDXF beacon tracker** | none | Low | Zero network, zero risk, high delight |
| DX spots on map | DXSummit | Medium | Both ends already geocoded |
| DRAP absorption overlay | NOAA | Medium | The absorption floor that MUF alone misses |
| Grey line on world map | local | Medium | Needs the map to exist first |

### LATER — real value, but not in the first build

- Satellite passes (Celestrak TLE + SGP4) — well-trodden but a big chunk of work
- POTA / SOTA activations (both anonymous, POTA includes lat/lon and grid)
- Moon position and phase for EME
- Contest calendar
- SDO solar imagery — needs JPEG decode and a bigger payload budget
- Aurora oval — **only if a compact product exists**; the current one is 918 KB

### REJECTED — and why

| Feature | Reason |
|---|---|
| **RBN "who heard me"** | **C1** — requires callsign login |
| **DX cluster over telnet** | **C1** — nodes authenticate by callsign. Use DXSummit HTTP instead |
| **PSK Reporter personal reports** | **C1** — keyed on callsign; also 2 MB responses (**C5**) |
| **QRZ / callsign lookup** | **C2** — requires an account |
| **VOACAP predictions** | Terms explicitly forbid automated access; self-hosting is out of scope |
| **HamClock backend as a data source** | **C3** — depends on one volunteer instance; the failure we are designing against |
| **WebSDR audio streaming** | **C5** — no audio hardware, no free GPIOs. Deferred to a separate project |
| **Full-resolution NOAA aurora / X-ray** | **C5** — 918 KB and 655 KB |
| **Continuous map animation** | **C5** — PSRAM bandwidth |

---

## 5. Screens

Five screens, persistent bottom tab bar, 44 px minimum targets.

**1 · Home** — dual clock, grid, local weather summary, solar strip, eight band
tiles, grey-line countdown, measured MUF.

**2 · Propagation** — band selector chips across the top; azimuthal-equidistant
map centred on the operator's QTH so screen direction *is* beam heading.
Toggleable overlays: WSPR measured openings, DX spots, grey line.
**Must state plainly that a blank sector means nobody is listening there, not
that the band is closed** — a propagation tool that misleads is worse than none.

**3 · Space weather** — storm banner first (actionable), then indices, then the
24-bar Kp forecast with G1/G2 thresholds, plus the NOAA rationale text.

**4 · Beacons** — NCDXF cycle, what is transmitting now on each band, countdown.

**5 · Settings** — callsign and grid (manual entry preferred over IP
geolocation), units, language, WiFi, panel timing console.

Mockups of screens 1–4 exist in [docs/img](docs/img).

---

## 6. Architecture and reuse

Roughly **60–70 %** carries over from the weather station untouched:

```
bsp/          panel, CH422G, GT911, LVGL, lcd timing console   100% reuse
net/          wifi_manager, http_get (TLS), geolocate           100% reuse
ui/           i18n + Cyrillic/Latin-1 fonts, screen manager      ~90% reuse
main.c        app task, command queue, LVGL locking discipline   pattern reuse
```

New work is the data layer (solar, propagation, spots, beacons), the map
renderer, and the tab navigation.

Threading stays as proven: LVGL on its own task, never blocking; all network
work on an app task; UI touched only under `bsp_display_lock()`.

**Resilience requirements**, learned from HamClock:

- Every Tier 2/3 source is optional. Its absence degrades one panel, never the app.
- Cache last-good values in NVS with timestamps, and **show the age** — stale
  data presented as current is the failure mode to avoid.
- No single source may be required for boot.

---

## 7. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Map rendering exceeds the PSRAM/DMA budget | **High** | Static backdrop + overlay dots. Prototype early; it is the one feature that could fail on hardware. |
| Flash exhaustion (2.2 MB free, map data is large) | Medium | Map data on the microSD card, not in flash |
| wspr.live or kc2g disappears | Medium | Both optional; NOAA-derived conditions remain |
| DXSummit HTTP-only | Low | No credentials at risk; do not present as verified |
| Users read the propagation map as authoritative | **High** | Explicit on-screen caveats; label measured vs modelled |

---

## 8. Phasing

**Phase 1 — foundation.** Fork the weather station, add tab navigation, dual
clock, grid, solar indices, Kp forecast, band conditions. No map. Ships as a
genuinely useful shack display and proves the data layer.

**Phase 2 — beacons.** NCDXF tracker. Cheap, offline, high delight.

**Phase 3 — the map.** Static world backdrop, then WSPR overlay, then DX spots,
then DRAP. The riskiest part, attempted only once everything else is stable.

**Phase 4 — later features**, as appetite allows.

---

## 9. Decisions taken

1. **Callsign** is entered in the commissioning assistant and shown large at the
   top left of every page. Display only — never transmitted (**C1**).
2. **QTH and locator are entered by hand**, also in commissioning. No IP
   geolocation: manual entry is both more private and more accurate, and the
   locator is what every bearing on the device depends on.
3. **Both map projections ship**, selectable in Settings. They answer different
   questions — azimuthal gives true beam headings, equirectangular shows the
   grey line, which is meaningless on an azimuthal plot.
4. **Night mode is Off / Auto / On**, in Settings. Auto follows civil twilight
   rather than sunset (sunset is too early; full dark too late), with 1 degree
   of hysteresis so it cannot flicker.

### Navigation

The band keypad is the primary navigation: twelve bands, four wide, each
showing its own measured condition and acting as the button that opens that
band's map. Beacons and Settings are icon buttons in the header. There is no
bottom tab bar — it cost 52 px on every page and the keypad made it redundant.

Actions live along the bottom edge, in thumb reach: projection toggle bottom
left, Back bottom right. The header is identical on every page, so the clock and
link state never move.

## 10. Implementation status

Phase 1 is under way. Built and compiling:

| Module | What it does |
|---|---|
| `bsp/` | panel, CH422G, GT911, LVGL, timing console — carried over intact |
| `lib/geo` | Maidenhead locators, great-circle bearing and distance |
| `lib/solar` | subsolar point, terminator, twilight rings, sunrise/sunset, grey line |
| `lib/beacons` | NCDXF 18-beacon cycle, computed from UTC alone |
| `lib/station` | callsign, QTH, locator in NVS; exposes the grid *field* only |
| `lib/selftest` | on-target checks for all of the above |
| `net/spacewx` | NOAA SWPC: SFI, Kp, A, wind, Bz, R/S/G, 3-day forecast, discussion |
| `net/propagation` | wspr.live conditions and maps, kc2g nearest ionosonde |
| `net/weather` | Open-Meteo, decoupled from the weather station's geolocation |
| `ui/theme` | day and night palettes, auto switching |
| `ui/ui_home` | header, band keypad, solar, scales, Kp forecast, alerts |

All five screens now exist: commissioning, home, map, beacons and settings.

| Screen | State |
|---|---|
| Commissioning | verified on hardware — joins a network, takes callsign/QTH/locator |
| Home | verified — header, band keypad, solar, R/S/G, Kp forecast, alerts |
| Beacons | built, not yet seen on the panel |
| Map | built, not yet seen on the panel |
| Settings | built, not yet seen on the panel |

### Known gaps

- **No i18n.** The weather station's three-language layer with Cyrillic fonts has
  not been ported; every string here is hardcoded English.
- **Date-line clipping.** Equirectangular polygons crossing 180 deg are skipped
  rather than clipped, so Antarctica and eastern Siberia are missing from that
  projection.
- **Azimuthal twilight** uses a subtractive fill because `lv_canvas` has no
  even-odd rule, unlike the SVG mockups. Visually unverified.
- **DRAP absorption** is fetched by nothing yet; band conditions come from
  measured WSPR activity alone.

### A note on testing

There is no host C compiler in this toolchain — only the Xtensa cross-compiler —
so the maths libraries are verified by `lib/selftest.c` running **on the
device**, invoked from the serial console with `selftest`. That is arguably
better than a host test: it exercises the real code on the real FPU, where a
float-versus-double slip would actually show up. `tools/test_geo.c` holds the
same checks for anyone who does have gcc to hand.
