/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Board support for the Waveshare ESP32-S3-Touch-LCD-7.
 *
 *   800x480 IPS, 16-bit RGB565 parallel interface (no controller IC — the
 *   ESP32-S3 LCD_CAM peripheral scans the panel directly out of PSRAM)
 *   GT911 capacitive touch on I2C0, shared with a CH422G I/O expander.
 *
 * Pin map is taken from the Waveshare schematic; see WS_* defines in board.c.
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LCD_H_RES 800
#define BSP_LCD_V_RES 480

/**
 * @brief Bring up expander, RGB panel, GT911 touch and LVGL.
 *
 * On return LVGL is running on its own task and lv_scr_act() is drawable.
 * Every LVGL call from outside that task must be wrapped in
 * bsp_display_lock()/bsp_display_unlock().
 */
esp_err_t bsp_board_init(void);

/** @brief Take the LVGL mutex. @param timeout_ms 0 = wait forever. */
bool bsp_display_lock(uint32_t timeout_ms);

/** @brief Release the LVGL mutex. */
void bsp_display_unlock(void);

/**
 * @brief Turn the panel backlight on or off.
 *
 * The backlight on this board hangs off the expander's DISP pin, so it is a
 * plain on/off switch — there is no PWM dimming path.
 */
esp_err_t bsp_display_backlight(bool on);

/**
 * @brief Overlay a panel-alignment pattern on the top layer.
 *
 * A bring-up aid rather than application UI: draws a 1px frame on the outermost
 * pixels, corner brackets, a centre cross and 50px ticks, so an RGB timing error
 * shows up as a clipped or gapped edge.
 */
void bsp_alignment_grid(bool on);

/** @brief The GT911 handle, for diagnostics. NULL before bsp_board_init(). */
esp_lcd_touch_handle_t bsp_touch_handle(void);

/**
 * @brief The frame buffer the panel is currently scanning out.
 *
 * Exposed so a screenshot can be taken of exactly what is on the glass, rather
 * than photographed off it. RGB565, BSP_LCD_H_RES * BSP_LCD_V_RES pixels.
 *
 * Call it with the display lock held and after forcing a redraw, or the buffer
 * may be caught mid-repaint. Returns NULL before the panel is up.
 */
const uint16_t *bsp_display_framebuffer(void);

/**
 * @brief Read raw GT911 registers over the shared I2C bus.
 *
 * Lets the console inspect the controller directly, which separates "the chip
 * is not reporting touches" from "the touches are not reaching LVGL".
 */
esp_err_t bsp_touch_read_reg(uint16_t reg, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
