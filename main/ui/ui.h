/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Screen manager and the boundary between the UI and the application task.
 *
 * LVGL runs on its own task and must never block, so widget callbacks do
 * nothing but post a ui_cmd_t. Every slow operation — network fetches, scans —
 * happens on the app task in main.c, which touches LVGL only while holding
 * bsp_display_lock().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "lvgl.h"
#include "net/dxcluster.h"
#include "net/propagation.h"
#include "net/spacewx.h"
#include "net/weather.h"
#include "net/wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_CMD_REFRESH,
    UI_CMD_OPEN_MAP,        /* .band_index selects the band */
    UI_CMD_WIFI_SCAN,
    UI_CMD_WIFI_CONNECT,
    UI_CMD_SET_STATION,
    UI_CMD_SET_NIGHT_MODE,
    UI_CMD_OPEN_DX,         /* fetch cluster spots for the DX page */
} ui_cmd_type_t;

typedef struct {
    ui_cmd_type_t type;
    int           band_index;
    int           value;
    char          a[40];    /* ssid / callsign */
    char          b[40];    /* password / qth */
    char          c[12];    /* locator */
} ui_cmd_t;

/** @brief Implemented in main.c; safe to call from the LVGL task. */
void ui_post_cmd(const ui_cmd_t *cmd);

/** @brief Build every screen. Call with the LVGL lock held. */
void ui_init(void);

/** @brief Rebuild all screens after a palette, language or unit change. */
void ui_rebuild(void);

/**
 * @brief Same, but deferred to the next LVGL iteration.
 *
 * Required whenever the trigger is a widget callback: ui_rebuild() deletes the
 * screen the callback belongs to, and destroying an object while its own event
 * is still being dispatched corrupts LVGL's event stack. Must be called with
 * the display lock held.
 */
void ui_rebuild_async(void);

void ui_show_home(void);
void ui_show_map(int band_index);
void ui_show_dx(void);
void ui_show_settings(void);
void ui_show_setup(void);

/** @brief Raise the About sheet over Settings, for the serial console. */
void ui_settings_open_about(void);

/** @brief Open commissioning directly on the network or the station step. */
void ui_show_wifi_setup(void);
void ui_show_station_setup(void);


/* Commissioning feedback from the app task. */
void ui_setup_aps(const wifi_mgr_ap_t *aps, size_t count);
void ui_setup_status(const char *text, bool error);
void ui_setup_joined(void);

/* ---- data in ------------------------------------------------------------ */

void ui_set_spacewx(const spacewx_t *sw);
void ui_set_bands(const prop_band_state_t *bands);
void ui_set_muf(const prop_muf_t *muf);
void ui_set_weather(const weather_data_t *wx);
void ui_set_link(wifi_mgr_state_t state, int8_t rssi);
void ui_set_map_spots(const prop_spot_t *spots, int count, const prop_muf_t *muf);
void ui_set_dx_spots(const dx_feed_t *feed);

/** @brief When the next cluster fetch is due, for the DX page countdown. */
void ui_set_dx_next(time_t next);

/** @brief Sources the DX page filter currently has enabled. */
uint8_t ui_dx_sources(void);

/** @brief Freshness shown in the header: when we last succeeded, and when next. */
void ui_set_refresh_times(time_t updated, time_t next);

#ifdef __cplusplus
}
#endif
