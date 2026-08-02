/*
 * Space weather from NOAA SWPC.
 *
 * NOAA is a US government agency publishing operational space weather, which
 * makes it about as durable as a public data source gets — the one source in
 * this project safe to treat as load-bearing.
 *
 * Sunspot number is deliberately absent. NOAA publishes no compact SSN product
 * — the only source is observed-solar-cycle-indices.json, 512 KB of monthly
 * history back to 1749 — and SSN tracks the solar flux closely enough that
 * showing both would spend a fetch to say the same thing twice.
 *
 * Every endpoint used here is deliberately one of the compact "summary"
 * products. The full-resolution equivalents are unusable on this hardware:
 * xrays-1-day.json is 655 KB and ovation_aurora_latest.json is 918 KB, against
 * 47 bytes for the solar flux summary.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPACEWX_KP_PERIODS   24   /* 3 days x 8 three-hour periods */
#define SPACEWX_DISCUSSION   240

typedef struct {
    /* Current indices */
    int   sfi;            /* 10.7 cm solar flux */
    char  xray[8];        /* strongest class today, e.g. "C6.3" */
    float kp;             /* planetary K, most recent 3-hour period */
    int   a_index;        /* running A */
    int   wind_speed;     /* solar wind, km/s */
    float bt;             /* IMF magnitude, nT */
    float bz;             /* IMF north-south component, nT. Negative is
                           * southward, which couples to Earth's field and
                           * drives activity — the sign carries the meaning. */

    /* NOAA R/S/G scales, current */
    int r_scale;
    int s_scale;
    int g_scale;

    /* Worst geomagnetic scale forecast over the next three days, and when */
    int  g_forecast;
    char g_forecast_date[12];   /* "2026-08-02" */

    /* Kp forecast, 3 days x 8 periods, index 0 = day 1 00-03 UT */
    float kp_forecast[SPACEWX_KP_PERIODS];
    char  forecast_days[3][8];  /* "Aug 01" */

    /* NOAA's own plain-English rationale — no model of ours explains a CME
     * better than the forecaster who wrote it. */
    char discussion[SPACEWX_DISCUSSION];

    time_t updated;
    bool   valid;
} spacewx_t;

/**
 * @brief Refresh everything. Partial success is still success.
 *
 * Each endpoint is fetched independently; one failing leaves its fields at
 * their previous values rather than discarding the whole update. Returns
 * ESP_OK if at least the core indices were obtained.
 */
esp_err_t spacewx_fetch(spacewx_t *out);

/** @brief Human text for a K index, e.g. "quiet", "unsettled", "storm". */
const char *spacewx_kp_text(float kp);

#ifdef __cplusplus
}
#endif
