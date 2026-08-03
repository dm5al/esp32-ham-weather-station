/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Host-side checks for lib/geo.c and lib/solar.c.
 *
 * These are pure maths with no ESP-IDF dependency, so they can be verified on a
 * PC in a second rather than by flashing and squinting at a panel.
 *
 *   gcc -I main tools/test_geo.c main/lib/geo.c main/lib/solar.c -lm -o test_geo
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/geo.h"
#include "lib/solar.h"

static int fails = 0;

static void ok(const char *what, int cond, const char *detail)
{
    printf("%-46s %s   %s\n", what, cond ? "PASS" : "FAIL", detail ? detail : "");
    if (!cond) {
        fails++;
    }
}

static void near(const char *what, double got, double want, double tol)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "got %.3f, want %.3f (+/- %g)", got, want, tol);
    ok(what, fabs(got - want) <= tol, buf);
}

int main(void)
{
    puts("--- locator round trip ---");
    geo_point_t p;
    ok("JO30xp parses", geo_from_locator("JO30xp", &p), NULL);
    near("  latitude", p.lat, 50.65, 0.05);
    near("  longitude", p.lon, 7.95, 0.05);

    char loc[8];
    geo_to_locator(p, loc, sizeof(loc));
    ok("JO30xp round-trips", strncmp(loc, "JO30xp", 6) == 0, loc);

    geo_point_t ny = {40.75f, -73.97f};
    geo_to_locator(ny, loc, sizeof(loc));
    ok("New York is FN30", strncmp(loc, "FN30", 4) == 0, loc);

    ok("rejects garbage", !geo_from_locator("ZZ99", &p), NULL);
    ok("rejects short", !geo_from_locator("JO3", &p), NULL);

    puts("\n--- great circle (JO30xp reference) ---");
    geo_point_t qth = {50.6495f, 7.9496f};
    geo_point_t zl6b = {-41.05f, 175.58f};
    geo_point_t w6wx = {37.15f, -121.90f};

    /* Cross-checked against the values computed for the mockups. */
    near("ZL6B bearing", geo_bearing(qth, zl6b), 47.0, 2.0);
    near("ZL6B distance km", geo_distance_km(qth, zl6b), 18585.0, 60.0);
    near("W6WX bearing", geo_bearing(qth, w6wx), 322.0, 2.0);
    near("W6WX distance km", geo_distance_km(qth, w6wx), 9092.0, 60.0);
    near("distance to self", geo_distance_km(qth, qth), 0.0, 0.001);

    puts("\n--- solar geometry ---");
    /* 2026-08-01 16:04 UTC, the instant the mockups depict. */
    struct tm tm = {0};
    tm.tm_year = 126; tm.tm_mon = 7; tm.tm_mday = 1;
    tm.tm_hour = 16; tm.tm_min = 4;
    time_t t = timegm(&tm);

    geo_point_t sub = solar_subsolar(t);
    near("subsolar latitude (early Aug)", sub.lat, 18.0, 1.5);
    near("subsolar longitude", sub.lon, -61.0, 2.0);

    ok("QTH is in daylight at 18:04 local", solar_is_daylight(qth, t), NULL);

    /* Northern summer: the Arctic is lit around the clock, Antarctica dark. */
    geo_point_t npole = {89.0f, 0.0f}, spole = {-89.0f, 0.0f};
    ok("north pole in permanent day", solar_is_daylight(npole, t), NULL);
    ok("south pole in permanent night", !solar_is_daylight(spole, t), NULL);

    /* Elevation at the subsolar point must be 90 deg by definition. */
    near("elevation at subsolar point", solar_elevation(sub, t), 90.0, 0.5);

    /* Every point on the 90 deg ring must sit on the horizon. */
    geo_point_t ring[73];
    solar_ring(t, SOLAR_TERMINATOR_DEG, ring, 73);
    double worst = 0;
    for (int i = 0; i < 73; i++) {
        double e = fabs(solar_elevation(ring[i], t));
        if (e > worst) {
            worst = e;
        }
    }
    near("terminator ring elevation", worst, 0.0, 0.2);

    solar_ring(t, SOLAR_CIVIL_DEG, ring, 73);
    near("civil twilight ring elevation", solar_elevation(ring[0], t), -6.0, 0.2);

    puts("\n--- sunrise / sunset ---");
    time_t rise = 0, set = 0;
    ok("rise/set found for JO30xp", solar_rise_set(qth, t, &rise, &set), NULL);
    struct tm rt, st;
    gmtime_r(&rise, &rt);
    gmtime_r(&set, &st);
    char buf[64];
    snprintf(buf, sizeof(buf), "rise %02d:%02dZ set %02d:%02dZ",
             rt.tm_hour, rt.tm_min, st.tm_hour, st.tm_min);
    /* Bad Marienberg on 1 Aug: about 03:50Z and 19:15Z (05:50 / 21:15 local). */
    ok("  sunrise near 03:50Z", rt.tm_hour == 3 && abs(rt.tm_min - 50) < 12, buf);
    ok("  sunset near 19:15Z", st.tm_hour == 19 && abs(st.tm_min - 15) < 12, buf);

    long gl = solar_seconds_to_greyline(qth, t);
    snprintf(buf, sizeof(buf), "%ld s = %.1f h", gl, gl / 3600.0);
    ok("grey line ahead, within 12 h", gl > 0 && gl < 12 * 3600, buf);

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASS", fails,
           fails == 1 ? "" : "s");
    return fails != 0;
}
