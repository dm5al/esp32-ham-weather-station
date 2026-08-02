#include "net/spacewx.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "net/http_get.h"

static const char *TAG = "spacewx";

#define URL_FLUX   "https://services.swpc.noaa.gov/products/summary/10cm-flux.json"
#define URL_WIND   "https://services.swpc.noaa.gov/products/summary/solar-wind-speed.json"
#define URL_MAG    "https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json"
#define URL_KP     "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json"
#define URL_SCALES "https://services.swpc.noaa.gov/products/noaa-scales.json"
#define URL_3DAY   "https://services.swpc.noaa.gov/text/3-day-forecast.txt"
/* Latest flare summary: 450 bytes, against 655 KB for the full flux series. */
#define URL_XRAY   "https://services.swpc.noaa.gov/json/goes/primary/xray-flares-latest.json"

/** @brief Fetch and parse one endpoint, logging but tolerating failure. */
static bool fetch_json(const char *url, cJSON **out)
{
    char *body = NULL;
    if (http_get_body(url, &body, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "fetch failed: %s", url);
        return false;
    }
    *out = cJSON_Parse(body);
    free(body);
    if (!*out) {
        ESP_LOGW(TAG, "unparseable JSON from %s", url);
        return false;
    }
    return true;
}

/* Summary products are a single-element array: [{"flux":148,...}] */
static bool summary_number(const char *url, const char *key, double *out)
{
    cJSON *root = NULL;
    if (!fetch_json(url, &root)) {
        return false;
    }
    bool ok = false;
    const cJSON *first = cJSON_GetArrayItem(root, 0);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(first, key);
    if (cJSON_IsNumber(v)) {
        *out = v->valuedouble;
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static bool fetch_kp_now(spacewx_t *s)
{
    cJSON *root = NULL;
    if (!fetch_json(URL_KP, &root)) {
        return false;
    }
    bool ok = false;
    int n = cJSON_GetArraySize(root);
    if (n > 0) {
        /* Most recent 3-hour period is last; it also carries the running A. */
        const cJSON *last = cJSON_GetArrayItem(root, n - 1);
        const cJSON *kp = cJSON_GetObjectItemCaseSensitive(last, "Kp");
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(last, "a_running");
        if (cJSON_IsNumber(kp)) {
            s->kp = (float)kp->valuedouble;
            ok = true;
        }
        if (cJSON_IsNumber(a)) {
            s->a_index = a->valueint;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static int scale_value(const cJSON *node, const char *letter)
{
    const cJSON *band = cJSON_GetObjectItemCaseSensitive(node, letter);
    const cJSON *sc = cJSON_GetObjectItemCaseSensitive(band, "Scale");
    /* NOAA encodes the scale as a string, and null when only a probability
     * is being forecast rather than a level. */
    if (cJSON_IsString(sc) && sc->valuestring) {
        return atoi(sc->valuestring);
    }
    return 0;
}

static bool fetch_xray(spacewx_t *s)
{
    cJSON *root = NULL;
    if (!fetch_json(URL_XRAY, &root)) {
        return false;
    }
    bool ok = false;
    const cJSON *first = cJSON_GetArrayItem(root, 0);
    /* max_class is the peak of the most recent flare; that is what operators
     * quote, and what a "today's X-ray" reading means in practice. */
    const cJSON *cls = cJSON_GetObjectItemCaseSensitive(first, "max_class");
    if (cJSON_IsString(cls) && cls->valuestring) {
        strlcpy(s->xray, cls->valuestring, sizeof(s->xray));
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static bool fetch_scales(spacewx_t *s)
{
    cJSON *root = NULL;
    if (!fetch_json(URL_SCALES, &root)) {
        return false;
    }
    /* Key "0" is now; "1".."3" are the forecast days. */
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(root, "0");
    if (now) {
        s->r_scale = scale_value(now, "R");
        s->s_scale = scale_value(now, "S");
        s->g_scale = scale_value(now, "G");
    }

    s->g_forecast = 0;
    s->g_forecast_date[0] = '\0';
    for (int i = 1; i <= 3; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", i);
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(root, key);
        if (!day) {
            continue;
        }
        int g = scale_value(day, "G");
        if (g > s->g_forecast) {
            s->g_forecast = g;
            const cJSON *d = cJSON_GetObjectItemCaseSensitive(day, "DateStamp");
            if (cJSON_IsString(d) && d->valuestring) {
                strlcpy(s->g_forecast_date, d->valuestring, sizeof(s->g_forecast_date));
            }
        }
    }
    cJSON_Delete(root);
    return now != NULL;
}

/**
 * @brief Pull the Kp table and rationale out of NOAA's plain-text bulletin.
 *
 * The layout has been stable for years:
 *
 *              Aug 01       Aug 02       Aug 03
 *   00-03UT     1.00         4.00         3.33
 *   ...
 *   Rationale: G1-G2 storms are likely ...
 */
static bool parse_3day(const char *text, spacewx_t *s)
{
    const char *tbl = strstr(text, "NOAA Kp index breakdown");
    if (!tbl) {
        return false;
    }

    /* Day headings sit on the line after the breakdown title. */
    const char *hdr = strchr(tbl, '\n');
    if (hdr) {
        hdr = strchr(hdr + 1, '\n');
    }
    if (hdr) {
        const char *p = hdr + 1;
        for (int d = 0; d < 3; d++) {
            while (*p == ' ') {
                p++;
            }
            int n = 0;
            /* A heading is "Mon DD"; stop at the run of spaces after it. */
            while (p[n] && p[n] != '\n' && !(p[n] == ' ' && p[n + 1] == ' ')) {
                n++;
            }
            if (n > 0 && n < (int)sizeof(s->forecast_days[0])) {
                memcpy(s->forecast_days[d], p, n);
                s->forecast_days[d][n] = '\0';
            }
            p += n;
            if (*p == '\n' || !*p) {
                break;
            }
        }
    }

    int filled = 0;
    const char *p = tbl;
    for (int period = 0; period < 8; period++) {
        /* Rows are labelled 00-03UT, 03-06UT ... 21-00UT. */
        char label[10];
        snprintf(label, sizeof(label), "%02d-%02dUT", period * 3, (period * 3 + 3) % 24);
        const char *row = strstr(p, label);
        if (!row) {
            continue;
        }
        const char *q = row + strlen(label);
        for (int day = 0; day < 3; day++) {
            char *end = NULL;
            double v = strtod(q, &end);
            if (end == q) {
                break;
            }
            s->kp_forecast[day * 8 + period] = (float)v;
            filled++;
            q = end;
            /* Skip a parenthesised storm label such as "(G2)". */
            while (*q == ' ') {
                q++;
            }
            if (*q == '(') {
                while (*q && *q != ')') {
                    q++;
                }
                if (*q) {
                    q++;
                }
            }
        }
    }

    const char *rat = strstr(text, "Rationale:");
    if (rat) {
        rat += strlen("Rationale:");
        while (*rat == ' ' || *rat == '\n') {
            rat++;
        }
        size_t n = 0;
        char *o = s->discussion;
        /* Unwrap the hard-wrapped paragraph into a single line, stopping at the
         * blank line that ends it. */
        while (*rat && n + 1 < sizeof(s->discussion)) {
            if (*rat == '\n') {
                if (rat[1] == '\n' || rat[1] == '\0') {
                    break;
                }
                o[n++] = ' ';
                rat++;
                while (*rat == ' ') {
                    rat++;
                }
                continue;
            }
            o[n++] = *rat++;
        }
        o[n] = '\0';
    }

    return filled > 0;
}

static bool fetch_3day(spacewx_t *s)
{
    char *body = NULL;
    if (http_get_body(URL_3DAY, &body, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "3-day forecast fetch failed");
        return false;
    }
    bool ok = parse_3day(body, s);
    free(body);
    return ok;
}

esp_err_t spacewx_fetch(spacewx_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    double v;
    bool core = false;

    if (summary_number(URL_FLUX, "flux", &v)) {
        out->sfi = (int)v;
        core = true;
    }
    if (fetch_kp_now(out)) {
        core = true;
    }
    if (summary_number(URL_WIND, "proton_speed", &v)) {
        out->wind_speed = (int)v;
    }
    if (summary_number(URL_MAG, "bt", &v)) {
        out->bt = (float)v;
    }
    if (summary_number(URL_MAG, "bz_gsm", &v)) {
        out->bz = (float)v;
    }

    fetch_xray(out);
    fetch_scales(out);
    fetch_3day(out);

    if (!core) {
        ESP_LOGE(TAG, "no core indices obtained");
        return ESP_FAIL;
    }

    out->updated = time(NULL);
    out->valid = true;
    ESP_LOGI(TAG, "SFI %d  Kp %.2f  A %d  X-ray %s  wind %d km/s  Bt %.0f Bz %.0f  R%d S%d G%d",
             out->sfi, out->kp, out->a_index, out->xray[0] ? out->xray : "-",
             out->wind_speed, out->bt, out->bz, out->r_scale, out->s_scale, out->g_scale);
    if (out->g_forecast > 0) {
        ESP_LOGI(TAG, "forecast G%d on %s", out->g_forecast, out->g_forecast_date);
    }
    return ESP_OK;
}

const char *spacewx_kp_text(float kp)
{
    if (kp < 2.0f) {
        return "quiet";
    }
    if (kp < 3.0f) {
        return "unsettled";
    }
    if (kp < 4.0f) {
        return "active";
    }
    if (kp < 5.0f) {
        return "minor storm";
    }
    if (kp < 6.0f) {
        return "moderate storm";
    }
    return "strong storm";
}
