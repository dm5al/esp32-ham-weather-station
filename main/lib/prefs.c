#include "lib/prefs.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "prefs";

#define NVS_NS    "ui"
#define KEY_PROJ  "proj"
#define KEY_UNITS "units"
#define KEY_LANG  "lang"
#define KEY_DXF   "dxfilter"

static map_projection_t s_proj = MAP_AZIMUTHAL;
static unit_system_t s_units = UNITS_EU;
static lang_t s_lang = LANG_EN;

/** @brief Read one clamped byte; leaves the default alone if absent or bogus. */
static void load_u8(nvs_handle_t h, const char *key, uint8_t limit, uint8_t *out)
{
    uint8_t v = 0;
    if (nvs_get_u8(h, key, &v) == ESP_OK && v < limit) {
        *out = v;
    }
}

static esp_err_t store_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void prefs_init(void)
{
    nvs_handle_t h;
    /* Absent namespace simply means nothing has been changed from default. */
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v;

    v = (uint8_t)s_proj;
    load_u8(h, KEY_PROJ, MAP_EQUIRECT + 1, &v);
    s_proj = (map_projection_t)v;

    v = (uint8_t)s_units;
    load_u8(h, KEY_UNITS, UNITS_COUNT, &v);
    s_units = (unit_system_t)v;

    v = (uint8_t)s_lang;
    load_u8(h, KEY_LANG, LANG_COUNT, &v);
    s_lang = (lang_t)v;

    nvs_close(h);
    ESP_LOGI(TAG, "projection %s, units %d, language %d",
             s_proj == MAP_AZIMUTHAL ? "azimuthal" : "equirect", s_units, s_lang);
}

map_projection_t prefs_projection(void)
{
    return s_proj;
}

esp_err_t prefs_set_projection(map_projection_t p)
{
    if (p > MAP_EQUIRECT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_proj = p;
    return store_u8(KEY_PROJ, (uint8_t)p);
}

unit_system_t prefs_units(void)
{
    return s_units;
}

esp_err_t prefs_set_units(unit_system_t u)
{
    if (u >= UNITS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_units = u;
    return store_u8(KEY_UNITS, (uint8_t)u);
}

lang_t prefs_lang(void)
{
    return s_lang;
}

esp_err_t prefs_set_lang(lang_t l)
{
    if (l >= LANG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_lang = l;
    return store_u8(KEY_LANG, (uint8_t)l);
}

bool prefs_get_dx_filter(void *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t got = len;
    bool ok = (nvs_get_blob(h, KEY_DXF, out, &got) == ESP_OK) && got == len;
    nvs_close(h);
    return ok;
}

esp_err_t prefs_set_dx_filter(const void *in, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_DXF, in, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
