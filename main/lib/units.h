/*
 * Unit and format conversion for everything the operator reads.
 *
 * The device measures in SI and converts only at the moment of display, so no
 * stored value ever depends on the current setting — flipping EU to USA in
 * Settings changes what is drawn on the next repaint and nothing else.
 *
 * The degree sign is U+00B0, which the UI fonts carry at every size from 12 to
 * 28 (tools/gen_fonts.sh includes 0xB0 explicitly). Celsius shows the ring
 * alone because the audience for a Celsius reading never needs telling which
 * scale it is; Fahrenheit keeps its letter because that audience does.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "lib/prefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief "EU", "UK", "USA" — the labels used on the buttons. */
const char *units_name(unit_system_t u);

bool units_clock_12h(void);
bool units_fahrenheit(void);
bool units_miles(void);

/** @brief Convert a stored SI value into the display system. */
float units_temp(float celsius);
float units_distance(float km);
float units_speed(float metres_per_second);

/** @brief Suffixes to print after the converted value. */
const char *units_temp_suffix(void);      /* "°" or "°F" */
const char *units_distance_suffix(void);  /* "km" or "mi" */
const char *units_speed_suffix(void);     /* "m/s" or "mph" */

/**
 * @brief Clock time in the chosen convention.
 *
 * 24-hour systems get "14:05" or "14:05:32"; the USA gets "2:05 pm". The am/pm
 * suffix is lower case because it sits beside large numerals, where capitals
 * fight the digits for attention.
 */
void units_format_time(char *buf, size_t n, const struct tm *tm, bool with_seconds);

/** @brief Full date: "02.08.2026", "02/08/2026" or "08/02/2026". */
void units_format_date(char *buf, size_t n, const struct tm *tm);

/** @brief Date without the year, for forecast columns: "02.08" or "08/02". */
void units_format_date_short(char *buf, size_t n, int day, int month);

#ifdef __cplusplus
}
#endif
