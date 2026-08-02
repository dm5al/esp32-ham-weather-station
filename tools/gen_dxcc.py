#!/usr/bin/env python3
"""Generate the DXCC entity table from AD1C's cty.csv.

HamQTH publishes an ADIF DXCC entity code, a country name and a continent with
every spot. DXWatch publishes none of them — its numeric fields turned out to be
the age of the spot in seconds and of the spotter's, which is why reading one as
a country code produced confident nonsense (EA1BVG in "Liberia").

So this emits two tables: entity code to name and continent, for HamQTH; and
callsign prefix to entity, so a DXWatch spot can be placed the same way every
other piece of ham software places one. The most-wanted test then matches on the
code, exactly, rather than on a name each service spells differently.

Source: https://www.country-files.com/cty/cty.csv, maintained by Jim Reisert
AD1C. Freely published for amateur radio use; this reads the entity name,
continent and ADIF code and nothing else.

    python tools/gen_dxcc.py
"""
import csv
import html
import io
import re
import os
import urllib.request

from fetch_ranking import fetch_ranking

URL = "https://www.country-files.com/cty/cty.csv"
# How many of the ranking count as "most wanted" on the display.
#
# The source ranks all 340 entities, which makes the cutoff arbitrary — so pick
# one that means something. At 60, an entity qualifies only if fewer than about
# a fifth of active DXers have worked it, which is roughly where "worth
# interrupting what you are doing" sits.
MOST_WANTED_TOP = 60

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "main", "lib")

# Rank order comes from dxnews.com, which republishes the Club Log most-wanted
# standings (last updated September 2025). Fetched and resolved at generation
# time rather than typed here: a hand-kept list is a hand-kept list, and this one
# moves every time a DXpedition lands.
#
# Names there and in cty.csv rarely match exactly ("DPRK (NORTH KOREA)" against
# "North Korea"), so the prefix column is tried first and the name only as a
# fallback. Anything still unresolved is reported rather than silently dropped.


# Shorter names for the entities that appear constantly.
#
# The table column is 126 px; AD1C's official names are written for a reference
# file, not a display, and "Fed. Rep. of Germany" or "Republic of the Congo"
# simply do not fit. Only entities common enough to be read at a glance are
# renamed — the rare ones keep their formal names, where the extra words are
# often the only thing distinguishing two similar islands.
SHORT_NAMES = {
    "United States": "USA",
    "Fed. Rep. of Germany": "Germany",
    "Republic of the Congo": "Congo",
    "Democratic Rep. of Congo": "DR Congo",
    "European Russia": "Russia",
    "Asiatic Russia": "Russia (Asia)",
    "Republic of Korea": "South Korea",
    "DPR of Korea": "North Korea",
    "Republic of South Africa": "South Africa",
    "Republic of Kosovo": "Kosovo",
    "Republic of South Sudan": "South Sudan",
    "United Nations HQ": "UN HQ",
    "Sov Mil Order of Malta": "SMO Malta",
    "Czech Republic": "Czechia",
    "Slovak Republic": "Slovakia",
    "Dominican Republic": "Dominican Rep.",
    "Central African Republic": "Central African Rep.",
    "United Arab Emirates": "UAE",
    "Trinidad & Tobago": "Trinidad",
    "Antigua & Barbuda": "Antigua",
    "St. Vincent": "St. Vincent",
    "Bosnia-Herzegovina": "Bosnia",
    "European Turkey": "Turkey (EU)",
    "Asiatic Turkey": "Turkey",
    "Papua New Guinea": "Papua N.G.",
    "Sao Tome & Principe": "Sao Tome",
    "Turks & Caicos Islands": "Turks & Caicos",
    "Fed. Rep. of Yugoslavia": "Serbia",
}


# Words that appear in half the entity names and distinguish nothing.
_NOISE = {"island", "islands", "is", "isl", "reef", "rocks", "the", "and", "of",
          "province", "land", "st", "saint"}


def _tokens(name):
    name = re.sub(r"\(.*?\)", " ", name)
    return {w for w in re.sub(r"[^a-z0-9]+", " ", name.lower()).split()
            if w and w not in _NOISE}


def match_by_name(name, codes, entities):
    """Best entity by distinctive word overlap, or None."""
    want = _tokens(name)
    best, best_score = None, 0
    for c in codes:
        score = len(want & _tokens(entities[c][0]))
        if score > best_score:
            best, best_score = c, score
    return best


