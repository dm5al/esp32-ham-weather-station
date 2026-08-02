#include "lib/units.h"

#include <stdio.h>

#define KM_PER_MILE   1.609344f
#define MPH_PER_MS    2.2369363f

const char *units_name(unit_system_t u)
{
    switch (u) {
    case UNITS_UK: return "UK";
    case UNITS_US: return "USA";
    default:       return "EU";
    }
}

bool units_clock_12h(void)
{
    return prefs_units() == UNITS_US;
}

bool units_fahrenheit(void)
{
    return prefs_units() == UNITS_US;
}

bool units_miles(void)
{
    return prefs_units() != UNITS_EU;
}

float units_temp(float celsius)
{
    return units_fahrenheit() ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}

float units_distance(float km)
{
    return units_miles() ? km / KM_PER_MILE : km;
}

float units_speed(float metres_per_second)
{
    return units_miles() ? metres_per_second * MPH_PER_MS : metres_per_second;
}

const char *units_temp_suffix(void)
{
    return units_fahrenheit() ? "°F" : "°";
}

const char *units_distance_suffix(void)
{
    return units_miles() ? "mi" : "km";
}

const char *units_speed_suffix(void)
{
    return units_miles() ? "mph" : "m/s";
}

void units_format_time(char *buf, size_t n, const struct tm *tm, bool with_seconds)
{
    if (!buf || n == 0) {
        return;
    }
    if (!tm) {
        snprintf(buf, n, "--:--");
        return;
    }

    if (units_clock_12h()) {
        int h = tm->tm_hour % 12;
        if (h == 0) {
            h = 12;   /* midnight and noon are 12, not 0 */
        }
        const char *ap = tm->tm_hour < 12 ? "am" : "pm";
        if (with_seconds) {
            snprintf(buf, n, "%d:%02d:%02d %s", h, tm->tm_min, tm->tm_sec, ap);
        } else {
            snprintf(buf, n, "%d:%02d %s", h, tm->tm_min, ap);
        }
        return;
    }

    if (with_seconds) {
        snprintf(buf, n, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        snprintf(buf, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
    }
}

void units_format_date(char *buf, size_t n, const struct tm *tm)
{
    if (!buf || n == 0) {
        return;
    }
    if (!tm) {
        snprintf(buf, n, "--");
        return;
    }
    int d = tm->tm_mday;
    int m = tm->tm_mon + 1;
    int y = tm->tm_year + 1900;

    switch (prefs_units()) {
    case UNITS_UK: snprintf(buf, n, "%02d/%02d/%04d", d, m, y); break;
    case UNITS_US: snprintf(buf, n, "%02d/%02d/%04d", m, d, y); break;
    default:       snprintf(buf, n, "%02d.%02d.%04d", d, m, y); break;
    }
}

void units_format_date_short(char *buf, size_t n, int day, int month)
{
    if (!buf || n == 0) {
        return;
    }
    switch (prefs_units()) {
    case UNITS_UK: snprintf(buf, n, "%02d/%02d", day, month); break;
    case UNITS_US: snprintf(buf, n, "%02d/%02d", month, day); break;
    default:       snprintf(buf, n, "%02d.%02d", day, month); break;
    }
}
