/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Measured propagation from wspr.live, and measured ionosphere from kc2g.
 *
 * Both answer "is the band open" with observation rather than a model, which is
 * the distinction this project cares about. wspr.live aggregates server-side in
 * SQL, so the device receives a summary of a few hundred bytes instead of
 * megabytes of raw spots — the property that makes it viable here at all.
 *
 * Queries are made by grid FIELD (e.g. "JO"), never by callsign. A field is
 * roughly a thousand kilometres across: it identifies a region, not a station.
 * The operator's callsign is never transmitted anywhere.
 *
 * wspr.live is volunteer-run: non-commercial use only, 20 requests/minute, and
 * queries are expected to be bounded by time and band.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lib/geo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROP_BAND_COUNT 12
#define PROP_MAX_SPOTS  320

typedef enum {
    PROP_CLOSED = 0,
    PROP_POOR,
    PROP_FAIR,
    PROP_GOOD,
} prop_cond_t;

typedef struct {
    const char *name;   /* "20m" */
    int  key;           /* wspr.live band key, MHz (0 = 630m) */
    int  good;          /* grid counts that qualify as good / fair / poor */
    int  fair;
    int  poor;
} prop_band_t;

typedef struct {
    int         grids;      /* distinct 4-character grids reached in the window */
    int         snr_avg;
    prop_cond_t cond;
} prop_band_state_t;

typedef struct {
    geo_point_t pos;
    int8_t      snr;
} prop_spot_t;

typedef struct {
    char  station[40];
    float muf;              /* MUF(3000) in MHz */
    float fof2;
    float distance_km;
    int   age_min;
    bool  valid;
} prop_muf_t;

/** @brief Band table entry, or NULL if out of range. */
const prop_band_t *prop_band(int index);

/** @brief Text for a condition, e.g. "Good". */
const char *prop_cond_text(prop_cond_t c);

/**
 * @brief Conditions for every band in one request.
 *
 * A single GROUP BY over the last hour returns all twelve bands in about
 * 500 bytes, so the keypad colours and the maps behind them can never disagree.
 *
 * @param grid_field Two-character Maidenhead field, e.g. "JO".
 */
esp_err_t prop_fetch_conditions(const char *grid_field,
                                prop_band_state_t out[PROP_BAND_COUNT]);

/**
 * @brief Grids reached on one band, for the propagation map.
 *
 * @param out_count Receives the number of spots written.
 */
esp_err_t prop_fetch_map(const char *grid_field, int band_key, prop_spot_t *out,
                         int max, int *out_count);

/**
 * @brief Nearest live ionosonde to @p qth.
 *
 * Gives a measured frequency ceiling for the region, independent of who happens
 * to be transmitting. Stations that have stopped reporting are skipped: dead
 * entries stay in the feed for years and would otherwise be shown as current.
 */
esp_err_t prop_fetch_muf(geo_point_t qth, prop_muf_t *out);

#ifdef __cplusplus
}
#endif
