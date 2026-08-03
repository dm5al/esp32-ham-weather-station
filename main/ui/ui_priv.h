/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Internal helpers shared between the screen files.
 *
 * The little wrappers exist because LVGL needs four or five calls to place a
 * plain label or box, and repeating that inline made the layout code unreadable.
 */
#pragma once

#include <stddef.h>

#include "lvgl.h"
#include "net/dxcluster.h"
#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_MAP,
    UI_PAGE_DX,
    UI_PAGE_SETTINGS,
    UI_PAGE_SETUP,
    UI_PAGE_COUNT,
} ui_page_t;

/* ---- primitives --------------------------------------------------------- */

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int x, int y,
                   const char *text);
lv_obj_t *ui_label_right(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int right,
                         int y, const char *text);
lv_obj_t *ui_label_centre(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int x,
                          int y, int w, const char *text);

/** @brief Flat rectangle with no border, padding or scrollbar. */
lv_obj_t *ui_box(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t colour, int radius);

/** @brief Card background in the current palette. */
lv_obj_t *ui_card(lv_obj_t *parent, int x, int y, int w, int h);

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb);

void ui_hline(lv_obj_t *parent, int x, int y, int w, lv_color_t colour);
void ui_vline(lv_obj_t *parent, int x, int y, int h, lv_color_t colour);

/** @brief "Mon".."Sun" for a tm_wday. */
const char *ui_weekday_short(int tm_wday);

/* ---- header, identical on every page ------------------------------------ */

void ui_header_build(lv_obj_t *scr, ui_page_t page);
void ui_header_refresh(ui_page_t page);
void ui_header_set_utc_offset(int seconds);
void ui_header_set_link(wifi_mgr_state_t state, int8_t rssi);
void ui_header_set_refresh_times(time_t updated, time_t next);
void ui_header_set_dx_time(time_t updated);
void ui_header_set_dx_next(time_t next);

/** @brief Which page the one-second clock timer should repaint. */
void ui_header_set_active(ui_page_t page);

/** @brief Start the shared one-second tick that advances the clocks. */
void ui_header_start_clock(void);

/* ---- bottom action bar, on every page except home ------------------------ */

/**
 * @brief Draws the divider above the action row; returns the screen.
 *
 * There is no Back button any more. Every page reachable from home is one level
 * deep, so "back" and "home" were always the same destination, and the header
 * button already offers it from a fixed position on every screen.
 */
lv_obj_t *ui_bottom_bar(lv_obj_t *scr);

/* ---- screens ------------------------------------------------------------ */

lv_obj_t *ui_home_create(void);
lv_obj_t *ui_setup_create(void);
lv_obj_t *ui_settings_create(void);
lv_obj_t *ui_dx_create(void);
lv_obj_t *ui_map_create(void);
void ui_map_set_band(int band_index);
void ui_map_set_spots(const prop_spot_t *spots, int count, const prop_muf_t *muf);
void ui_dx_refresh(void);
void ui_dx_set_spots(const dx_feed_t *feed);
uint8_t ui_dx_selected_sources(void);
void ui_settings_refresh(void);
void ui_setup_set_aps(const wifi_mgr_ap_t *aps, size_t count);
void ui_setup_set_status(const char *text, bool error);
void ui_setup_wifi_connected(void);
void ui_setup_goto_wifi(void);
void ui_setup_goto_station(void);

void ui_home_set_spacewx(const spacewx_t *sw);
void ui_home_set_bands(const prop_band_state_t *bands);
void ui_home_set_weather(const weather_data_t *wx);
void ui_home_set_dx(const dx_feed_t *feed);

#ifdef __cplusplus
}
#endif
