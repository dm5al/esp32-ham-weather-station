# Changelog

## v1.2.0 — 2026-08-03

The grey line, which turned out to be wrong in three separate ways.

### Fixed
- **The terminator is a hard edge now, not a gradient.** It was a sixteen-step
  ramp reaching full night at astronomical twilight — eighteen degrees below the
  horizon, a band roughly two thousand kilometres wide — and the darkest step
  only mixed 150 of 255 toward the night colour. A station eight degrees below
  the horizon, sky fully dark, therefore rendered at 44 % of the ramp and 26 % of
  the mix: a barely perceptible grey. The map put the operator in twilight while
  they stood in the dark. There is now one edge, at the geometric terminator
  where solar elevation crosses zero, which is the question a grey line map
  exists to answer and what every other such map draws. The night side is
  shaded 165 of 255 toward the night colour — dark enough that the edge is
  unmistakable, light enough that the coastlines under it stay readable, which
  is what makes it possible to see *which* land is in darkness.
- **The map is redrawn on a timer.** `redraw()` was reachable from exactly three
  places — the projection button, a band change, and new spot data — and nothing
  on a clock, so an open grey line stayed frozen wherever the sun was when the
  page was opened. The sun crosses 0.25° of longitude per minute, which is 0.4 px
  on this map, so the grey line repaints every 60 s and stays under half a pixel
  from true. The azimuthal view repaints every 5 minutes instead: it has no
  terminator to speak of and its repaint costs 229 ms, which is a visible hitch.
- **The home screen's time-to-grey-line counted in fifteen-minute jumps.** It was
  computed only inside `ui_home_set_weather()`, so it advanced when a forecast
  arrived rather than when time passed — and since v1.1 only pushes weather to
  the UI on a successful fetch, a run of failures froze it entirely while the
  clock beside it kept running. Nothing in that figure needs the weather; it
  follows from position and time. Now on its own 30 s tick.

---

## v1.1.0 — 2026-08-03

Three fixes found in use, plus a documentation change.

### Fixed
- **Settings → Station → Edit showed only the first letter of each field.**
  With `accepted_chars` set, LVGL does not assign a text area's contents in one
  go — it replays the string character by character, each raising a change
  event. The duplicate-keypress guard added in v1.0 saw a whole prefill arrive
  in one millisecond and deleted all but the first character. Machine-written
  text is now exempt from the guard.
- **DX cluster mode column showed `E`, or nothing at all.** HamQTH publishes a
  bare `E` for some spots, which was passed straight through. A published mode
  is now only used when it names a mode that exists; otherwise the mode is
  inferred from the frequency using an IARU band-plan table, so CW, digital and
  SSB segments are labelled instead of left blank.
- **Network password sheet had Cancel sitting on top of the input field.** The
  text area spanned the full width behind both buttons; it now ends where the
  buttons begin.
- **Sunton: the picture jumped out of alignment when a button was pressed.** The
  5″ board ran its panel at 24 MHz, drawing 48 MB/s out of PSRAM. A repaint
  blitting into the frame buffer left the bounce buffers no margin, they ran dry
  mid-frame and the RGB DMA lost sync — permanently, since nothing restores it
  before a restart. That is why it appeared to come and go: every reboot cleared
  it. The board now runs at 21 MHz, the same 42 MB/s the Waveshare has always
  used without trouble, at a cost of 53 → 46 Hz refresh.

### Changed
- **Licence stays PolyForm Noncommercial 1.0.0**, and the LICENSE now records
  why GPL-3.0 was considered and rejected. Copyleft prevents a derivative being
  made *proprietary*; it does not prevent one being *sold*. A manufacturer could
  build this by the thousand and comply simply by publishing the source. Every
  figure the device shows comes from a free, anonymous, largely volunteer-run
  service — one hobby station costs them nothing, a production run would, and it
  would be their bandwidth underwriting someone else's margin. The restriction
  is not about revenue; there is none.
- Every source file now carries an `SPDX-License-Identifier` line.
- **New About sheet**, reached from Settings, listing the author, contact
  address, licence, privacy statement, firmware version and a full component
  bill of materials with versions. A support address appears on that screen and
  nowhere else in the project: the licence and documentation are public and get
  scraped, a panel in a shack does not.
- The component versions are read from each dependency's own header at compile
  time, so the list cannot drift from what was linked. A hand-copied bill of
  materials is worse than none — it is wrong silently, and it is trusted. A
  noncommercial project is outside the scope of the EU Cyber Resilience Act, but
  the argument for publishing an SBOM does not depend on being compelled to: when
  the next Mbed TLS advisory lands, the only question is which version is on the
  device.
- **Settings page rearranged.** About and Factory reset moved into their own card
  under Units and formats. On the footer line they overlapped the text beside
  them and cut off the last line of the privacy statement.
- **The privacy claim was narrowed because it was overstated.** "No private data
  is shared" was not quite true: the propagation queries send only the
  two-character grid field, but the weather forecast sends the position derived
  from the full locator, placing the station within about 2.5 km. The device now
  says what is unconditionally true — no callsign and no account reach any
  service — and the About sheet states the location question in full.
- Author's email address removed from the licence and documentation. Contact
  there is via GitHub issues.

### Fixed
- **Map kept the previous band's spots after tapping a new band.** The title was
  relabelled and nothing else, so for the length of the fetch the map showed one
  band's receptions under another band's name. It now clears the spots and MUF
  immediately and shows "measuring..." until real data arrives.
- "All quiet" on the info panel is now "No new events", in all six languages.
- Russian: `ионосонда` corrected to `ионосферный зонд`.

---

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
