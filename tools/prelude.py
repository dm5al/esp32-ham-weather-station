"""Drawing primitives, palette and geodesy shared by the mockup screens."""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wspr_sample import WSPR_20M  # noqa: E402,F401

W, H = 800, 480

FONT = "Segoe UI,Roboto,Helvetica,Arial,sans-serif"
MONO = "Consolas,DejaVu Sans Mono,monospace"

QTH = (50.6458, 7.8750)      # JO30WP, Unnau
CALL = "DM5AL"
QTH_NAME = "UNNAU"
GRID = "JO30WP"
ANTIPODE_KM = 20015.0

UTC_HOURS = 16.07            # instant depicted, used for the terminator
SOLAR_DECL = 18.0            # early August


class Palette:
    """Day theme, and a red-preserving night theme for a dark shack."""

    def __init__(self, night=False):
        if night:
            self.bg      = "#120604"
            self.card    = "#221008"
            self.card_hi = "#33180C"
            self.text    = "#FF7A55"
            self.muted   = "#A85030"
            self.accent  = "#FF5722"
            # Red-only cannot encode hue, so conditions are encoded by brightness.
            self.good    = "#FFB08A"
            self.fair    = "#E0603A"
            self.poor    = "#8B3520"
            self.orange  = "#C04A28"
            self.sea     = "#1A0B06"
            self.land    = "#3A1A0E"
        else:
            self.bg      = "#0F172A"
            self.card    = "#1E293B"
            self.card_hi = "#334155"
            self.text    = "#F1F5F9"
            self.muted   = "#94A3B8"
            self.accent  = "#38BDF8"
            self.good    = "#34D399"
            self.fair    = "#FBBF24"
            self.poor    = "#F87171"
            self.orange  = "#FB923C"
            self.sea     = "#16233C"
            self.land    = "#2C4364"
        self.night_shade = "#000000"


_P = Palette()


def set_palette(night):
    global _P
    _P = Palette(night)


def get_palette():
    return _P


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self):
        self.p = []

    def rect(self, x, y, w, h, fill, r=0, opa=None, stroke=None, sw=1):
        a = ' opacity="%s"' % opa if opa is not None else ""
        st = ' stroke="%s" stroke-width="%s"' % (stroke, sw) if stroke else ""
        self.p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%s" '
                      'fill="%s"%s%s/>' % (x, y, w, h, r, fill, a, st))

    def circle(self, cx, cy, r, fill="none", opa=None, stroke=None, sw=1):
        a = ' opacity="%s"' % opa if opa is not None else ""
        st = ' stroke="%s" stroke-width="%s"' % (stroke, sw) if stroke else ""
        self.p.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"%s%s/>'
                      % (cx, cy, r, fill, a, st))

    def line(self, x1, y1, x2, y2, stroke, sw=1, opa=None):
        a = ' opacity="%s"' % opa if opa is not None else ""
        self.p.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" '
                      'stroke-width="%s"%s/>' % (x1, y1, x2, y2, stroke, sw, a))

    def poly(self, pts, fill, opa=None, stroke=None, sw=1, closed=True):
        # An SVG polyline still fills its implicitly-closed area, which is
        # essentially never wanted — force it off rather than rely on callers.
        if not closed:
            fill = "none"
        a = ' opacity="%s"' % opa if opa is not None else ""
        st = ' stroke="%s" stroke-width="%s"' % (stroke, sw) if stroke else ""
        d = " ".join("%.1f,%.1f" % (x, y) for x, y in pts)
        tag = "polygon" if closed else "polyline"
        self.p.append('<%s points="%s" fill="%s"%s%s/>' % (tag, d, fill, a, st))

    def text(self, x, y, s, size, fill, anchor="start", weight="400", font=None, opa=None):
        a = ' opacity="%s"' % opa if opa is not None else ""
        self.p.append('<text x="%.1f" y="%.1f" font-family="%s" font-size="%s" '
                      'font-weight="%s" fill="%s" text-anchor="%s"%s>%s</text>'
                      % (x, y, font or FONT, size, weight, fill, anchor, a, esc(s)))

    def clip_rect(self, cid, x, y, w, h):
        self.p.append('<clipPath id="%s"><rect x="%s" y="%s" width="%s" height="%s"/>'
                      '</clipPath>' % (cid, x, y, w, h))

    def clip_circle(self, cid, cx, cy, r):
        self.p.append('<clipPath id="%s"><circle cx="%s" cy="%s" r="%s"/></clipPath>'
                      % (cid, cx, cy, r))

    def path(self, d, fill="none", stroke=None, sw=1, opa=None, cap="round"):
        a = ' opacity="%s"' % opa if opa is not None else ""
        st = (' stroke="%s" stroke-width="%s" stroke-linecap="%s"' % (stroke, sw, cap)
              if stroke else "")
        self.p.append('<path d="%s" fill="%s"%s%s/>' % (d, fill, st, a))

    def raw(self, markup):
        self.p.append(markup)

    def group(self, clip=None):
        self.p.append('<g clip-path="url(#%s)">' % clip if clip else "<g>")

    def endgroup(self):
        self.p.append("</g>")

    def out(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
                'viewBox="0 0 %d %d"><rect width="%d" height="%d" fill="%s"/>%s</svg>'
                % (W, H, W, H, W, H, _P.bg, "".join(self.p)))


