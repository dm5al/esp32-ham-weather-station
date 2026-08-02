/*
 * Ham Weather Station - Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Threading: LVGL owns its own task inside esp_lvgl_port and must never block,
 * so widget callbacks only enqueue a ui_cmd_t. Everything slow - HTTP, scans -
 * runs on the app task here, which touches LVGL only under bsp_display_lock().
 */
#include <string.h>

#include "bsp/board.h"
#include "bsp/lcd_console.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lib/i18n.h"
#include "lib/selftest.h"
#include "lib/prefs.h"
#include "lib/station.h"
#include "net/dxcluster.h"
#include "net/propagation.h"
#include "net/spacewx.h"
#include "net/weather.h"
#include "net/wifi_manager.h"
#include "nvs_flash.h"
#include "ui/theme.h"
#include "ui/ui.h"

static const char *TAG = "app";

/* wspr.live and NOAA both update on roughly this cadence, and wspr.live asks
 * that queries stay bounded - one pass every 15 min sits far inside fair use. */
#define REFRESH_INTERVAL_MS (15 * 60 * 1000)
#define RETRY_INTERVAL_MS   (2 * 60 * 1000)

/*
 * Cluster spots run on their own, much shorter cycle. They age out in minutes,
 * and the home screen's info panel needs them whether or not the DX page is
 * showing — a most-wanted entity appearing is exactly the thing worth surfacing
 * to someone looking at the weather.
 *
 * One request a minute against a 3 KB feed is well inside anything a public
 * service would object to, and far below what a real cluster pushes.
 */
#define DX_INTERVAL_MS (60 * 1000)
#define QUEUE_LEN 8

typedef enum { EVT_UI_CMD, EVT_WIFI_STATE } evt_kind_t;

typedef struct {
    evt_kind_t kind;
    union {
        ui_cmd_t cmd;
        wifi_mgr_state_t wifi;
    };
} app_evt_t;

