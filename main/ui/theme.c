#include "ui/theme.h"

#include "esp_log.h"
#include "lib/solar.h"
#include "nvs.h"

static const char *TAG = "theme";

#define NVS_NS  "ui"
#define NVS_KEY "night"

/*
 * Night switches at civil twilight rather than at sunset. Sunset itself is too
 * early — the sky is still bright and the red palette looks broken — while full
 * darkness is too late to be useful.
 */
#define NIGHT_ELEVATION_DEG (-6.0f)

/* Hysteresis, so a station near the threshold does not flicker between
 * palettes as the sun hovers around civil twilight. */
#define NIGHT_HYSTERESIS_DEG 1.0f

static const theme_t k_day = {
    .bg          = LV_COLOR_MAKE(0x0F, 0x17, 0x2A),
    .card        = LV_COLOR_MAKE(0x1E, 0x29, 0x3B),
    .card_hi     = LV_COLOR_MAKE(0x33, 0x41, 0x55),
    .text        = LV_COLOR_MAKE(0xF1, 0xF5, 0xF9),
    .muted       = LV_COLOR_MAKE(0x94, 0xA3, 0xB8),
    .accent      = LV_COLOR_MAKE(0x38, 0xBD, 0xF8),
    /*
     * Four status colours that must stay apart at 3x3 pixels on a map. The
     * previous amber (FBBF24) and orange (FB923C) differed only in the green
     * channel by 45 counts and were indistinguishable at that size, and the
     * old "poor" (F87171) was a desaturated pink that never read as red.
     * Yellow now keeps a high green channel, orange sits well below it, and
     * red has almost none.
     */
    .good        = LV_COLOR_MAKE(0x34, 0xD3, 0x99),
    .fair        = LV_COLOR_MAKE(0xFD, 0xE0, 0x47),
    .poor        = LV_COLOR_MAKE(0xFF, 0x2D, 0x2D),
    .orange      = LV_COLOR_MAKE(0xF9, 0x73, 0x16),
    .sea         = LV_COLOR_MAKE(0x16, 0x23, 0x3C),
    .land        = LV_COLOR_MAKE(0x2C, 0x43, 0x64),
    .night_shade = LV_COLOR_MAKE(0x04, 0x09, 0x1A),
};

/* Red-only: conditions are separated by brightness, not hue. */
static const theme_t k_night = {
    .bg          = LV_COLOR_MAKE(0x12, 0x06, 0x04),
    .card        = LV_COLOR_MAKE(0x22, 0x10, 0x08),
    .card_hi     = LV_COLOR_MAKE(0x33, 0x18, 0x0C),
    .text        = LV_COLOR_MAKE(0xFF, 0x7A, 0x55),
    .muted       = LV_COLOR_MAKE(0xA8, 0x50, 0x30),
    .accent      = LV_COLOR_MAKE(0xFF, 0x57, 0x22),
    /* Same four, separated by brightness instead of hue. The steps are widened
     * to match: at 3x3 pixels two similar reds are one colour. */
    .good        = LV_COLOR_MAKE(0xFF, 0xB8, 0x8C),
    .fair        = LV_COLOR_MAKE(0xF0, 0x7E, 0x42),
    .poor        = LV_COLOR_MAKE(0x8C, 0x2E, 0x14),
    .orange      = LV_COLOR_MAKE(0xB4, 0x50, 0x1F),
    .sea         = LV_COLOR_MAKE(0x1A, 0x0B, 0x06),
    .land        = LV_COLOR_MAKE(0x3A, 0x1A, 0x0E),
    .night_shade = LV_COLOR_MAKE(0x00, 0x00, 0x00),
};

static theme_mode_t s_mode = THEME_DAY_ALWAYS;
static bool s_night;
static theme_changed_cb_t s_cb;

static void apply(bool night)
{
    if (night == s_night) {
        return;
    }
    s_night = night;
    ESP_LOGI(TAG, "palette -> %s", night ? "night" : "day");
    if (s_cb) {
        s_cb();
    }
}

void theme_init(theme_changed_cb_t on_change)
{
    s_cb = on_change;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = THEME_DAY_ALWAYS;
        if (nvs_get_u8(h, NVS_KEY, &v) == ESP_OK && v <= THEME_NIGHT_ALWAYS) {
            s_mode = (theme_mode_t)v;
        }
        nvs_close(h);
    }
    /*
     * Night mode no longer has any control in Settings, so a mode left over in
     * NVS from an earlier build would be unreachable — and AUTO would still
     * flip the palette at dusk, rebuilding every screen with nothing on screen
     * to explain why. Pin it to day until there is a way to choose again.
     */
    if (s_mode != THEME_DAY_ALWAYS) {
        ESP_LOGI(TAG, "stored night mode %d ignored: no control for it", s_mode);
        s_mode = THEME_DAY_ALWAYS;
    }
    s_night = false;
    ESP_LOGI(TAG, "mode %d, starting %s", s_mode, s_night ? "night" : "day");
}

const theme_t *theme(void)
{
    return s_night ? &k_night : &k_day;
}

theme_mode_t theme_get_mode(void)
{
    return s_mode;
}

bool theme_is_night(void)
{
    return s_night;
}

esp_err_t theme_set_mode(theme_mode_t mode)
{
    if (mode > THEME_NIGHT_ALWAYS) {
        return ESP_ERR_INVALID_ARG;
    }
    s_mode = mode;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY, (uint8_t)mode);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }

    if (mode == THEME_DAY_ALWAYS) {
        apply(false);
    } else if (mode == THEME_NIGHT_ALWAYS) {
        apply(true);
    }
    /* AUTO takes effect on the next tick, once a position is known. */
    return err;
}

void theme_tick(geo_point_t qth, time_t now)
{
    if (s_mode != THEME_AUTO || now < 1600000000) {
        return;   /* fixed mode, or the clock has not been set yet */
    }
    float elev = solar_elevation(qth, now);
    float threshold = s_night ? NIGHT_ELEVATION_DEG + NIGHT_HYSTERESIS_DEG
                              : NIGHT_ELEVATION_DEG;
    apply(elev < threshold);
}
