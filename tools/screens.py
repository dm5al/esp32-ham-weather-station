"""Screen definitions for the Ham Weather Station mockups.

Imported by gen_mockups.py, which owns the drawing primitives and the palette.
Kept separate only to keep each file a readable length.
"""
import math

from coastline import LAND
from prelude import (ANTIPODE_KM, CALL, GRID, MONO, QTH, QTH_NAME, SOLAR_DECL, Svg, UTC_HOURS,
                     W, WSPR_20M, bearing_distance, get_palette, grid_to_latlon,
                     snr_colour)

# Band, condition and grids reached in the last hour — all measured from
# wspr.live in a single 506-byte query, not modelled.
BAND_COND = [("630m", "Closed", "2"),  ("160m", "Fair", "30"),
             ("80m",  "Fair",   "67"), ("60m",  "Fair", "44"),
             ("40m",  "Good",  "116"), ("30m",  "Good", "110"),
             ("20m",  "Good",  "186"), ("17m",  "Good", "122"),
             ("15m",  "Good",  "148"), ("12m",  "Fair", "51"),
             ("10m",  "Fair",   "78"), ("6m",   "Poor", "6")]

BEACONS = [
    ("4U1UN",  "New York",     40.75,  -73.97),
    ("VE8AT",  "Nunavut",      79.99,  -85.94),
    ("W6WX",   "California",   37.15, -121.90),
    ("KH6RS",  "Hawaii",       20.79, -156.46),
    ("ZL6B",   "New Zealand", -41.05,  175.58),
    ("VK6RBP", "Australia",   -32.10,  116.05),
    ("JA2IGY", "Japan",        34.46,  136.79),
    ("RR9O",   "Novosibirsk",  54.98,   82.89),
    ("VR2B",   "Hong Kong",    22.28,  114.16),
    ("4S7B",   "Sri Lanka",     6.91,   79.87),
    ("ZS6DN",  "South Africa",-25.90,   28.20),
    ("5Z4B",   "Kenya",        -1.25,   36.65),
    ("4X6TU",  "Israel",       32.05,   34.78),
    ("OH2B",   "Finland",      60.25,   24.40),
    ("CS3B",   "Madeira",      32.68,  -16.90),
    ("LU4AA",  "Argentina",   -34.62,  -58.37),
    ("OA4B",   "Peru",        -12.05,  -77.05),
    ("YV5B",   "Venezuela",    10.48,  -66.87),
]
BANDS_HZ = ["14.100", "18.110", "21.150", "24.930", "28.200"]


def cond_colour(c):
    P = get_palette()
    return {"Good": P.good, "Fair": P.fair, "Poor": P.poor, "Closed": P.muted}[c]


def card(s, x, y, w, h, title=None):
    P = get_palette()
    s.rect(x, y, w, h, P.card, r=12)
    if title:
        s.text(x + 16, y + 24, title, 12, P.muted, weight="600")


def icon_beacon(s, cx, cy, col):
    """Mast with radiating arcs — reads as 'transmitting' at 20 px."""
    s.rect(cx - 1.5, cy - 3, 3, 13, col, r=1)
    s.path("M %.1f %.1f L %.1f %.1f L %.1f %.1f" % (cx - 5, cy + 10, cx, cy + 2, cx + 5, cy + 10),
           stroke=col, sw=2)
    for r in (6, 10):
        s.path("M %.1f %.1f A %d %d 0 0 1 %.1f %.1f" % (cx - r, cy - 4, r, r, cx - r, cy - 4 - 0.1),
               stroke=col, sw=1.6, opa="0.9")
        s.path("M %.1f %.1f A %d %d 0 0 0 %.1f %.1f"
               % (cx - r * 0.8, cy - 5, r, r, cx - r * 0.2, cy - 11), stroke=col, sw=1.8)
        s.path("M %.1f %.1f A %d %d 0 0 1 %.1f %.1f"
               % (cx + r * 0.8, cy - 5, r, r, cx + r * 0.2, cy - 11), stroke=col, sw=1.8)


