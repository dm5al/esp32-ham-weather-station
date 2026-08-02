#include "lib/solar.h"

#include <math.h>

#define RAD (M_PI / 180.0)
#define DEG (180.0 / M_PI)

/* Grey line is treated as the sun within this many degrees of the horizon. */
#define GREYLINE_BAND_DEG 6.0f

/** @brief Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

time_t solar_timegm(const struct tm *tm)
{
    long days = days_from_civil(tm->tm_year + 1900, (unsigned)(tm->tm_mon + 1),
                                (unsigned)tm->tm_mday);
    return (time_t)(days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
}

/** @brief Days since J2000.0. */
static double julian_centuries(time_t t)
{
    return ((double)t / 86400.0 + 2440587.5 - 2451545.0) / 36525.0;
}

/**
 * @brief Sun declination and equation of time.
 *
 * Low-precision formulae from the Astronomical Almanac; good to about 0.01 deg
 * over several decades, which is well inside what a 800x480 panel can show.
 */
static void sun_position(time_t t, double *decl_deg, double *eot_min)
{
    double t_c = julian_centuries(t);

    double mean_lon = fmod(280.46646 + t_c * (36000.76983 + t_c * 0.0003032), 360.0);
    double mean_anom = 357.52911 + t_c * (35999.05029 - 0.0001537 * t_c);
    double ecc = 0.016708634 - t_c * (0.000042037 + 0.0000001267 * t_c);

    double m = mean_anom * RAD;
    double centre = sin(m) * (1.914602 - t_c * (0.004817 + 0.000014 * t_c))
                    + sin(2 * m) * (0.019993 - 0.000101 * t_c)
                    + sin(3 * m) * 0.000289;

    double true_lon = mean_lon + centre;
    double omega = 125.04 - 1934.136 * t_c;
    double app_lon = true_lon - 0.00569 - 0.00478 * sin(omega * RAD);

    double obliq = 23.0 + (26.0 + ((21.448 - t_c * (46.815 + t_c * (0.00059 - t_c * 0.001813))))
                                  / 60.0) / 60.0;
    double obliq_corr = obliq + 0.00256 * cos(omega * RAD);

    *decl_deg = asin(sin(obliq_corr * RAD) * sin(app_lon * RAD)) * DEG;

    double y = tan(obliq_corr / 2 * RAD);
    y *= y;
    double eot = y * sin(2 * mean_lon * RAD)
                 - 2 * ecc * sin(m)
                 + 4 * ecc * y * sin(m) * cos(2 * mean_lon * RAD)
                 - 0.5 * y * y * sin(4 * mean_lon * RAD)
                 - 1.25 * ecc * ecc * sin(2 * m);
    *eot_min = eot * 4 * DEG;
}

geo_point_t solar_subsolar(time_t t)
{
    double decl, eot;
    sun_position(t, &decl, &eot);

    struct tm tm;
    gmtime_r(&t, &tm);
    double utc_hours = tm.tm_hour + tm.tm_min / 60.0 + tm.tm_sec / 3600.0;

    /* Solar noon is displaced from 12:00 UTC by the equation of time. */
    double lon = -15.0 * (utc_hours + eot / 60.0 - 12.0);
    lon = fmod(lon + 540.0, 360.0) - 180.0;

    geo_point_t p = {.lat = (float)decl, .lon = (float)lon};
    return p;
}

float solar_elevation(geo_point_t p, time_t t)
{
    geo_point_t s = solar_subsolar(t);
    double la = p.lat * RAD, lo = p.lon * RAD;
    double sa = s.lat * RAD, so = s.lon * RAD;
    double cos_z = sin(la) * sin(sa) + cos(la) * cos(sa) * cos(lo - so);
    if (cos_z > 1.0) {
        cos_z = 1.0;
    }
    if (cos_z < -1.0) {
        cos_z = -1.0;
    }
    return (float)(90.0 - acos(cos_z) * DEG);
}

void solar_ring(time_t t, float dist_deg, geo_point_t *out, int count)
{
    if (!out || count < 2) {
        return;
    }
    geo_point_t s = solar_subsolar(t);
    double lat_s = s.lat * RAD, lon_s = s.lon * RAD;
    double d = dist_deg * RAD;

    for (int i = 0; i < count; i++) {
        double b = (2.0 * M_PI * i) / (count - 1);
        double lat = asin(sin(lat_s) * cos(d) + cos(lat_s) * sin(d) * cos(b));
        double lon = lon_s + atan2(sin(b) * sin(d) * cos(lat_s),
                                   cos(d) - sin(lat_s) * sin(lat));
        out[i].lat = (float)(lat * DEG);
        out[i].lon = (float)(fmod(lon * DEG + 540.0, 360.0) - 180.0);
    }
}

/**
 * @brief Scan the day for horizon crossings.
 *
 * A closed-form solution exists, but it needs careful handling of polar cases
 * and of the equation of time changing across the day. Sampling every four
 * minutes and refining by bisection is simpler to get right, costs under a
 * millisecond, and degrades gracefully at high latitudes.
 */
bool solar_rise_set(geo_point_t p, time_t t, time_t *rise, time_t *set)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    time_t day_start = solar_timegm(&tm);

    bool got_rise = false, got_set = false;
    const float horizon = SOLAR_RISESET_ELEV_DEG;
    float prev = solar_elevation(p, day_start);

    for (int i = 1; i <= 360; i++) {          /* 4-minute steps over 24 h */
        time_t tc = day_start + i * 240;
        float cur = solar_elevation(p, tc);

        if ((prev <= horizon) != (cur <= horizon)) {
            time_t lo = tc - 240, hi = tc;
            for (int k = 0; k < 12; k++) {    /* bisect to about a second */
                time_t mid = lo + (hi - lo) / 2;
                if ((solar_elevation(p, mid) <= horizon) == (prev <= horizon)) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            if (cur > horizon && !got_rise) {
                if (rise) {
                    *rise = hi;
                }
                got_rise = true;
            } else if (cur <= horizon && !got_set) {
                if (set) {
                    *set = hi;
                }
                got_set = true;
            }
        }
        prev = cur;
    }
    return got_rise || got_set;
}

long solar_seconds_to_greyline(geo_point_t p, time_t t)
{
    float elev = solar_elevation(p, t);
    if (fabsf(elev) <= GREYLINE_BAND_DEG) {
        return 0;
    }

    /* Walk forward in one-minute steps until the sun enters the band. A full
     * day of steps bounds the search; beyond that it is polar day or night. */
    for (long s = 60; s <= 36 * 3600; s += 60) {
        if (fabsf(solar_elevation(p, t + s)) <= GREYLINE_BAND_DEG) {
            return s;
        }
    }
    return -1;
}
