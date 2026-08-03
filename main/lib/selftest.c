/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "lib/selftest.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "lib/i18n.h"
#include "lib/prefs.h"
#include "lib/units.h"
#include "lib/geo.h"
#include "lib/solar.h"

static const char *TAG = "selftest";

static int s_fails;

static void check(const char *what, bool cond, const char *detail)
{
    if (cond) {
        ESP_LOGI(TAG, "PASS  %-42s %s", what, detail ? detail : "");
    } else {
        ESP_LOGE(TAG, "FAIL  %-42s %s", what, detail ? detail : "");
        s_fails++;
    }
}

static void near(const char *what, double got, double want, double tol)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "got %.3f want %.3f +/-%g", got, want, tol);
    check(what, fabs(got - want) <= tol, buf);
}

int selftest_run(void)
{
    s_fails = 0;
    ESP_LOGI(TAG, "==== locator ====");

    geo_point_t p;
    check("JO30xp parses", geo_from_locator("JO30xp", &p), NULL);
    near("  latitude", p.lat, 50.65, 0.05);
    near("  longitude", p.lon, 7.95, 0.05);

    char loc[8];
    geo_to_locator(p, loc, sizeof(loc));
    check("JO30xp round-trips", strncmp(loc, "JO30xp", 6) == 0, loc);

    geo_point_t ny = {40.75f, -73.97f};
    geo_to_locator(ny, loc, sizeof(loc));
    check("New York is FN30", strncmp(loc, "FN30", 4) == 0, loc);
    check("rejects garbage", !geo_from_locator("ZZ99", &p), NULL);
    check("rejects short", !geo_from_locator("JO3", &p), NULL);

    ESP_LOGI(TAG, "==== great circle ====");
    geo_point_t qth = {50.6495f, 7.9496f};
    geo_point_t zl6b = {-41.05f, 175.58f};
    geo_point_t w6wx = {37.15f, -121.90f};

    near("ZL6B bearing", geo_bearing(qth, zl6b), 47.0, 2.0);
    near("ZL6B distance km", geo_distance_km(qth, zl6b), 18585.0, 60.0);
    near("W6WX bearing", geo_bearing(qth, w6wx), 322.0, 2.0);
    near("W6WX distance km", geo_distance_km(qth, w6wx), 9092.0, 60.0);
    near("distance to self", geo_distance_km(qth, qth), 0.0, 0.001);

    ESP_LOGI(TAG, "==== solar ====");
    /* 2026-08-01 16:04 UTC — the instant the design mockups depict. */
    struct tm tm = {0};
    tm.tm_year = 126;
    tm.tm_mon = 7;
    tm.tm_mday = 1;
    tm.tm_hour = 16;
    tm.tm_min = 4;
    time_t t = solar_timegm(&tm);

    geo_point_t sub = solar_subsolar(t);
    near("subsolar latitude (early Aug)", sub.lat, 18.0, 1.5);
    near("subsolar longitude", sub.lon, -61.0, 2.0);
    near("elevation at subsolar point", solar_elevation(sub, t), 90.0, 0.5);

    check("QTH in daylight at 18:04 local", solar_is_daylight(qth, t), NULL);
    geo_point_t npole = {89.0f, 0.0f};
    geo_point_t spole = {-89.0f, 0.0f};
    check("north pole in permanent day", solar_is_daylight(npole, t), NULL);
    check("south pole in permanent night", !solar_is_daylight(spole, t), NULL);

    geo_point_t ring[73];
    solar_ring(t, SOLAR_TERMINATOR_DEG, ring, 73);
    double worst = 0;
    for (int i = 0; i < 73; i++) {
        double e = fabs(solar_elevation(ring[i], t));
        if (e > worst) {
            worst = e;
        }
    }
    near("terminator ring on horizon", worst, 0.0, 0.2);

    solar_ring(t, SOLAR_CIVIL_DEG, ring, 73);
    near("civil twilight ring elevation", solar_elevation(ring[0], t), -6.0, 0.2);

    ESP_LOGI(TAG, "==== sunrise / sunset ====");
    time_t rise = 0, set = 0;
    check("rise/set found", solar_rise_set(qth, t, &rise, &set), NULL);
    struct tm rt, st;
    gmtime_r(&rise, &rt);
    gmtime_r(&set, &st);
    char buf[64];
    snprintf(buf, sizeof(buf), "rise %02d:%02dZ set %02d:%02dZ", rt.tm_hour, rt.tm_min,
             st.tm_hour, st.tm_min);
    /* Bad Marienberg, 1 Aug 2026: 05:55 / 21:13 local = 03:55Z / 19:13Z, with
     * the upper limb at the refracted horizon. Tight tolerance on purpose —
     * this is what caught the missing refraction correction. */
    check("sunrise near 03:55Z", rt.tm_hour == 3 && abs(rt.tm_min - 55) <= 4, buf);
    check("sunset near 19:13Z", st.tm_hour == 19 && abs(st.tm_min - 13) <= 4, buf);

    long gl = solar_seconds_to_greyline(qth, t);
    snprintf(buf, sizeof(buf), "%ld s = %.1f h", gl, gl / 3600.0);
    check("grey line ahead within 12 h", gl > 0 && gl < 12 * 3600, buf);

    ESP_LOGI(TAG, "==== units and formats ====");
    /* Conversions and the 12-hour clock are pure arithmetic with well-known
     * answers, which makes them worth pinning: a sign or factor error here is
     * invisible on screen until someone who knows the right number looks. */
    unit_system_t saved = prefs_units();

    prefs_set_units(UNITS_EU);
    near("0 C stays 0 C", units_temp(0.0f), 0.0f, 0.01);
    near("100 km stays 100 km", units_distance(100.0f), 100.0f, 0.01);

    prefs_set_units(UNITS_US);
    near("0 C is 32 F", units_temp(0.0f), 32.0f, 0.01);
    near("100 C is 212 F", units_temp(100.0f), 212.0f, 0.01);
    near("-40 is the same either way", units_temp(-40.0f), -40.0f, 0.01);
    near("1609.344 m is 1 mile", units_distance(1.609344f), 1.0f, 0.001);
    near("1 m/s is 2.2369 mph", units_speed(1.0f), 2.2369363f, 0.001);

    /* Midnight and noon are the classic 12-hour off-by-twelve. */
    struct tm c = {.tm_hour = 0, .tm_min = 5, .tm_mday = 2, .tm_mon = 7, .tm_year = 126};
    units_format_time(buf, sizeof(buf), &c, false);
    check("00:05 is 12:05 am", strcmp(buf, "12:05 am") == 0, buf);
    c.tm_hour = 12;
    units_format_time(buf, sizeof(buf), &c, false);
    check("12:05 is 12:05 pm", strcmp(buf, "12:05 pm") == 0, buf);
    c.tm_hour = 13;
    units_format_time(buf, sizeof(buf), &c, false);
    check("13:05 is 1:05 pm", strcmp(buf, "1:05 pm") == 0, buf);

    units_format_date(buf, sizeof(buf), &c);
    check("USA date is month first", strcmp(buf, "08/02/2026") == 0, buf);
    prefs_set_units(UNITS_EU);
    units_format_date(buf, sizeof(buf), &c);
    check("EU date is day first", strcmp(buf, "02.08.2026") == 0, buf);
    units_format_time(buf, sizeof(buf), &c, true);
    check("EU clock is 24 hour", strcmp(buf, "13:05:00") == 0, buf);

    prefs_set_units(saved);

    ESP_LOGI(TAG, "==== language ====");
    lang_t saved_lang = prefs_lang();
    bool all_present = true;
    for (int l = 0; l < LANG_COUNT; l++) {
        prefs_set_lang((lang_t)l);
        for (int s = 0; s < S_COUNT; s++) {
            if (T((str_id_t)s)[0] == '\0') {
                ESP_LOGE(TAG, "empty string %d in language %d", s, l);
                all_present = false;
            }
        }
    }
    prefs_set_lang(saved_lang);
    snprintf(buf, sizeof(buf), "%d strings x %d languages", S_COUNT, LANG_COUNT);
    check("every string translated", all_present, buf);

    if (s_fails) {
        ESP_LOGE(TAG, "==== %d FAILURE(S) ====", s_fails);
    } else {
        ESP_LOGI(TAG, "==== ALL PASS ====");
    }
    return s_fails;
}