def icon_gear(s, cx, cy, col):
    s.circle(cx, cy, 7.5, stroke=col, sw=2.2)
    s.circle(cx, cy, 2.6, fill=col)
    for k in range(8):
        s.raw('<rect x="%.1f" y="%.1f" width="3" height="5" rx="1" fill="%s" '
              'transform="rotate(%d %.1f %.1f)"/>' % (cx - 1.5, cy - 13.5, col, k * 45, cx, cy))


def icon_button(s, x, y, kind):
    P = get_palette()
    s.rect(x, y, 54, 42, P.card, r=10)
    if kind == "beacon":
        icon_beacon(s, x + 27, y + 20, P.text)
    else:
        icon_gear(s, x + 27, y + 21, P.text)


def full_header(s, updated="16:02"):
    """Station identity, both clocks, link health, and the two page buttons."""
    P = get_palette()
    s.text(20, 48, CALL, 40, P.text, weight="600")
    s.text(20, 72, GRID + "  -  " + QTH_NAME, 16, P.text)
    s.text(20, 91, GRID, 14, P.accent, font=MONO)

    s.line(282, 10, 282, 88, P.card_hi, 1)
    s.text(298, 28, "UTC", 12, P.muted, weight="600")
    s.text(298, 62, "16:04:22", 29, P.text, weight="600", font=MONO)
    s.text(298, 85, "Sat 01.08.2026", 13, P.muted)

    s.line(452, 10, 452, 88, P.card_hi, 1)
    s.text(468, 28, "LOCAL", 12, P.muted, weight="600")
    s.text(468, 60, "18:04:22", 25, P.text, font=MONO)
    s.text(468, 84, "Sat 01.08.2026 CEST", 12, P.muted)

    icon_button(s, 672, 10, "beacon")
    icon_button(s, 734, 10, "gear")
    s.text(788, 72, "Wi-Fi  Dm5al  -52 dBm", 13, P.good, anchor="end")
    s.text(788, 91, "updated " + updated + "  ·  next in 13 min", 12, P.muted, anchor="end")

    s.line(0, 98, W, 98, P.card_hi, 1)


def alerts(s, x, y, w):
    """Only shown when something is actually wrong — normally this area is empty
    and the band keypad simply sits higher."""
    P = get_palette()
    items = [(P.poor, "G1-G2 STORM", "Likely 02.08 from a CME.",
              "HF degradation expected."),
             (P.fair, "THUNDERSTORM", "18:00-22:00 local.",
              "Gusts to 65 km/h.")]
    for i, (col, title, l1, l2) in enumerate(items):
        yy = y + i * 58
        s.rect(x, yy, w, 52, P.card, r=10)
        s.rect(x, yy, 5, 52, col, r=3)
        s.text(x + 16, yy + 19, title, 12, col, weight="600")
        s.text(x + 16, yy + 34, l1, 12, P.text)
        s.text(x + 16, yy + 47, l2, 11, P.muted)


def band_keyboard(s, x0, y0, sel=None):
    """12 bands, 4 wide, each carrying its measured condition.

    The set is exactly the bands wspr.live has usable data for from this region,
    so every button leads to a real map. 4m and 2200m were dropped after
    measurement: 4m returned 16 spots across 4 grids worldwide in 24 hours.
    """
    P = get_palette()
    bw, bh, gap = 56, 75, 6
    s.text(x0, y0 - 8, "BAND CONDITIONS  ·  TAP FOR MAP", 11, P.muted, weight="600")
    for i, (name, cond, grids) in enumerate(BAND_COND):
        col, row = i % 4, i // 4
        x = x0 + col * (bw + gap)
        y = y0 + row * (bh + gap)
        c = cond_colour(cond)
        on = name == sel
        s.rect(x, y, bw, bh, P.card_hi if on else P.card, r=10)
        s.rect(x, y, bw, 5, c, r=2)
        if on:
            s.rect(x, y, bw, bh, "none", r=10, stroke=P.accent, sw=2)
        s.text(x + bw / 2, y + 34, name, 19, P.text, anchor="middle", weight="600")
        s.text(x + bw / 2, y + 53, cond, 12, c, anchor="middle")
        s.text(x + bw / 2, y + 68, grids, 11, P.muted, anchor="middle")


