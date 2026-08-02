/*
 * Maidenhead locators and great-circle geometry.
 *
 * Pure functions, no dependencies beyond libm — deliberately, so they can be
 * unit-tested on the host (see tools/test_geo.c) rather than only on target.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float lat;
    float lon;
} geo_point_t;

/**
 * @brief Locator ("JO30xp") to the centre of its square.
 *
 * Accepts 4 or 6 characters, case-insensitive.
 * @return false if the locator is malformed.
 */
bool geo_from_locator(const char *loc, geo_point_t *out);

/**
 * @brief Coordinates to a 6-character locator.
 *
 * @param out Buffer of at least 7 bytes.
 */
void geo_to_locator(geo_point_t p, char *out, size_t out_sz);

/** @brief Initial great-circle bearing from @p a to @p b, degrees true. */
float geo_bearing(geo_point_t a, geo_point_t b);

/** @brief Great-circle distance in kilometres. */
float geo_distance_km(geo_point_t a, geo_point_t b);

#ifdef __cplusplus
}
#endif
