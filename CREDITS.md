# Credits and third-party material

## Authors

- **Dmitriy Aleksandrov, DM5AL** — reachable via GitHub issues
- Developed with **Claude Opus 5** (Anthropic)

## Data sources

Every source below is public, requires no account, and is queried anonymously.
No callsign or personal identifier is ever transmitted. The only location data
sent is the two-character Maidenhead field (`JO`), which covers roughly
1000 km.

| Source | Used for | Terms |
|---|---|---|
| [NOAA SWPC](https://www.swpc.noaa.gov/) | Solar indices, R/S/G scales, Kp forecast | US Government, public domain |
| [wspr.live](https://wspr.live/) | Measured band conditions and map spots | Free for research; queries kept well inside fair use |
| [prop.kc2g.com](https://prop.kc2g.com/) | Ionosonde MUF | Public API, Andrew Rodland KC2G |
| [Open-Meteo](https://open-meteo.com/) | Local weather and sunrise/sunset | CC BY 4.0, free for non-commercial use |
| [HamQTH](https://www.hamqth.com/) | DX cluster spots | Public feed, Petr Hlozek OK2CQR |
| [DXWatch](https://dxwatch.com/) | DX cluster spots | Public JSON, no restriction in robots.txt |
| [country-files.com](https://www.country-files.com/) | DXCC entities, callsign prefixes | `cty.csv` by Jim Reisert AD1C, free for amateur radio use |
| [dxnews.com](https://dxnews.com/dxcc-2017/) | Most-wanted DXCC ranking | Republishes the Club Log standings |
| [Natural Earth](https://www.naturalearthdata.com/) | Coastlines for the land mask | Public domain |

### Sources considered and rejected

Recorded because the reasoning matters more than the outcome, and saves the next
person repeating the work.

- **DX cluster telnet / Reverse Beacon Network** — require a callsign to log in,
  which is then visible network-wide to anyone typing `sh/users`. Directly
  violates the project's one hard rule.
- **DXHeat** — clean JSON, but `robots.txt` disallows `/source/`, the exact
  endpoint the data lives on. The operator is saying don't automate against it.
- **Holy Cluster** — a single-page app; every API path returns the same 2 KB
  HTML shell. No reachable API without reverse-engineering.
- **QRZCQ** — no API at all. A 61 KB HTML page per fetch and `Crawl-delay: 5`.
- **DXSummit** — permitted by `robots.txt` and has a documented API, but HTTPS
  times out entirely. Plaintext only, which also signals a site not being
  maintained.

## Software

| Component | Licence |
|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 |
| [LVGL](https://lvgl.io/) 9.2 | MIT |
| [esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) | Apache-2.0 |
| [esp_lcd_touch_gt911](https://components.espressif.com/components/espressif/esp_lcd_touch_gt911) | Apache-2.0 |
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT |
| [Montserrat](https://fonts.google.com/specimen/Montserrat) | SIL Open Font License 1.1 |
| [Font Awesome](https://fontawesome.com/) (icon glyphs) | CC BY 4.0 |

## Prior art

**HamClock** by Elwood Downey, WB0OEW, was the reference point for what a
station display should show. Elwood became a silent key on 29 January 2026 and
HamClock ceased functioning that June when its central backend went offline —
every installation in the world stopped receiving data at once, while the
software itself kept running perfectly.

That is the reason this project has no backend of its own, treats every
single-maintainer service as temporary, and makes each page degrade to a visible
"unavailable" rather than quietly showing yesterday's numbers.

**OpenHamClock** carries on the idea as a community fork. Its DX cluster support
is the direct ancestor of this project's, minus the three of its four modes that
require a callsign login.