def bottom_bar(s, left_buttons=(), back=True):
    """Actions live along the bottom edge, within easy thumb reach, so the
    header can stay identical on every page."""
    P = get_palette()
    s.line(0, 424, W, 424, P.card_hi, 1)
    for i, (label, on) in enumerate(left_buttons):
        x = 14 + i * 132
        s.rect(x, 432, 126, 40, P.accent if on else P.card, r=10)
        s.text(x + 63, 458, label, 15, P.bg if on else P.text, anchor="middle",
               weight="600" if on else "400")
    if back:
        s.rect(660, 432, 126, 40, P.card_hi, r=10)
        s.text(723, 458, "< Back", 16, P.text, anchor="middle", weight="600")


# ---- day / night -----------------------------------------------------------

SUBSOLAR = (SOLAR_DECL, 15.0 * (12.0 - UTC_HOURS))

# Angular distance from the subsolar point at which the sun sits at a given
# elevation. 90 deg is the terminator itself; beyond that comes civil, nautical
# and astronomical twilight. Shading between these instead of stepping straight
# to black is what makes a grey-line map read as a lit sphere rather than a
# stencil — and the band between them IS the grey line operators care about.
TWILIGHT = (90.0, 96.0, 102.0, 108.0)
NIGHT_TINT = "#04091A"      # deep blue, not black: black reads as a hole
NIGHT_STEP = 0.15           # stacks to ~0.52 over full night


def small_circle(dist_deg, step=3):
    """Locus of points a given angular distance from the subsolar point."""
    lat_s = math.radians(SUBSOLAR[0])
    lon_s = math.radians(SUBSOLAR[1])
    d = math.radians(dist_deg)
    out = []
    for b in range(0, 361, step):
        br = math.radians(b)
        lat = math.asin(math.sin(lat_s) * math.cos(d)
                        + math.cos(lat_s) * math.sin(d) * math.cos(br))
        lon = lon_s + math.atan2(math.sin(br) * math.sin(d) * math.cos(lat_s),
                                 math.cos(d) - math.sin(lat_s) * math.sin(lat))
        out.append((math.degrees(lat), (math.degrees(lon) + 540) % 360 - 180))
    return out


def twilight_lat(lon, dist_deg):
    """Latitude on a meridian where the sun is at the given angular distance.

    Clamped at the poles: near the subsolar meridian the deeper twilight circles
    run past the pole and simply have no crossing.
    """
    lat_s = math.radians(SOLAR_DECL)
    a = math.sin(lat_s)
    b = math.cos(lat_s) * math.cos(math.radians(lon - SUBSOLAR[1]))
    c = math.cos(math.radians(dist_deg))
    r = math.hypot(a, b)
    if r < 1e-9:
        return 0.0
    v = max(-1.0, min(1.0, c / r))
    return math.degrees(math.asin(v) - math.atan2(b, a))


def is_daylight(lat, lon):
    la, lo = math.radians(lat), math.radians(lon)
    sa, so = math.radians(SUBSOLAR[0]), math.radians(SUBSOLAR[1])
    return (math.sin(la) * math.sin(sa)
            + math.cos(la) * math.cos(sa) * math.cos(lo - so)) > 0


# ---- commissioning assistant ----------------------------------------------