static QueueHandle_t s_queue;
static spacewx_t s_sw;
static prop_band_state_t s_bands[PROP_BAND_COUNT];
static prop_muf_t s_muf;
static weather_data_t s_wx;
static EXT_RAM_BSS_ATTR dx_feed_t s_dx;
static int64_t s_next_refresh_ms;
static int64_t s_next_dx_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void ui_post_cmd(const ui_cmd_t *cmd)
{
    if (!s_queue || !cmd) {
        return;
    }
    app_evt_t e = {.kind = EVT_UI_CMD};
    e.cmd = *cmd;
    if (xQueueSend(s_queue, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full");
    }
}

static void on_wifi_state(wifi_mgr_state_t state, void *ctx)
{
    (void)ctx;
    app_evt_t e = {.kind = EVT_WIFI_STATE};
    e.wifi = state;
    xQueueSend(s_queue, &e, 0);
}

static void on_theme_changed(void)
{
    /* Deferred: this also fires from the night-mode buttons in Settings, and a
     * synchronous rebuild there would delete the screen mid-event. The port's
     * lock is recursive, so taking it from the LVGL task is harmless. */
    if (bsp_display_lock(1000)) {
        ui_rebuild_async();
        bsp_display_unlock();
    }
}

static void start_sntp_once(void)
{
    static bool started;
    if (started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        started = true;
        ESP_LOGI(TAG, "SNTP started");
    }
}

static void do_scan(void)
{
    static wifi_mgr_ap_t aps[WIFI_MGR_MAX_APS];
    size_t found = 0;
    esp_err_t err = wifi_mgr_scan(aps, WIFI_MGR_MAX_APS, &found);
    if (bsp_display_lock(0)) {
        if (err == ESP_OK) {
            ui_setup_aps(aps, found);
        } else {
            ui_setup_status(T(S_NO_NETWORKS), true);
        }
        bsp_display_unlock();
    }
}

/**
 * @brief Refresh cluster spots and hand them to the UI.
 *
 * Independent of do_refresh() so a failing cluster never delays space weather,
 * and a slow space-weather pass never holds up spots.
 */
static void do_dx_refresh(void)
{
    s_next_dx_ms = now_ms() + DX_INTERVAL_MS;
    if (wifi_mgr_get_state() != WIFI_MGR_CONNECTED) {
        return;
    }
    if (dx_fetch(&s_dx, ui_dx_sources()) != ESP_OK) {
        return;
    }
    if (bsp_display_lock(0)) {
        ui_set_dx_spots(&s_dx);
        ui_set_dx_next(time(NULL) + DX_INTERVAL_MS / 1000);
        bsp_display_unlock();
    }
}

static void do_refresh(void)
{
    if (wifi_mgr_get_state() != WIFI_MGR_CONNECTED) {
        s_next_refresh_ms = now_ms() + RETRY_INTERVAL_MS;
        return;
    }
    const station_t *st = station_get();
    if (!st->configured) {
        ESP_LOGW(TAG, "station not commissioned, skipping refresh");
        s_next_refresh_ms = now_ms() + RETRY_INTERVAL_MS;
        return;
    }

    bool any = false;
    if (spacewx_fetch(&s_sw) == ESP_OK) {
        any = true;
    }
    if (prop_fetch_conditions(station_grid_field(), s_bands) == ESP_OK) {
        any = true;
    }
    prop_fetch_muf(st->pos, &s_muf);
    weather_fetch(st->pos, &s_wx);

    if (bsp_display_lock(0)) {
        ui_set_spacewx(&s_sw);
        ui_set_bands(s_bands);
        ui_set_muf(&s_muf);
        ui_set_weather(&s_wx);
        bsp_display_unlock();
    }

    /* Stack headroom is reported alongside heap: this task carries TLS and the
     * JSON parsers, and it is the one that overflowed once a buffer grew. A
     * number that trends toward zero is the warning that was missing. */
    ESP_LOGI(TAG, "refresh done, free heap %u, largest block %u, app stack free %u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    s_next_refresh_ms = now_ms() + (any ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS);
    if (bsp_display_lock(0)) {
        ui_set_refresh_times(time(NULL), time(NULL) + (any ? 900 : 120));
        bsp_display_unlock();
    }
}

static void app_task(void *arg)
{
    (void)arg;

    /*
     * Commissioning is required whenever identity or network is missing. Until
     * a locator exists there is nothing to query, so the assistant is the only
     * sensible first screen.
     */
    bool need_setup = !station_get()->configured || !wifi_mgr_has_saved();
    if (need_setup) {
        if (bsp_display_lock(0)) {
            ui_show_setup();
            bsp_display_unlock();
        }
        if (wifi_mgr_has_saved()) {
            wifi_mgr_connect_saved();
        } else {
            do_scan();
        }
    } else {
        wifi_mgr_connect_saved();
    }
    s_next_refresh_ms = now_ms() + 2000;
    s_next_dx_ms = now_ms() + 6000;

    while (1) {
        int64_t wait = s_next_refresh_ms - now_ms();
        if (wait < 0) {
            wait = 0;
        }
        if (wait > 5000) {
            wait = 5000;   /* short naps: the cluster cycle is only a minute */
        }

        app_evt_t e;
        if (xQueueReceive(s_queue, &e, pdMS_TO_TICKS(wait)) == pdTRUE) {
            if (e.kind == EVT_WIFI_STATE) {
                if (e.wifi == WIFI_MGR_CONNECTED) {
                    start_sntp_once();
                    if (!station_get()->configured) {
                        /* Network is up but we still need identity and location. */
                        if (bsp_display_lock(0)) {
                            ui_setup_joined();
                            bsp_display_unlock();
                        }
                    } else {
                        do_refresh();
                    }
                } else if (e.wifi == WIFI_MGR_FAILED && bsp_display_lock(0)) {
                    ui_setup_status(T(S_CONNECT_FAIL), true);
                    bsp_display_unlock();
                }
                if (bsp_display_lock(0)) {
                    ui_set_link(wifi_mgr_get_state(), wifi_mgr_rssi());
                    bsp_display_unlock();
                }
            } else {
                switch (e.cmd.type) {
                case UI_CMD_REFRESH:
                    do_refresh();
                    break;
                case UI_CMD_WIFI_SCAN:
                    do_scan();
                    break;
                case UI_CMD_OPEN_DX:
                    do_dx_refresh();
                    break;
                case UI_CMD_OPEN_MAP: {
                    const prop_band_t *b = prop_band(e.cmd.band_index);
                    const station_t *bst = station_get();
                    if (b && bst->configured) {
                        static prop_spot_t spots[PROP_MAX_SPOTS];
                        int n = 0;
                        prop_fetch_map(station_grid_field(), b->key, spots,
                                       PROP_MAX_SPOTS, &n);
                        if (bsp_display_lock(0)) {
                            ui_set_map_spots(spots, n, &s_muf);
                            bsp_display_unlock();
                        }
                    }
                    break;
                }
                case UI_CMD_WIFI_CONNECT:
                    wifi_mgr_connect(e.cmd.a, e.cmd.b);
                    break;
                case UI_CMD_SET_STATION:
                    if (station_set(e.cmd.a, e.cmd.b, e.cmd.c) == ESP_OK) {
                        /* Commissioning done: leave the assistant and fetch. */
                        if (bsp_display_lock(0)) {
                            ui_show_home();
                            bsp_display_unlock();
                        }
                        s_next_refresh_ms = now_ms();
                    } else if (bsp_display_lock(0)) {
                        ui_setup_status(T(S_LOC_BAD), true);
                        bsp_display_unlock();
                    }
                    break;
                default:
                    break;
                }
            }
        } else {
            const station_t *st = station_get();
            if (st->configured) {
                theme_tick(st->pos, time(NULL));
            }
            if (bsp_display_lock(0)) {
                ui_set_link(wifi_mgr_get_state(), wifi_mgr_rssi());
                bsp_display_unlock();
            }
            if (now_ms() >= s_next_refresh_ms) {
                do_refresh();
            }
            if (now_ms() >= s_next_dx_ms) {
                do_dx_refresh();
            }
        }
    }
}

void app_main(void)
{
    /* NVS first: the panel timings, station identity and theme all live there,
     * and nvs_open() fails outright on an unmounted partition. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    station_init();
    prefs_init();
    theme_init(on_theme_changed);

    ESP_ERROR_CHECK(bsp_board_init());

    bsp_display_lock(0);
    ui_init();
    bsp_display_unlock();

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(bsp_display_backlight(true));

    selftest_run();

    s_queue = xQueueCreate(QUEUE_LEN, sizeof(app_evt_t));
    ESP_ERROR_CHECK(s_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(wifi_mgr_init(on_wifi_state, NULL));

    /*
     * 12 KB, not 8. This task runs every network fetch: a TLS handshake wants
     * several KB on its own, and cJSON recurses through a 42 KB ionosonde
     * response on top of it. The margin at 8 KB was thin enough that growing
     * one buffer was sufficient to overflow it.
     */
    xTaskCreate(app_task, "app", 12288, NULL, 5, NULL);
    lcd_console_start();
}
