/*
 * Solar geometry: subsolar point, terminator, twilight and grey line.
 *
 * Everything here is computed from UTC alone — no network, no ephemeris tables.
 * Accuracy is roughly a minute for sunrise/sunset, which is far better than the
 * display needs and costs a few dozen flops.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "lib/geo.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sunrise and sunset are defined as the moment the sun's UPPER LIMB touches the
 * horizon, not its centre, and atmospheric refraction lifts the apparent disc
 * by roughly 34 arcminutes. Together that puts the true geometric elevation at
 * -0.833 deg. Using 0 deg instead makes sunrise about six minutes late and
 * sunset six minutes early at mid latitudes — a symmetric error that is small
 * but wrong, and grey-line timing depends on it.
 */
#define SOLAR_RISESET_ELEV_DEG (-0.833f)

/* Angular distance from the subsolar point at which the sun sits at each
 * twilight boundary. 90 deg is the terminator itself. */
#define SOLAR_TERMINATOR_DEG   90.0f
#define SOLAR_CIVIL_DEG        96.0f
#define SOLAR_NAUTICAL_DEG    102.0f
#define SOLAR_ASTRO_DEG       108.0f

/**
 * @brief struct tm (UTC) to time_t.
 *
 * ESP-IDF's newlib does not expose timegm() without feature macros, and mktime()
 * applies the local timezone. This is the standard days-from-civil algorithm,
 * valid for any year the platform can represent.
 */
time_t solar_timegm(const struct tm *tm);

/** @brief Point on Earth where the sun is directly overhead at time @p t. */
geo_point_t solar_subsolar(time_t t);

/** @brief Sun elevation in degrees at a place and time. Negative is below horizon. */
float solar_elevation(geo_point_t p, time_t t);

static inline bool solar_is_daylight(geo_point_t p, time_t t)
{
    return solar_elevation(p, t) > 0.0f;
}

/**
 * @brief Locus of points a given angular distance from the subsolar point.
 *
 * Used to draw the terminator and the twilight bands. Fills @p out with
 * @p count evenly spaced points around the circle.
 */
void solar_ring(time_t t, float dist_deg, geo_point_t *out, int count);

/**
 * @brief Sunrise and sunset for a place on the UTC day containing @p t.
 *
 * @return false during polar day or night, when neither event occurs.
 */
bool solar_rise_set(geo_point_t p, time_t t, time_t *rise, time_t *set);

/**
 * @brief Seconds until the sun next passes through the grey-line window.
 *
 * The grey line is the period around sunrise and sunset when the terminator
 * crosses the path — the low bands lift briefly. Returns 0 if it is happening
 * now, or -1 if the sun does not cross the horizon today.
 */
long solar_seconds_to_greyline(geo_point_t p, time_t t);

#ifdef __cplusplus
}
#endif