def setup_station():
    P = get_palette()
    s = Svg()
    for i, name in enumerate(("Wi-Fi", "Station", "Location", "Display")):
        x = 40 + i * 186
        done, cur = i < 1, i == 1
        col = P.accent if (done or cur) else P.card_hi
        s.circle(x + 14, 40, 14, fill=col if (done or cur) else P.card, stroke=col, sw=2)
        s.text(x + 14, 46, "OK" if done else str(i + 1), 12,
               P.bg if (done or cur) else P.muted, anchor="middle", weight="600")
        s.text(x + 38, 46, name, 15, P.text if cur else P.muted,
               weight="600" if cur else "400")
        if i < 3:
            s.line(x + 120, 40, x + 176, 40, P.card_hi, 2)

    s.text(40, 100, "Callsign, QTH and locator", 24, P.text, weight="600")
    s.rect(40, 116, 520, 36, P.card, r=10)
    s.rect(40, 116, 4, 36, P.good, r=2)
    s.text(58, 139, "Never sent to any service - this device stays anonymous.", 12, P.good)

    for i, (label, val, active) in enumerate((("CALLSIGN", CALL, True),
                                              ("QTH", QTH_NAME, False),
                                              ("LOCATOR", GRID, False))):
        y = 172 + i * 54
        s.text(40, y + 14, label, 10, P.muted, weight="600")
        s.rect(150, y - 8, 410, 44, P.bg, r=9,
               stroke=P.accent if active else P.card_hi, sw=2)
        s.text(166, y + 21, val, 20, P.text, font=MONO)
        if active:
            s.rect(238, y + 1, 2, 26, P.accent)
    s.text(586, 186, "Locator is derived", 12, P.muted)
    s.text(586, 204, "from QTH, or type", 12, P.muted)
    s.text(586, 222, "it directly.", 12, P.muted)

    ky = 336
    for r, row in enumerate(("QWERTZUIOP", "ASDFGHJKL", "YXCVBNM")):
        kw, gap = 62, 8
        total = len(row) * kw + (len(row) - 1) * gap
        x0 = (W - total) / 2
        for c, ch in enumerate(row):
            s.rect(x0 + c * (kw + gap), ky + r * 46, kw, 40, P.card, r=8)
            s.text(x0 + c * (kw + gap) + kw / 2, ky + r * 46 + 27, ch, 18, P.text,
                   anchor="middle")
    s.rect(640, ky + 138, 120, 40, P.accent, r=8)
    s.text(700, ky + 165, "Next", 16, P.bg, anchor="middle", weight="600")
    return s


# ---- main screen -----------------------------------------------------------

def home():
    P = get_palette()
    s = Svg()
    full_header(s)

    RX, RW = 546, 242
    alerts(s, RX, 106, RW)
    band_keyboard(s, RX, 234)

    LW = 522

    card(s, 12, 106, LW, 94)
    s.text(28, 130, "LOCAL WEATHER", 12, P.muted, weight="600")
    s.text(28, 178, "21", 38, P.text, weight="600")
    s.text(72, 178, "C", 18, P.muted)
    s.text(108, 158, "Overcast", 19, P.accent)
    s.text(108, 182, "feels 20  ·  78 %  ·  3.4 m/s  ·  1013 hPa", 13, P.muted)
    s.text(360, 152, "sunrise", 12, P.muted)
    s.text(360, 172, "05:48", 16, P.fair, font=MONO)
    s.text(360, 192, "sunset  21:16", 13, P.orange, font=MONO)
    s.text(452, 152, "grey line", 12, P.muted)
    s.text(452, 174, "5 h 12 m", 17, P.text, weight="600")

    card(s, 12, 206, LW, 88)
    s.text(28, 230, "SOLAR  ·  NOAA SWPC", 12, P.muted, weight="600")
    for i, (k, v, c) in enumerate((("SFI", "148", P.text), ("SSN", "90", P.text),
                                   ("A", "5", P.good), ("K", "1", P.good),
                                   ("X-ray", "B7.9", P.good), ("Wind", "272", P.text),
                                   ("Bz", "0", P.good))):
        x = 28 + i * 71
        s.text(x, 254, k, 12, P.muted)
        s.text(x, 284, v, 24, c, weight="600")

    card(s, 12, 300, LW, 72)
    for i, (name, lvl, note, col) in enumerate(
            (("RADIO BLACKOUT", "R0", "none", P.good),
             ("SOLAR RADIATION", "S0", "none", P.good),
             ("GEOMAGNETIC", "G0", "quiet now", P.good))):
        x = 28 + i * 172
        s.text(x, 324, name, 11, P.muted, weight="600")
        s.text(x, 356, lvl, 26, col, weight="600")
        s.text(x + 44, 356, note, 13, P.muted)

    card(s, 12, 378, LW, 94)
    s.text(28, 402, "PLANETARY Kp  ·  3-DAY FORECAST", 12, P.muted, weight="600")
    kp = [1.0, 1.0, 1.3, 0.7, 2.0, 1.7, 2.3, 3.3, 4.0, 4.3, 5.7, 4.3,
          5.0, 3.0, 1.7, 1.7, 3.3, 4.0, 3.0, 2.3, 3.0, 1.0, 1.0, 0.7]
    base, bh = 456, 44
    for i, v in enumerate(kp):
        c = P.poor if v >= 5 else (P.fair if v >= 4 else P.good)
        h = max(2, v / 9.0 * bh)
        s.rect(28 + i * 20, base - h, 16, h, c, r=2)
    for i, lbl in enumerate(("Sat 01.08", "Sun 02.08", "Mon 03.08")):
        s.text(28 + i * 160 + 72, base + 14, lbl, 12, P.muted, anchor="middle")
    return s


