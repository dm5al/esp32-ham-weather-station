/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * DX cluster spots from HamQTH.
 *
 * Every conventional route to cluster data — telnet to a DX Spider node, or the
 * Reverse Beacon Network — requires logging in with a callsign, which is then
 * visible to the whole network to anyone typing "sh/users". That is exactly the
 * disclosure this project refuses to make, so none of them can be used.
 *
 * HamQTH publishes the same aggregate as a plain caret-delimited feed over
 * HTTPS with no authentication and no identifier of any kind:
 *
 *   https://www.hamqth.com/dxc_csv.php?limit=N
 *   G0API^144480.0^F6ZAQ/B^IO80XS(TR)JN09CM 569^0859 2026-08-02^^^EU^2M^France^227
 *   spotter^kHz^dx^comment^HHMM date^^mode^continent^band^country^dxcc
 *
 * At roughly 80 bytes per spot this is a fraction of the cost of the JSON
 * alternatives, and it parses by splitting in place with no allocation.
 *
 * The trade is freshness and durability: spots run about 30 s behind a live
 * telnet feed, which is irrelevant on a display that refreshes every quarter
 * hour, and HamQTH is maintained by one operator. Callers must treat absence as
 * normal and say so on screen rather than showing stale data as current.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Three screens of twelve. Fetching more than fits costs nothing extra — the
 * feed is one request whatever the count — and filtering can discard most of a
 * page, so a shallow buffer would leave the table half empty. */
#define DX_ROWS_PER_PAGE 12
#define DX_PAGES         8
#define DX_MAX_SPOTS     (DX_ROWS_PER_PAGE * DX_PAGES)

/* Bands the table can filter on, in the order the buttons appear. */
typedef enum {
    DXB_160, DXB_80, DXB_40, DXB_30, DXB_20, DXB_17,
    DXB_15, DXB_12, DXB_10, DXB_6, DXB_2, DXB_OTHER,
    DXB_COUNT,
} dx_band_t;

/* Mode groups. Anything unrecognised lands in OTHER rather than vanishing —
 * a filter that silently drops spots is worse than one that over-includes. */
typedef enum {
    DXM_CW, DXM_SSB, DXM_FT8, DXM_FT4, DXM_DIGI, DXM_OTHER, DXM_COUNT,
} dx_mode_t;

/*
 * Where spots come from.
 *
 * HamQTH is the primary and the only one enabled by default. DXWatch is the one
 * other service of the five suggested that survived checking: it answers over
 * HTTPS with JSON, needs no account and no identifier, and its robots.txt is
 * empty. DXHeat disallows the very endpoint its data lives on, Holy Cluster
 * serves a single-page app with no reachable API, and QRZCQ has no API at all —
 * a 61 KB HTML page and a five-second crawl delay.
 */
typedef enum { DXS_HAMQTH, DXS_DXWATCH, DXS_COUNT } dx_source_t;

typedef struct {
    uint16_t bands;        /* bitmask over dx_band_t; 0 means every band */
    uint16_t modes;        /* bitmask over dx_mode_t; 0 means every mode */
    uint8_t  cats;         /* bitmask over dx_category_t; 0 means every one */
    uint8_t  sources;      /* bitmask over dx_source_t */
} dx_filter_t;

/** @brief Human name for a source, for the filter buttons. */
const char *dx_source_name(dx_source_t s);

/*
 * How interesting a spot is, in the two ways this device can actually judge.
 *
 * RARE is membership of a curated most-wanted list. DISTANT is a different
 * continent from the operator's — deliberately not "further than 1000 km",
 * because the feed carries no coordinates for the spotted station. It gives a
 * country name, a DXCC number and a continent, and nothing that would place the
 * station on the globe. Deriving a distance from a country centroid would put
 * every Russian station in Siberia and call the number a measurement, which is
 * exactly the sort of invented precision this project avoids elsewhere.
 *
 * Continent is what the feed knows, and off-continent is what "DX" has always
 * meant in practice.
 *
 * Note this is NOT what WSJT-X colours mean. Its highlighting is worked-before
 * status against your own ADIF log — New DXCC, New Call, New Grid — which needs
 * a logbook. This device has none and never learns what you have worked, so the
 * same scheme is not available to it.
 */
typedef enum {
    DX_COMMON = 0,
    DX_DISTANT,     /* another continent */
    DX_RARE,        /* on the most-wanted list */
} dx_category_t;

typedef struct {
    char  dx[14];        /* the spotted station — the reason to look */
    char  spotter[14];   /* who heard it, which implies the path */
    char  country[28];   /* longest entity name in cty.csv is 32 */
    char  band[6];       /* "20M" as published */
    char  mode[6];       /* "FT8", "CW", "SSB" — inferred, see dxcluster.c */
    char  comment[32];   /* free text from the spotter: untrusted, sanitised */
    char  continent[4];  /* "EU", "NA", ... published or resolved from dxcc */
    uint16_t dxcc;       /* ADIF entity code; the key both services agree on */
    uint8_t  source;     /* dx_source_t that supplied it */
    float freq_khz;
    int   hour;          /* UTC, as published */
    int   minute;
    dx_category_t category;
} dx_spot_t;

/** @brief True if the DXCC entity is on the most-wanted list. */
bool dx_is_rare(int dxcc);

/** @brief Does this spot survive the filter? */
bool dx_spot_matches(const dx_spot_t *s, const dx_filter_t *f);

/** @brief Band and mode bucket names for the filter buttons. */
const char *dx_band_name(dx_band_t b);
const char *dx_mode_name(dx_mode_t m);

/** @brief Category name for the legend, already translated. */
const char *dx_category_name(dx_category_t c);

typedef struct {
    dx_spot_t spots[DX_MAX_SPOTS];
    int       count;
    time_t    updated;
    bool      valid;
} dx_feed_t;

/**
 * @brief Fetch the newest spots, most recent first.
 *
 * On failure @p out is left untouched so the previous set stays on screen; the
 * caller decides whether that is still worth showing.
 */
esp_err_t dx_fetch(dx_feed_t *out, uint8_t sources);

#ifdef __cplusplus
}
#endif
