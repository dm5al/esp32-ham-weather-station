# Changelog

## v1.0.0 — 2026-08-02

First release. Runs on two boards from one source tree.

### Display and hardware
- Waveshare ESP32-S3-Touch-LCD-7 (7″) and Sunton ESP32-8048S050C (5″)
- 800×480 RGB565 over LCD_CAM, GT911 touch, tear-free double buffering in PSRAM
- Panel timings tunable over the serial console and stored in NVS, so a picture
  that rolls or shifts can be corrected without a rebuild

### Home screen
- Local weather, sunrise, sunset, time to the next grey line
- NOAA SWPC solar indices, R/S/G scales, three-day Kp forecast
- Info panel at three severities: thunderstorm and gale in red, degrading
  conditions in amber, a most-wanted entity on the cluster in green
- Twelve band buttons with measured conditions

### Propagation maps
- Azimuthal equidistant with bearing spokes every 30°, and grey line with a
  per-pixel shaded terminator
- Dots are real WSPR receptions from the last three hours
- MUF from the nearest ionosonde, named and with its distance

### DX cluster
- HamQTH and DXWatch, merged and deduplicated, eight pages
- Callsigns coloured by category: most wanted, another continent, or near
- Filter by band, mode, category and source, persisted across restarts
- 340 DXCC entities and ~8300 callsign prefixes compiled in, so a spot from a
  source that sends no country name is still identified

### Interface
- Six languages: English, German, Russian, French, Italian, Spanish
- EU / UK / USA unit systems affecting clock, temperature, distance and date
- Commissioning assistant; factory reset behind a confirmation

### Privacy
- The callsign never leaves the device. Only the two-character grid field is
  ever transmitted.
- Every data source is anonymous and needs no account

---

## Notes on what this release cost

Kept because the failures were more instructive than the features, and because
the same mistakes are easy to repeat.

**A single tap entered two characters.** Reproducible, but only on the fifth
character of a locator, and never on an all-letter one. Three explanations were
wrong before the log settled it: the duplicate arrived 3 ms after the original,
far too fast for the auto-repeat everyone assumed. The cause was a handler added
by an earlier attempted fix, which re-applied a keyboard flag from inside the
keyboard's own event dispatch. The observations that looked contradictory were
both pointing at the same thing — the fifth character is the first press after
the keyboard changes its button map.

**A numeric field was assumed to be a country code.** It was the spot's age in
seconds. The display confidently placed a Spanish station in Liberia. Entities
now come from the callsign prefix, the way every other piece of ham software
does it — and that resolution is careful too, because stripping the slash off
`3Y/B` or `SV/A` gives the parent entity and would have flagged Greece and Fiji
as most wanted.

**The picture trembled after a canvas grew.** Not the paint routine, which was
never the bottleneck, but LVGL blitting that canvas into the PSRAM framebuffer
and saturating the bus the LCD DMA reads from. Fixed by raising the bounce
buffer from 10 lines to 16.

**A stack overflow appeared the moment a buffer grew.** A 4.5 KB struct on an
8 KB stack that also runs a TLS handshake. Making it static then consumed the
contiguous internal RAM that TLS needed, so the fetches began failing instead —
one bug traded for another. Both are now in PSRAM.

**Two panels, two independent display faults.** On the Sunton, noise in dark
areas turned out to be the clock *source*, and visible flicker the clock *rate*:
the vendor's 18 MHz gives only 40 Hz with those porches. They looked like one
problem and were two.