def main():
    raw = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8", "replace")

    entities = {}
    for row in csv.reader(io.StringIO(raw)):
        if len(row) < 4:
            continue
        try:
            code = int(row[2])
        except ValueError:
            continue
        name, continent = row[1].strip(), row[3].strip()
        name = SHORT_NAMES.get(name, name)
        # Many prefixes map to one entity; the first row wins and they agree.
        entities.setdefault(code, (name, continent))

    codes = sorted(entities)

    # Prefix -> code, built from cty.csv, used to resolve the ranking below.
    pfx_to_code = {}
    for row in csv.reader(io.StringIO(raw)):
        if len(row) < 10:
            continue
        try:
            code = int(row[2])
        except ValueError:
            continue
        pfx_to_code.setdefault(row[0].strip().lstrip("*"), code)
        for tok in row[9].rstrip(";").split():
            tok = re.sub(r"[\(\[<{~].*?[\)\]>}~]", "", tok).strip().lstrip("=").rstrip(";")
            if tok:
                pfx_to_code.setdefault(tok, code)

    ranking = fetch_ranking()
    wanted, missing = [], []
    for rank, prefix, name in ranking[:MOST_WANTED_TOP]:
        hit = None
        if "/" not in prefix:
            hit = pfx_to_code.get(prefix)
        if hit is None:
            hit = match_by_name(name, codes, entities)
        if hit is None:
            missing.append("%d %s %s" % (rank, prefix, name))
        else:
            wanted.append(hit)
    wanted = sorted(set(wanted))

    # Callsign prefix -> entity.
    #
    # Needed because DXWatch identifies the entity of a spot not at all: its
    # numeric fields turned out to be the spot's age in seconds and the
    # spotter's, not a country code. Deriving the entity from the callsign is
    # what every other piece of ham software does, and cty.csv exists for it.
    #
    # Zone overrides in (), [], <> and {} are stripped. Exact-callsign entries
    # (written "=CALL") keep their full length, where longest-match makes them
    # behave as the exact matches they are meant to be.
    prefixes = {}
    for row in csv.reader(io.StringIO(raw)):
        if len(row) < 10:
            continue
        try:
            code = int(row[2])
        except ValueError:
            continue
        for tok in row[9].rstrip(";").split():
            tok = re.sub(r"[\(\[<{~].*?[\)\]>}~]", "", tok).strip().lstrip("=").rstrip(";")
            # Entries containing a slash are exact compound callsigns; the
            # lookup resolves the slash before matching, so they could never be
            # reached and would only pad the table.
            if tok and tok.isalnum():
                prefixes.setdefault(tok, code)
    prefixes = dict(sorted(prefixes.items()))
    max_pfx = max(len(p) for p in prefixes)

    # Names go in one blob with 16-bit offsets: 340 separate pointers would cost
    # more in relocations than the strings themselves.
    blob, offsets = bytearray(), {}
    for code in codes:
        offsets[code] = len(blob)
        blob += entities[code][0].encode("utf-8") + b"\0"

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "dxcc.c"), "w", encoding="utf-8") as f:
        f.write('/*\n * DXCC entity table: ADIF code -> name and continent.\n *\n'
                ' * GENERATED by tools/gen_dxcc.py from AD1C\'s cty.csv - do not edit.\n'
                ' * %d entities, %d most-wanted, %d callsign prefixes.\n */\n'
                '#include <stddef.h>\n#include <string.h>\n\n'
                '#include "lib/dxcc.h"\n\n'
                % (len(codes), len(wanted), len(prefixes)))

        f.write("static const char k_names[] =\n")
        for code in codes:
            f.write('    "%s\\0"\n' % entities[code][0].replace('"', '\\"'))
        f.write("    ;\n\n")

        f.write("const dxcc_entity_t DXCC_TABLE[DXCC_COUNT] = {\n")
        for code in codes:
            name, cont = entities[code]
            f.write("    {%4d, %5d, \"%s\"},   /* %s */\n"
                    % (code, offsets[code], cont, name))
        f.write("};\n\n")

        f.write("/* Sorted, so membership is a binary search like the table itself. */\n")
        f.write("const uint16_t DXCC_MOST_WANTED[DXCC_MOST_WANTED_COUNT] = {\n    ")
        for i, c in enumerate(wanted):
            f.write("%d, " % c)
            if i % 12 == 11:
                f.write("\n    ")
        f.write("\n};\n\n")
        f.write("const dxcc_prefix_t DXCC_PREFIXES[DXCC_PREFIX_COUNT] = {\n")
        for p, code in prefixes.items():
            f.write('    {"%s", %d},\n' % (p, code))
        f.write("};\n")

        # Fixed boilerplate over generated data: both tables come out sorted by
        # code, so both lookups are a binary search.
        f.write("""
static const dxcc_entity_t *find(int code)
{
    int lo = 0, hi = DXCC_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (DXCC_TABLE[mid].code == code) {
            return &DXCC_TABLE[mid];
        }
        if (DXCC_TABLE[mid].code < code) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

const char *dxcc_name(int code)
{
    const dxcc_entity_t *e = find(code);
    return e ? &k_names[e->name_offset] : NULL;
}

const char *dxcc_continent(int code)
{
    const dxcc_entity_t *e = find(code);
    return (e && e->continent[0]) ? e->continent : NULL;
}

bool dxcc_is_most_wanted(int code)
{
    int lo = 0, hi = DXCC_MOST_WANTED_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (DXCC_MOST_WANTED[mid] == code) {
            return true;
        }
        if (DXCC_MOST_WANTED[mid] < code) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return false;
}

/** @brief Exact prefix lookup; the table is sorted so this is a binary search. */
static int prefix_code(const char *p)
{
    int lo = 0, hi = DXCC_PREFIX_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(DXCC_PREFIXES[mid].prefix, p);
        if (c == 0) {
            return DXCC_PREFIXES[mid].code;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

/*
 * Which part of a compound callsign names the entity.
 *
 * "SV8/IU0PYH" is an Italian operating from Greece and counts as Greece, so the
 * added prefix wins over the home call. "W4MQC/1" is a district number and
 * "CT1JGA/P" a portable marker, neither of which changes anything. The rule
 * that covers all three: a suffix that is a known operating marker or a bare
 * digit is ignored, and otherwise the shorter side is the prefix — which works
 * whichever order the operator wrote it in.
 *
 * Maritime and aeronautical mobile belong to no entity at all, so they resolve
 * to nothing rather than to whatever their home call would suggest.
 */
static bool is_marker(const char *s)
{
    static const char *const k[] = {"P", "M", "A", "QRP", "LH", "J", "AG", "AE"};
    if (s[0] && !s[1] && s[0] >= '0' && s[0] <= '9') {
        return true;
    }
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (strcmp(s, k[i]) == 0) {
            return true;
        }
    }
    return false;
}

int dxcc_from_callsign(const char *call)
{
    if (!call || !call[0]) {
        return 0;
    }

    char up[24];
    size_t n = 0;
    for (const char *c = call; *c && n + 1 < sizeof(up); c++) {
        up[n++] = (*c >= 'a' && *c <= 'z') ? (char)(*c - 'a' + 'A') : *c;
    }
    up[n] = '\\0';

    char *slash = strchr(up, '/');
    char *base = up;
    if (slash) {
        *slash = '\\0';
        char *tail = slash + 1;
        char *tail2 = strchr(tail, '/');
        if (tail2) {
            *tail2 = '\\0';      /* "DL1ABC/HB0/P": the middle part decides */
        }
        if (strcmp(tail, "MM") == 0 || strcmp(tail, "AM") == 0) {
            return 0;           /* at sea or in the air: no entity */
        }
        if (!is_marker(tail) && tail[0]) {
            base = (strlen(tail) < strlen(up)) ? tail : up;
        }
    }

    /* Longest match wins, so try the full length first and work down. */
    size_t len = strlen(base);
    if (len > DXCC_PREFIX_MAX) {
        len = DXCC_PREFIX_MAX;
    }
    for (size_t l = len; l >= 1; l--) {
        char pfx[DXCC_PREFIX_MAX + 1];
        memcpy(pfx, base, l);
        pfx[l] = '\\0';
        int code = prefix_code(pfx);
        if (code) {
            return code;
        }
    }
    return 0;
}
""")

    with open(os.path.join(OUT, "dxcc.h"), "w", encoding="utf-8") as f:
        f.write('''/*
 * DXCC entity lookup.
 *
 * Both cluster feeds carry an ADIF DXCC entity code on every spot. HamQTH also
 * sends a country name and continent; DXWatch sends only the number. Resolving
 * the number here means a spot is classified the same way whichever service it
 * came from, and the most-wanted test is an exact code match rather than a
 * substring search against a name that each service spells differently.
 *
 * GENERATED by tools/gen_dxcc.py from AD1C's cty.csv - do not edit by hand.
 * Re-run it after a DXpedition changes what is most wanted, or when an entity
 * is added or deleted.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DXCC_COUNT %d
#define DXCC_MOST_WANTED_COUNT %d
#define DXCC_PREFIX_COUNT %d
#define DXCC_PREFIX_MAX %d

typedef struct {
    uint16_t code;        /* ADIF DXCC entity code */
    uint16_t name_offset; /* into the name blob */
    char     continent[3];
} dxcc_entity_t;

typedef struct {
    char     prefix[DXCC_PREFIX_MAX + 1];
    uint16_t code;
} dxcc_prefix_t;

extern const dxcc_entity_t DXCC_TABLE[DXCC_COUNT];
extern const uint16_t DXCC_MOST_WANTED[DXCC_MOST_WANTED_COUNT];
extern const dxcc_prefix_t DXCC_PREFIXES[DXCC_PREFIX_COUNT];

/**
 * @brief Entity code for a callsign, or 0 if it cannot be placed.
 *
 * Handles compound calls: an added prefix wins over the home call, portable and
 * district markers are ignored, and maritime or aeronautical mobile resolves to
 * nothing because neither belongs to an entity.
 */
int dxcc_from_callsign(const char *call);

/** @brief Entity name, or NULL if the code is unknown. */
const char *dxcc_name(int code);

/** @brief Two-letter continent, or NULL if the code is unknown. */
const char *dxcc_continent(int code);

/** @brief True if the entity is on the most-wanted list. */
bool dxcc_is_most_wanted(int code);
''' % (len(codes), len(wanted), len(prefixes), max_pfx))

    print("wrote %d entities, %d most-wanted, %d bytes of names"
          % (len(codes), len(wanted), len(blob)))
    if missing:
        print("WARNING: no match for: %s" % ", ".join(missing))


if __name__ == "__main__":
    main()