# ---- coarse world outline (mockup fidelity only) ---------------------------

CONTINENTS = {
    "eurasia": [(36,-9),(43,-9),(48,-5),(51,2),(58,5),(58,10),(63,11),(71,25),(69,33),(66,45),
                (70,60),(73,70),(76,90),(73,110),(72,130),(70,160),(66,180),(60,170),(54,160),
                (45,142),(35,130),(30,122),(22,115),(10,105),(8,98),(15,95),(22,90),(15,80),
                (8,77),(20,70),(25,66),(25,60),(27,56),(30,48),(30,32),(36,36),(41,29),(40,26),
                (37,23),(40,20),(45,14),(44,12),(41,15),(38,16),(40,9),(43,3),(36,-9)],
    "africa":  [(37,-6),(35,10),(32,22),(31,32),(12,43),(11,51),(-2,42),(-16,40),(-26,33),
                (-34,26),(-34,18),(-23,14),(-6,12),(4,9),(6,-1),(5,-8),(10,-15),(15,-17),
                (21,-17),(28,-13),(33,-9),(37,-6)],
    "namerica":[(70,-165),(71,-155),(70,-130),(69,-110),(68,-95),(63,-95),(62,-78),(58,-65),
                (52,-56),(47,-53),(45,-60),(41,-70),(35,-76),(30,-81),(25,-80),(30,-88),
                (29,-95),(26,-97),(21,-97),(18,-95),(16,-95),(13,-87),(9,-83),(9,-78),
                (15,-88),(18,-92),(21,-105),(23,-110),(30,-115),(34,-120),(40,-124),(48,-125),
                (55,-133),(60,-140),(60,-148),(58,-155),(63,-165),(66,-164),(70,-165)],
    "samerica":[(12,-72),(11,-64),(6,-58),(4,-52),(-1,-50),(-5,-36),(-13,-38),(-23,-41),
                (-33,-53),(-38,-57),(-42,-64),(-50,-69),(-55,-68),(-53,-72),(-46,-75),
                (-38,-73),(-30,-71),(-18,-70),(-6,-81),(0,-80),(6,-77),(12,-72)],
    "oceania": [(-11,131),(-12,137),(-11,142),(-17,146),(-24,153),(-33,152),(-38,146),
                (-38,141),(-35,138),(-32,134),(-34,120),(-32,116),(-22,114),(-18,122),
                (-14,127),(-11,131)],
    "greenland":[(83,-32),(81,-18),(76,-20),(70,-22),(65,-40),(60,-44),(67,-53),(72,-55),
                 (76,-60),(80,-65),(83,-45),(83,-32)],
    "nz":      [(-35,173),(-38,178),(-42,174),(-46,168),(-45,167),(-41,172),(-35,173)],
    "uk":      [(58,-5),(57,-2),(53,0),(51,1),(50,-4),(53,-5),(55,-6),(58,-5)],
    "japan":   [(45,142),(43,145),(38,141),(35,140),(33,131),(34,129),(37,137),(41,140),(45,142)],
    "madagascar":[(-12,49),(-16,50),(-22,48),(-25,47),(-23,44),(-16,44),(-12,49)],
}


# ---- geodesy ---------------------------------------------------------------

def grid_to_latlon(g):
    g = g.upper()
    return ((ord(g[1]) - 65) * 10 - 90 + int(g[3]) + 0.5,
            (ord(g[0]) - 65) * 20 - 180 + int(g[2]) * 2 + 1)


def bearing_distance(a, b):
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dl = math.radians(b[1] - a[1])
    x = math.sin(dl) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    brg = (math.degrees(math.atan2(x, y)) + 360) % 360
    dp = p2 - p1
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return brg, 2 * 6371 * math.asin(math.sqrt(h))


def snr_colour(snr):
    p = get_palette()
    if snr >= -10:
        return p.good
    if snr >= -18:
        return p.fair
    if snr >= -24:
        return p.orange
    return p.poor


def terminator_lat(lon):
    """Latitude of the day/night terminator at a given longitude."""
    lon_sun = 15.0 * (12.0 - UTC_HOURS)
    d = math.radians(SOLAR_DECL)
    return math.degrees(math.atan(-math.cos(math.radians(lon - lon_sun)) / math.tan(d)))
