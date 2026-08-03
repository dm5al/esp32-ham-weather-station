/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Colour scheme, with a red-preserving night mode for a dark shack.
 *
 * Night mode is not simply "darker". Red light preserves dark adaptation, but
 * red alone cannot encode hue — so where the day palette distinguishes
 * Good/Fair/Poor by colour, the night palette distinguishes them by brightness.
 * Any new status indicator has to work under both.
 *
 * The panel backlight on this board is on/off only (DISP via the CH422G), so
 * dimming has to be done by darkening the palette in software.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"
#include "lib/geo.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_color_t bg;
    lv_color_t card;
    lv_color_t card_hi;
    lv_color_t text;
    lv_color_t muted;
    lv_color_t accent;
    lv_color_t good;
    lv_color_t fair;
    lv_color_t poor;
    lv_color_t orange;
    lv_color_t sea;
    lv_color_t land;
    lv_color_t night_shade;
} theme_t;

typedef enum {
    THEME_DAY_ALWAYS = 0,
    THEME_AUTO,          /* follows local sunset */
    THEME_NIGHT_ALWAYS,
} theme_mode_t;

/** @brief Called when the effective palette changes and screens must rebuild. */
typedef void (*theme_changed_cb_t)(void);

/** @brief Load the stored mode from NVS. */
void theme_init(theme_changed_cb_t on_change);

/** @brief The palette currently in force. Never NULL. */
const theme_t *theme(void);

theme_mode_t theme_get_mode(void);

/** @brief Set and persist the mode; fires the change callback if it matters. */
esp_err_t theme_set_mode(theme_mode_t mode);

bool theme_is_night(void);

/**
 * @brief Re-evaluate AUTO mode against the sun.
 *
 * Cheap enough to call once a minute. Does nothing in the fixed modes.
 */
void theme_tick(geo_point_t qth, time_t now);

#ifdef __cplusplus
}
#endif
