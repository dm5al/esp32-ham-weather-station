/*
 * Operator identity and location.
 *
 * The callsign is display-only. It is shown on screen and stored in NVS, and it
 * is never placed in a URL, header, query or login — see PRIVACY in the README.
 * Propagation queries use the grid FIELD (first two characters of the locator),
 * which covers roughly a thousand kilometres and identifies a region rather
 * than a station.
 *
 * Location is entered by hand in the commissioning assistant. That is both more
 * private and more accurate than IP geolocation, which is imprecise and fails
 * entirely behind a VPN.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lib/geo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATION_CALL_MAX 12
#define STATION_QTH_MAX  32
#define STATION_LOC_MAX  8

typedef struct {
    char        call[STATION_CALL_MAX];
    char        qth[STATION_QTH_MAX];    /* free text, e.g. "Bad Marienberg" */
    char        locator[STATION_LOC_MAX];
    geo_point_t pos;                     /* derived from the locator */
    bool        configured;
} station_t;

/** @brief Load from NVS. Leaves .configured false if commissioning is pending. */
void station_init(void);

/** @brief Current settings. Never NULL. */
const station_t *station_get(void);

/**
 * @brief Store identity and location.
 *
 * The locator is validated and the position derived from it; an invalid locator
 * is rejected rather than silently accepted, since every bearing on the device
 * depends on it.
 */
esp_err_t station_set(const char *call, const char *qth, const char *locator);

/**
 * @brief Forget the stored identity, forcing commissioning on next boot.
 *
 * Separate from station_set() because an empty locator is correctly rejected by
 * validation — clearing has to be its own intent, not a degenerate save.
 */
esp_err_t station_clear(void);

/** @brief Grid field ("JO") for propagation queries — never the callsign. */
const char *station_grid_field(void);

/**
 * @brief Rough continent code for the QTH: "EU", "NA", "SA", "AF", "AS", "OC".
 *
 * Coarse latitude/longitude boxes, not a political lookup. It exists only to
 * answer "is this spot on my continent", where being wrong within a few hundred
 * kilometres of a boundary costs nothing. Derived locally from the locator the
 * operator typed, so it leaks nothing.
 */
const char *station_continent(void);

#ifdef __cplusplus
}
#endif