# ---- propagation map -------------------------------------------------------

def _side_panel(s, n):
    P = get_palette()
    px, pw = 556, 232
    s.rect(px, 106, pw, 62, P.card, r=12)
    s.text(px + 16, 134, str(n), 24, P.text, weight="600")
    s.text(px + 60, 134, "grids reached", 13, P.muted)
    s.text(px + 16, 158, "18 593 km furthest", 14, P.good)

    s.rect(px, 176, pw, 52, P.card, r=12)
    s.text(px + 16, 202, "MUF 21.5 MHz", 17, P.accent, weight="600")
    s.text(px + 16, 220, "Dourbes 245 km  ·  8 min ago", 12, P.muted)

    s.text(px, 254, "SIGNAL-TO-NOISE", 11, P.muted, weight="600")
    for i, (c, lbl) in enumerate(((P.good, ">= -10 dB"), (P.fair, "-10 to -18"),
                                  (P.orange, "-18 to -24"), (P.poor, "below -24"))):
        y = 276 + i * 22
        s.circle(px + 8, y - 4, 5, fill=c)
        s.text(px + 24, y, lbl, 13, P.text)

    s.rect(px, 370, pw, 46, P.card, r=10)
    s.text(px + 12, 390, "Blank areas mean nobody there", 11, P.muted)
    s.text(px + 12, 406, "is listening, not band closed.", 11, P.muted)


def _draw_land(s, proj, clip):
    P = get_palette()
    s.group(clip)
    for poly in LAND:
        pts = [proj(a, b) for a, b in poly]
        s.poly(pts, P.land, stroke=P.sea, sw=0.6)
    s.endgroup()


