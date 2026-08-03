/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "net/propagation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lib/solar.h"
#include "net/http_get.h"

static const char *TAG = "prop";

#define WSPR_HOST "https://db1.wspr.live/?query="
#define KC2G_URL  "https://prop.kc2g.com/api/stations.json"

/* An ionosonde entry older than this is stale. Dead stations are never removed
 * from the feed — one has been reporting the same 2024 measurement for years. */
#define MUF_MAX_AGE_MIN 180

/*
 * Only bands wspr.live carries usable data for from mid-latitudes. 4m and
 * 2200m were dropped after measurement: 4m returned 16 spots across 4 grids
 * worldwide in 24 hours, which is not a map.
 *
 * The good/fair/poor thresholds are grid counts over one hour, set from
 * observed activity. They are first estimates and want tuning against a rolling
 * per-band median — 25 grids is remarkable on 6m and dire on 20m.
 */
static const prop_band_t k_bands[PROP_BAND_COUNT] = {
    {"630m",  0,  20,   8,  2},
    {"160m",  1,  50,  20,  5},
    {"80m",   3,  90,  40, 10},
    {"60m",   5,  70,  30,  8},
    {"40m",   7, 150,  70, 20},
    {"30m",  10, 140,  60, 15},
    {"20m",  14, 180,  80, 20},
    {"17m",  18, 140,  60, 15},
    {"15m",  21, 140,  60, 15},
    {"12m",  24,  80,  35, 10},
    {"10m",  28,  90,  40, 10},
    {"6m",   50,  25,  10,  3},
};

const prop_band_t *prop_band(int index)
{
    if (index < 0 || index >= PROP_BAND_COUNT) {
        return NULL;
    }
    return &k_bands[index];
}

const char *prop_cond_text(prop_cond_t c)
{
    switch (c) {
    case PROP_GOOD: return "Good";
    case PROP_FAIR: return "Fair";
    case PROP_POOR: return "Poor";
    default:        return "Closed";
    }
}

static prop_cond_t classify(const prop_band_t *b, int grids)
{
    if (grids >= b->good) {
        return PROP_GOOD;
    }
    if (grids >= b->fair) {
        return PROP_FAIR;
    }
    if (grids >= b->poor) {
        return PROP_POOR;
    }
    return PROP_CLOSED;
}

/** @brief Percent-encode everything outside the unreserved set. */
static size_t url_encode(const char *in, char *out, size_t out_sz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            if (n + 2 > out_sz) {
                return 0;
            }
            out[n++] = (char)*p;
        } else {
            if (n + 4 > out_sz) {
                return 0;
            }
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 0x0F];
        }
    }
    out[n] = '\0';
    return n;
}

/** @brief Run a ClickHouse query and return the parsed `data` array. */
static cJSON *wspr_query(const char *sql, cJSON **root_out)
{
    char enc[1200];
    if (url_encode(sql, enc, sizeof(enc)) == 0) {
        ESP_LOGE(TAG, "query too long to encode");
        return NULL;
    }

    char url[1400];
    int n = snprintf(url, sizeof(url), "%s%s", WSPR_HOST, enc);
    if (n <= 0 || n >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "url overflow");
        return NULL;
    }

    char *body = NULL;
    if (http_get_body(url, &body, NULL) != ESP_OK) {
        return NULL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGW(TAG, "unparseable response");
        return NULL;
    }
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return NULL;
    }
    *root_out = root;
    return data;
}

esp_err_t prop_fetch_conditions(const char *grid_field, prop_band_state_t out[PROP_BAND_COUNT])
{
    if (!grid_field || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(prop_band_state_t) * PROP_BAND_COUNT);

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT band, uniq(substring(rx_loc,1,4)), round(avg(snr)) "
             "FROM wspr.rx WHERE time > subtractHours(now(),1) "
             "AND substring(tx_loc,1,2)='%.2s' GROUP BY band FORMAT JSONCompact",
             grid_field);

    cJSON *root = NULL;
    cJSON *data = wspr_query(sql, &root);
    if (!data) {
        return ESP_FAIL;
    }

    int matched = 0;
    const cJSON *row = NULL;
    cJSON_ArrayForEach(row, data)
    {
        const cJSON *key = cJSON_GetArrayItem(row, 0);
        const cJSON *grids = cJSON_GetArrayItem(row, 1);
        const cJSON *snr = cJSON_GetArrayItem(row, 2);
        if (!cJSON_IsNumber(key) || !cJSON_IsNumber(grids)) {
            continue;
        }
        for (int i = 0; i < PROP_BAND_COUNT; i++) {
            if (k_bands[i].key == key->valueint) {
                out[i].grids = grids->valueint;
                out[i].snr_avg = cJSON_IsNumber(snr) ? (int)snr->valuedouble : 0;
                out[i].cond = classify(&k_bands[i], out[i].grids);
                matched++;
                break;
            }
        }
    }
    cJSON_Delete(root);

    /* Bands absent from the reply had no spots at all, which is itself a
     * result: they stay at zero grids and classify as closed. */
    ESP_LOGI(TAG, "conditions: %d of %d bands active", matched, PROP_BAND_COUNT);
    return ESP_OK;
}

