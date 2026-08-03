/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "lib/station.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "station";

#define NVS_NS      "station"
#define KEY_CALL    "call"
#define KEY_QTH     "qth"
#define KEY_LOCATOR "loc"

static station_t s_st;
static char s_field[3] = "JO";

void station_init(void)
{
    memset(&s_st, 0, sizeof(s_st));

    nvs_handle_t h;
    /* No namespace yet just means commissioning has not been run. */
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "not configured yet");
        return;
    }

    size_t n = sizeof(s_st.call);
    esp_err_t e1 = nvs_get_str(h, KEY_CALL, s_st.call, &n);
    n = sizeof(s_st.qth);
    nvs_get_str(h, KEY_QTH, s_st.qth, &n);
    n = sizeof(s_st.locator);
    esp_err_t e2 = nvs_get_str(h, KEY_LOCATOR, s_st.locator, &n);
    nvs_close(h);

    if (e1 == ESP_OK && e2 == ESP_OK && geo_from_locator(s_st.locator, &s_st.pos)) {
        s_st.configured = true;
        memcpy(s_field, s_st.locator, 2);
        s_field[2] = '\0';
        ESP_LOGI(TAG, "%s at %s (%s) %.4f %.4f", s_st.call, s_st.locator, s_st.qth,
                 s_st.pos.lat, s_st.pos.lon);
    } else {
        ESP_LOGW(TAG, "stored settings incomplete, commissioning required");
        s_st.configured = false;
    }
}

esp_err_t station_clear(void)
{
    memset(&s_st, 0, sizeof(s_st));
    strcpy(s_field, "JO");

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(h, KEY_CALL);
    nvs_erase_key(h, KEY_QTH);
    nvs_erase_key(h, KEY_LOCATOR);
    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "station cleared");
    return err;
}

const station_t *station_get(void)
{
    return &s_st;
}

const char *station_grid_field(void)
{
    return s_field;
}

esp_err_t station_set(const char *call, const char *qth, const char *locator)
{
    if (!call || !locator || !call[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    geo_point_t p;
    if (!geo_from_locator(locator, &p)) {
        ESP_LOGE(TAG, "invalid locator '%s'", locator);
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(s_st.call, call, sizeof(s_st.call));
    strlcpy(s_st.qth, qth ? qth : "", sizeof(s_st.qth));
    strlcpy(s_st.locator, locator, sizeof(s_st.locator));
    /*
     * Stored exactly as typed.
     *
     * Both fields used to be case-normalised — the callsign to capitals, the
     * locator to the conventional JO30xp form. It was cosmetic, and it was
     * confusing: characters entered in capitals came back lower case with no
     * explanation. Maidenhead is case-insensitive and geo_from_locator()
     * uppercases internally before parsing, so nothing downstream depends on
     * the stored form.
     *
     * The grid field below is the exception, and stays capitalised: it is not
     * shown to anyone, it is the two characters sent to wspr.live.
     */
    s_st.pos = p;
    s_st.configured = true;

    memcpy(s_field, s_st.locator, 2);
    s_field[2] = '\0';
    for (int i = 0; i < 2; i++) {
        s_field[i] = (char)toupper((unsigned char)s_field[i]);
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_CALL, s_st.call);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_QTH, s_st.qth);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_LOCATOR, s_st.locator);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    ESP_LOGI(TAG, "saved %s / %s / %s -> %.4f %.4f", s_st.call, s_st.qth, s_st.locator,
             p.lat, p.lon);
    return err;
}

const char *station_continent(void)
{
    if (!s_st.configured) {
        return "EU";
    }
    const float lat = s_st.pos.lat;
    const float lon = s_st.pos.lon;

    if (lat < -60.0f) {
        return "AN";
    }
    if (lon >= -170.0f && lon < -30.0f) {
        /* The Americas: the split runs roughly along the Panama isthmus. */
        return (lat >= 13.0f) ? "NA" : "SA";
    }
    if (lon >= -30.0f && lon < 40.0f) {
        /* Europe above the Mediterranean, Africa below it. */
        return (lat >= 35.0f) ? "EU" : "AF";
    }
    if (lon >= 40.0f && lon < 180.0f) {
        if (lat < -10.0f) {
            return "OC";
        }
        /* European Russia reaches to the Urals at about 60 degrees east. */
        return (lat >= 40.0f && lon < 60.0f) ? "EU" : "AS";
    }
    return "OC";
}
