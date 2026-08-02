/*
 * Screen manager.
 *
 * A palette change rebuilds every screen rather than re-styling in place: LVGL
 * styles are applied per object at creation, and chasing every one of them
 * through a colour swap is far more error-prone than building again from
 * scratch. Rebuilds only happen at sunset, or when the user changes the
 * setting, so the cost is irrelevant.
 */
#include "ui/ui.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui";

static lv_obj_t *s_screens[UI_PAGE_COUNT];
static ui_page_t s_current = UI_PAGE_HOME;
static int s_map_band;

/* Last known values, retained so a rebuild can repaint without a refetch. */
static spacewx_t s_sw;
static prop_band_state_t s_bands[PROP_BAND_COUNT];
static prop_muf_t s_muf;
static weather_data_t s_wx;
static EXT_RAM_BSS_ATTR dx_feed_t s_dx;
static bool s_have_wx;

/**
 * @brief Build a screen the first time it is needed.
 *
 * Creating all five up front cost enough heap to starve the TLS handshake:
 * hundreds of LVGL objects plus the map's 284 KB canvas, held permanently for
 * screens the user may never open. Only one is ever visible, so only one needs
 * to exist. Retained data is replayed into whichever screen is built, so a page
 * appearing late is indistinguishable from one that was always there.
 */
static lv_obj_t *ensure(ui_page_t page)
{
    if (s_screens[page]) {
        return s_screens[page];
    }
    switch (page) {
    case UI_PAGE_HOME:
        s_screens[page] = ui_home_create();
        ui_home_set_bands(s_bands);
        if (s_sw.valid) {
            ui_home_set_spacewx(&s_sw);
        }
        if (s_have_wx) {
            ui_home_set_weather(&s_wx);
        }
        ui_home_set_dx(&s_dx);
        break;
    case UI_PAGE_MAP:
        s_screens[page] = ui_map_create();
        ui_map_set_band(s_map_band);
        break;
    case UI_PAGE_DX:
        s_screens[page] = ui_dx_create();
        ui_dx_set_spots(&s_dx);
        break;
    case UI_PAGE_SETTINGS:
        s_screens[page] = ui_settings_create();
        break;
    case UI_PAGE_SETUP:
        s_screens[page] = ui_setup_create();
        break;
    default:
        return NULL;
    }
    ESP_LOGI(TAG, "built page %d, free heap %u", page,
             (unsigned)esp_get_free_heap_size());
    return s_screens[page];
}

static void load(ui_page_t page)
{
    if (!ensure(page)) {
        ESP_LOGW(TAG, "page %d could not be built", page);
        return;
    }
    s_current = page;
    if (lv_screen_active() != s_screens[page]) {
        lv_screen_load(s_screens[page]);
    }
    /* The clock timer repaints whichever page is showing. */
    ui_header_set_active(page);
    ui_header_refresh(page);
}

void ui_init(void)
{
    memset(s_screens, 0, sizeof(s_screens));
    load(UI_PAGE_HOME);
    ui_header_start_clock();
    ESP_LOGI(TAG, "ui ready, free heap %u", (unsigned)esp_get_free_heap_size());
}

void ui_rebuild(void)
{
    ESP_LOGI(TAG, "rebuilding for palette change");
    ui_page_t want = s_current;

    lv_obj_t *old[UI_PAGE_COUNT];
    memcpy(old, s_screens, sizeof(old));
    memset(s_screens, 0, sizeof(s_screens));

    /* Build the replacement before dropping the old one so nothing flickers,
     * then release every stale screen — they will be rebuilt on demand. */
    load(want);
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        if (old[i] && old[i] != s_screens[i]) {
            lv_obj_delete(old[i]);
        }
    }
}

static void rebuild_cb(void *unused)
{
    (void)unused;
    ui_rebuild();
}

void ui_rebuild_async(void)
{
    lv_async_call(rebuild_cb, NULL);
}

void ui_show_home(void)
{
    load(UI_PAGE_HOME);
}

void ui_show_map(int band_index)
{
    const prop_band_t *b = prop_band(band_index);
    if (!b) {
        return;
    }
    s_map_band = band_index;
    ESP_LOGI(TAG, "map requested for %s", b->name);

    ui_map_set_band(band_index);
    /* Show immediately with whatever is already drawn; the app task fills it. */
    load(UI_PAGE_MAP);

    ui_cmd_t cmd = {.type = UI_CMD_OPEN_MAP, .band_index = band_index};
    ui_post_cmd(&cmd);
}

void ui_show_dx(void)
{
    /* Show whatever was last fetched immediately, then ask for fresh spots;
     * a blank table while the network works would look like a failure. */
    load(UI_PAGE_DX);
    const ui_cmd_t cmd = {.type = UI_CMD_OPEN_DX};
    ui_post_cmd(&cmd);
}

void ui_show_settings(void)
{
    ui_settings_refresh();
    load(UI_PAGE_SETTINGS);
}

void ui_show_setup(void)
{
    load(UI_PAGE_SETUP);
}

void ui_show_wifi_setup(void)
{
    ensure(UI_PAGE_SETUP);
    ui_setup_goto_wifi();
    load(UI_PAGE_SETUP);
}

void ui_show_station_setup(void)
{
    ensure(UI_PAGE_SETUP);
    ui_setup_goto_station();
    load(UI_PAGE_SETUP);
}

void ui_setup_aps(const wifi_mgr_ap_t *aps, size_t count)
{
    ui_setup_set_aps(aps, count);
}

void ui_setup_status(const char *text, bool error)
{
    ui_setup_set_status(text, error);
}

void ui_setup_joined(void)
{
    ui_setup_wifi_connected();
}

/* ---- data in ------------------------------------------------------------ */

void ui_set_spacewx(const spacewx_t *sw)
{
    if (sw) {
        s_sw = *sw;
        ui_home_set_spacewx(&s_sw);
    }
}

void ui_set_bands(const prop_band_state_t *bands)
{
    if (bands) {
        memcpy(s_bands, bands, sizeof(s_bands));
        ui_home_set_bands(s_bands);
    }
}

void ui_set_muf(const prop_muf_t *muf)
{
    if (muf) {
        s_muf = *muf;
    }
}

void ui_set_weather(const weather_data_t *wx)
{
    if (wx) {
        s_wx = *wx;
        s_have_wx = true;
        ui_header_set_utc_offset(wx->utc_offset_seconds);
        ui_home_set_weather(&s_wx);
    }
}

void ui_set_link(wifi_mgr_state_t state, int8_t rssi)
{
    ui_header_set_link(state, rssi);
    ui_header_refresh(s_current);
}

void ui_set_dx_spots(const dx_feed_t *feed)
{
    if (feed) {
        s_dx = *feed;
        ui_dx_set_spots(&s_dx);
        /* The home panel surfaces most-wanted spots, so it needs this too. */
        ui_home_set_dx(&s_dx);
    }
}

uint8_t ui_dx_sources(void)
{
    return ui_dx_selected_sources();
}

void ui_set_dx_next(time_t next)
{
    ui_header_set_dx_next(next);
    ui_header_refresh(s_current);
}

void ui_set_map_spots(const prop_spot_t *spots, int count, const prop_muf_t *muf)
{
    /* Safe when the map has never been opened: the screen module ignores this
     * until it exists, and the app refetches on the next open. */
    ui_map_set_spots(spots, count, muf);
}

void ui_set_refresh_times(time_t updated, time_t next)
{
    ui_header_set_refresh_times(updated, next);
    ui_header_refresh(s_current);
}
