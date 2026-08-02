#include "lib/geo.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EARTH_R_KM 6371.0

static double rad(double d)
{
    return d * M_PI / 180.0;
}

static double deg(double r)
{
    return r * 180.0 / M_PI;
}

bool geo_from_locator(const char *loc, geo_point_t *out)
{
    if (!loc || !out) {
        return false;
    }
    size_t n = strlen(loc);
    if (n != 4 && n != 6) {
        return false;
    }

    char c[6];
    for (size_t i = 0; i < n; i++) {
        c[i] = (char)toupper((unsigned char)loc[i]);
    }
    if (c[0] < 'A' || c[0] > 'R' || c[1] < 'A' || c[1] > 'R') {
        return false;
    }
    if (!isdigit((unsigned char)c[2]) || !isdigit((unsigned char)c[3])) {
        return false;
    }

    double lon = (c[0] - 'A') * 20.0 - 180.0 + (c[2] - '0') * 2.0;
    double lat = (c[1] - 'A') * 10.0 - 90.0 + (c[3] - '0') * 1.0;

    if (n == 6) {
        if (c[4] < 'A' || c[4] > 'X' || c[5] < 'A' || c[5] > 'X') {
            return false;
        }
        lon += (c[4] - 'A') * (2.0 / 24.0) + (1.0 / 24.0);
        lat += (c[5] - 'A') * (1.0 / 24.0) + (0.5 / 24.0);
    } else {
        /* Centre of the square when no subsquare was given. */
        lon += 1.0;
        lat += 0.5;
    }

    out->lat = (float)lat;
    out->lon = (float)lon;
    return true;
}

void geo_to_locator(geo_point_t p, char *out, size_t out_sz)
{
    if (!out || out_sz < 7) {
        return;
    }
    double lon = fmod(p.lon + 180.0, 360.0);
    double lat = p.lat + 90.0;
    if (lon < 0) {
        lon += 360.0;
    }
    if (lat < 0) {
        lat = 0;
    }
    if (lat > 180.0) {
        lat = 180.0;
    }

    int f1 = (int)(lon / 20.0);
    int f2 = (int)(lat / 10.0);
    lon -= f1 * 20.0;
    lat -= f2 * 10.0;

    int s1 = (int)(lon / 2.0);
    int s2 = (int)lat;
    lon -= s1 * 2.0;
    lat -= s2;

    int u1 = (int)(lon * 12.0);   /* 24 subsquares across 2 degrees */
    int u2 = (int)(lat * 24.0);

    snprintf(out, out_sz, "%c%c%d%d%c%c", 'A' + f1, 'A' + f2, s1, s2, 'a' + u1, 'a' + u2);
}

float geo_bearing(geo_point_t a, geo_point_t b)
{
    double p1 = rad(a.lat), p2 = rad(b.lat);
    double dl = rad(b.lon - a.lon);
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double br = deg(atan2(y, x));
    if (br < 0) {
        br += 360.0;
    }
    return (float)br;
}

float geo_distance_km(geo_point_t a, geo_point_t b)
{
    double p1 = rad(a.lat), p2 = rad(b.lat);
    double dp = p2 - p1;
    double dl = rad(b.lon - a.lon);
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    if (h > 1.0) {
        h = 1.0;
    }
    return (float)(2.0 * EARTH_R_KM * asin(sqrt(h)));
}