def map_page(equirect=False):
    P = get_palette()
    s = Svg()
    full_header(s)
    s.text(20, 122, "20 m propagation", 22, P.text, weight="600")
    s.text(210, 122, "WSPR measured  ·  last 3 hours", 13, P.muted)

    n = 0
    if equirect:
        mx, mw = 12, 528
        mh = mw / 2.0
        my = 140

        def proj(lat, lon):
            return mx + (lon + 180) / 360.0 * mw, my + (90 - lat) / 180.0 * mh

        s.rect(mx, my, mw, mh, P.sea, r=10)
        s.clip_rect("eq", mx, my, mw, mh)
        _draw_land(s, proj, "eq")

        s.group("eq")
        # The day side is left untouched — a lit hemisphere should look like the
        # map, not like the map behind a yellow filter.
        night_edge = my + mh if SOLAR_DECL > 0 else my
        for dist in TWILIGHT:
            band = [proj(twilight_lat(l, dist), l) for l in range(-180, 181, 2)]
            s.poly([(mx, night_edge)] + band + [(mx + mw, night_edge)],
                   NIGHT_TINT, opa="%.2f" % NIGHT_STEP)
        curve = [proj(twilight_lat(l, 90.0), l) for l in range(-180, 181, 2)]
        s.poly(curve, "none", opa="0.55", stroke=P.fair, sw=1.6, closed=False)
        s.endgroup()

        for lat in (-60, -30, 0, 30, 60):
            y = proj(lat, 0)[1]
            s.line(mx, y, mx + mw, y, P.text, 1, opa="0.10")
        for lon in range(-120, 121, 60):
            x = proj(0, lon)[0]
            s.line(x, my, x, my + mh, P.text, 1, opa="0.10")
        s.rect(mx, my, mw, mh, "none", r=10, stroke=P.card_hi, sw=2)

        for g, snr in WSPR_20M:
            px_, py_ = proj(*grid_to_latlon(g))
            s.circle(px_, py_, 3.2, fill=snr_colour(snr), opa="0.95")
            n += 1
        qx, qy = proj(*QTH)
        s.circle(qx, qy, 5, fill=P.text)
        s.circle(qx, qy, 9, stroke=P.text, sw=1.5, opa="0.75")
        s.text(mx + 8, my + mh + 20, "Amber line is the terminator  ·  bands are civil, nautical, astronomical twilight",
               12, P.fair)
    else:
        cx, cy, R = 276, 268, 152

        def proj(lat, lon):
            brg, dist = bearing_distance(QTH, (lat, lon))
            rr = R * min(dist, ANTIPODE_KM) / ANTIPODE_KM
            a = math.radians(brg)
            return cx + rr * math.sin(a), cy - rr * math.cos(a)

        s.clip_circle("az", cx, cy, R)
        s.circle(cx, cy, R + 5, fill=P.card)
        s.circle(cx, cy, R, fill=P.sea)
        _draw_land(s, proj, "az")

        s.group("az")
        d_disc = ("M %.1f %.1f A %d %d 0 1 0 %.1f %.1f A %d %d 0 1 0 %.1f %.1f Z"
                  % (cx - R, cy, R, R, cx + R, cy, R, R, cx - R, cy))
        day_inside = is_daylight(*QTH)
        for dist in TWILIGHT:
            ring = [proj(la, lo) for la, lo in small_circle(dist)]
            d_ring = "M " + " L ".join("%.1f %.1f" % pt for pt in ring) + " Z"
            if day_inside:
                # Night is everything outside the circle: even-odd carves it out.
                s.raw('<path d="%s %s" fill-rule="evenodd" fill="%s" opacity="%.2f"/>'
                      % (d_disc, d_ring, NIGHT_TINT, NIGHT_STEP))
            else:
                s.raw('<path d="%s" fill="%s" opacity="%.2f"/>'
                      % (d_ring, NIGHT_TINT, NIGHT_STEP))
        term = [proj(la, lo) for la, lo in small_circle(90.0)]
        s.poly(term, "none", stroke=P.fair, sw=1.6, opa="0.55", closed=True)
        s.endgroup()

        for km in (5000, 10000, 15000):
            s.circle(cx, cy, R * km / ANTIPODE_KM, stroke=P.text, sw=1, opa="0.14")
        s.circle(cx, cy, R, stroke=P.card_hi, sw=2)
        for brg in range(0, 360, 45):
            a = math.radians(brg)
            s.line(cx, cy, cx + R * math.sin(a), cy - R * math.cos(a), P.text, 1, opa="0.10")
        for brg, lbl in ((0, "N"), (90, "E"), (180, "S"), (270, "W")):
            a = math.radians(brg)
            s.text(cx + (R + 15) * math.sin(a), cy - (R + 15) * math.cos(a) + 5, lbl,
                   13, P.muted, anchor="middle", weight="600")

        for g, snr in WSPR_20M:
            px_, py_ = proj(*grid_to_latlon(g))
            s.circle(px_, py_, 3.2, fill=snr_colour(snr), opa="0.95")
            n += 1
        s.circle(cx, cy, 5, fill=P.text)
        s.circle(cx, cy, 9, stroke=P.text, sw=1.5, opa="0.75")

    _side_panel(s, n)
    bottom_bar(s, (("Azimuthal", not equirect), ("Grey line", equirect)))
    return s


# ---- beacons ---------------------------------------------------------------