esp_err_t prop_fetch_map(const char *grid_field, int band_key, prop_spot_t *out, int max,
                         int *out_count)
{
    if (!grid_field || !out || !out_count || max <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT substring(rx_loc,1,4) AS g, round(avg(snr)) FROM wspr.rx "
             "WHERE time > subtractHours(now(),3) AND band=%d "
             "AND substring(tx_loc,1,2)='%.2s' GROUP BY g HAVING count()>1 "
             "ORDER BY count() DESC LIMIT %d FORMAT JSONCompact",
             band_key, grid_field, max);

    cJSON *root = NULL;
    cJSON *data = wspr_query(sql, &root);
    if (!data) {
        return ESP_FAIL;
    }

    int n = 0;
    const cJSON *row = NULL;
    cJSON_ArrayForEach(row, data)
    {
        if (n >= max) {
            break;
        }
        const cJSON *g = cJSON_GetArrayItem(row, 0);
        const cJSON *snr = cJSON_GetArrayItem(row, 1);
        if (!cJSON_IsString(g) || !g->valuestring) {
            continue;
        }
        geo_point_t p;
        if (!geo_from_locator(g->valuestring, &p)) {
            continue;
        }
        out[n].pos = p;
        out[n].snr = cJSON_IsNumber(snr) ? (int8_t)snr->valuedouble : 0;
        n++;
    }
    cJSON_Delete(root);

    *out_count = n;
    ESP_LOGI(TAG, "map: %d grids on band key %d", n, band_key);
    return ESP_OK;
}

esp_err_t prop_fetch_muf(geo_point_t qth, prop_muf_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->valid = false;

    /* This response is the largest the device fetches — about 42 KB of JSON for
     * a hundred stations — so it is the first thing to fail when memory tightens.
     * Log which step gave up, rather than reporting a bare "MUF unavailable"
     * that could equally mean the network, the parser or a stale feed. */
    char *body = NULL;
    size_t body_len = 0;
    esp_err_t err = http_get_body(KC2G_URL, &body, &body_len);
    if (err != ESP_OK || !body) {
        ESP_LOGW(TAG, "ionosonde fetch failed: %s (largest free block %u)",
                 esp_err_to_name(err),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "ionosonde JSON unparseable (%u bytes)", (unsigned)body_len);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    time_t now = time(NULL);
    float best_km = 1e9f;
    const cJSON *st = NULL;

    cJSON_ArrayForEach(st, root)
    {
        const cJSON *station = cJSON_GetObjectItemCaseSensitive(st, "station");
        const cJSON *mufd = cJSON_GetObjectItemCaseSensitive(st, "mufd");
        const cJSON *tstr = cJSON_GetObjectItemCaseSensitive(st, "time");
        if (!cJSON_IsObject(station) || !cJSON_IsNumber(mufd)) {
            continue;
        }

        /* Reject stale measurements — see MUF_MAX_AGE_MIN. */
        int age = 0;
        if (cJSON_IsString(tstr) && tstr->valuestring && now > 1600000000) {
            struct tm tm = {0};
            if (sscanf(tstr->valuestring, "%d-%d-%dT%d:%d", &tm.tm_year, &tm.tm_mon,
                       &tm.tm_mday, &tm.tm_hour, &tm.tm_min) == 5) {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                age = (int)((now - solar_timegm(&tm)) / 60);
                if (age < 0 || age > MUF_MAX_AGE_MIN) {
                    continue;
                }
            }
        }

        const cJSON *la = cJSON_GetObjectItemCaseSensitive(station, "latitude");
        const cJSON *lo = cJSON_GetObjectItemCaseSensitive(station, "longitude");
        if (!la || !lo) {
            continue;
        }
        geo_point_t p = {
            .lat = (float)atof(cJSON_IsString(la) ? la->valuestring : ""),
            .lon = (float)atof(cJSON_IsString(lo) ? lo->valuestring : ""),
        };
        if (cJSON_IsNumber(la)) {
            p.lat = (float)la->valuedouble;
        }
        if (cJSON_IsNumber(lo)) {
            p.lon = (float)lo->valuedouble;
        }
        if (p.lon > 180.0f) {
            p.lon -= 360.0f;   /* the feed uses 0..360 */
        }

        float km = geo_distance_km(qth, p);
        if (km < best_km) {
            best_km = km;
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(station, "name");
            const cJSON *fof2 = cJSON_GetObjectItemCaseSensitive(st, "fof2");
            strlcpy(out->station,
                    (cJSON_IsString(name) && name->valuestring) ? name->valuestring : "?",
                    sizeof(out->station));
            out->muf = (float)mufd->valuedouble;
            out->fof2 = cJSON_IsNumber(fof2) ? (float)fof2->valuedouble : 0.0f;
            out->distance_km = km;
            out->age_min = age;
            out->valid = true;
        }
    }
    cJSON_Delete(root);

    if (out->valid) {
        ESP_LOGI(TAG, "MUF %.1f MHz from %s, %.0f km, %d min old", out->muf, out->station,
                 out->distance_km, out->age_min);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "no ionosonde within %d min of now in %d entries",
             MUF_MAX_AGE_MIN, cJSON_GetArraySize(root));
    return ESP_ERR_NOT_FOUND;
}