def beacons():
    P = get_palette()
    s = Svg()
    slot, remaining = 7, 3
    full_header(s)
    s.text(20, 122, "NCDXF / IARU Beacons", 22, P.text, weight="600")
    s.text(250, 122, "18 beacons  ·  3 min cycle  ·  no network needed", 13, P.muted)
    s.text(788, 122, "slot %d of 18" % (slot + 1), 13, P.accent, anchor="end")

    for i, freq in enumerate(BANDS_HZ):
        call, loc, lat, lon = BEACONS[(slot - i) % 18]
        brg, dist = bearing_distance(QTH, (lat, lon))
        x = 12 + i * 156
        s.rect(x, 134, 148, 118, P.card, r=14)
        s.rect(x, 134, 148, 4, P.accent, r=2)
        s.text(x + 74, 158, freq, 14, P.muted, anchor="middle", font=MONO)
        s.text(x + 74, 186, call, 22, P.text, anchor="middle", weight="600")
        s.text(x + 74, 205, loc, 12, P.muted, anchor="middle")
        s.text(x + 74, 226, "%.0f deg - %s km" % (brg, format(int(dist), ",").replace(",", " ")),
               11, P.accent, anchor="middle")
        s.rect(x + 16, 234, 116, 6, P.card_hi, r=3)
        s.rect(x + 16, 234, 116 * remaining / 10.0, 6, P.accent, r=3)
        s.text(x + 74, 250, "%d s" % remaining, 11, P.accent, anchor="middle")

    s.text(20, 278, "FULL CYCLE - BEARING AND DISTANCE FROM " + GRID.upper(), 9,
           P.muted, weight="600")
    active = {(slot - i) % 18 for i in range(5)}
    for idx, (call, loc, lat, lon) in enumerate(BEACONS):
        brg, dist = bearing_distance(QTH, (lat, lon))
        col, row = idx // 6, idx % 6
        x = 12 + col * 260
        y = 288 + row * 22
        on = idx in active
        s.rect(x, y, 252, 19, P.card_hi if on else P.card, r=5)
        if on:
            s.rect(x, y, 3, 19, P.accent, r=2)
        s.text(x + 12, y + 14, call, 12, P.text if on else P.muted,
               weight="600" if on else "400", font=MONO)
        s.text(x + 78, y + 14, loc, 11, P.muted)
        s.text(x + 200, y + 14, "%.0f" % brg, 11, P.accent if on else P.muted,
               anchor="end", font=MONO)
        s.text(x + 246, y + 14, "%.1fk" % (dist / 1000), 11, P.muted, anchor="end",
               font=MONO)
    bottom_bar(s)
    return s


# ---- settings --------------------------------------------------------------

def settings():
    P = get_palette()
    s = Svg()
    full_header(s)
    s.text(20, 122, "Settings", 22, P.text, weight="600")

    card(s, 12, 134, 380, 106, "STATION")
    s.text(28, 180, CALL, 24, P.text, weight="600", font=MONO)
    s.text(28, 204, GRID + "  -  " + QTH_NAME, 13, P.muted)
    s.rect(28, 212, 140, 24, P.card_hi, r=7)
    s.text(98, 229, "Edit", 13, P.text, anchor="middle")
    s.text(186, 229, "entered manually, never sent", 11, P.good)

    card(s, 400, 134, 388, 106, "MAP PROJECTION")
    for i, (name, sub, on) in enumerate((("Azimuthal", "true bearings", True),
                                         ("Grey line", "terminator", False))):
        x = 416 + i * 182
        s.rect(x, 160, 172, 66, P.accent if on else P.card_hi, r=10)
        s.text(x + 86, 190, name, 16, P.bg if on else P.text, anchor="middle",
               weight="600" if on else "400")
        s.text(x + 86, 212, sub, 12, P.bg if on else P.muted, anchor="middle")

    card(s, 12, 250, 380, 106, "NIGHT MODE")
    for i, (name, on) in enumerate((("Off", False), ("Auto", True), ("On", False))):
        x = 28 + i * 118
        s.rect(x, 276, 110, 58, P.accent if on else P.card_hi, r=10)
        s.text(x + 55, 312, name, 16, P.bg if on else P.text, anchor="middle",
               weight="600" if on else "400")
    s.text(28, 348, "Auto follows local sunset - red palette preserves night vision",
           11, P.muted)

    card(s, 400, 250, 388, 106, "LANGUAGE")
    for i, (name, on) in enumerate((("English", True), ("Russian", False),
                                    ("Deutsch", False))):
        x = 416 + i * 120
        s.rect(x, 276, 112, 58, P.accent if on else P.card_hi, r=10)
        s.text(x + 56, 312, name, 15, P.bg if on else P.text, anchor="middle",
               weight="600" if on else "400")

    card(s, 12, 366, 776, 52, "NETWORK AND DATA")
    s.rect(28, 380, 232, 30, P.accent, r=10)
    s.text(144, 400, "Choose a Wi-Fi network", 13, P.bg, anchor="middle", weight="600")
    s.text(280, 392, "Dm5al  -52 dBm", 12, P.text)
    s.text(280, 408, "192.168.178.93", 11, P.muted)
    s.text(430, 392, "NOAA SWPC - Open-Meteo - wspr.live - kc2g", 11, P.muted)
    s.text(430, 408, "No callsign is transmitted to any of them.", 11, P.good)
    bottom_bar(s)
    return s
