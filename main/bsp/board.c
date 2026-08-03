/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "bsp/board.h"

#include <inttypes.h>

/* First: it decides which of the two boards everything below is compiled for,
 * including whether the expander exists at all. */
#include "bsp/board_pins.h"

#include "bsp/lcd_timing.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "ui/fonts/ui_fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* ---------------------------------------------------------------------------
 * Both panels are raw 800x480 RGB565 TFTs: the S3's LCD_CAM peripheral scans
 * them continuously out of PSRAM, so there is no command interface and no init
 * sequence — only timings and pins, which live in bsp/board_pins.h.
 *
 * ESP-IDF orders data_gpio_nums as [0..4]=B, [5..10]=G, [11..15]=R, LSB first.
 * The Waveshare ties its low colour bits off on the PCB, so its mapping starts
 * at B3/G2/R3; the Sunton wires B0/G0/R0.
 * ------------------------------------------------------------------------- */
#include "driver/ledc.h"

static lcd_timing_t s_timing;
static i2c_master_bus_handle_t s_i2c_bus;
#if BOARD_HAS_EXPANDER
static ch422g_handle_t s_expander;
#endif
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_tp_io;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_disp;

static esp_err_t init_i2c_and_expander(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = TP_I2C_SDA,
        .scl_io_num = TP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");

#if BOARD_HAS_EXPANDER
    ESP_RETURN_ON_ERROR(ch422g_create(s_i2c_bus, &s_expander), TAG, "ch422g");

    /*
     * Park the expander outputs in a safe state before anything else runs:
     * backlight off (so the user never sees an uninitialised frame buffer),
     * SD deselected, USB mux to the CH343 UART, panel + touch held in reset.
     */
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_LCD_BL, false), TAG, "bl");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_SD_CS, true), TAG, "sd_cs");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_USB_SEL, false), TAG, "usb_sel");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_LCD_RST, false), TAG, "lcd_rst");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_TP_RST, false), TAG, "tp_rst");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_LCD_RST, true), TAG, "lcd_rst");
#else
    /*
     * No expander. The backlight is a plain GPIO on an LEDC channel, which is
     * an improvement — this panel can actually dim, where the Waveshare's DISP
     * line is on or off. It starts off so no one sees an uninitialised frame.
     */
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "bl timer");

    const ledc_channel_config_t bl_ch = {
        .gpio_num = BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_ch), TAG, "bl channel");
#endif
    return ESP_OK;
}

static esp_err_t init_lcd(void)
{
    /* Timings come from NVS when present so they can be tuned over the console
     * without a rebuild — see bsp/lcd_timing.h. */
    lcd_timing_load(&s_timing);
    ESP_LOGI(TAG, "timings: pclk=%" PRIu32 "MHz h(%u/%u/%u) v(%u/%u/%u) "
                  "hpol=%u vpol=%u depol=%u pclkneg=%u bb=%u lines%s",
             s_timing.pclk_mhz, s_timing.hpw, s_timing.hbp, s_timing.hfp, s_timing.vpw,
             s_timing.vbp, s_timing.vfp, s_timing.hsync_idle_low, s_timing.vsync_idle_low,
             s_timing.de_idle_high, s_timing.pclk_active_neg, s_timing.bb_lines,
             lcd_timing_is_overridden() ? " (from NVS)" : " (defaults)");

    if (s_timing.bb_lines && (BSP_LCD_V_RES % s_timing.bb_lines) != 0) {
        /* The driver derives its expected DMA-EOF count from fb_size/bb_size;
         * a remainder makes that count short and its underrun detector fires
         * on every frame, which looks like a trembling picture. */
        ESP_LOGW(TAG, "bb_lines=%u does not divide %d evenly — underrun detection "
                      "will misfire; use 8, 10, 12, 16, 20 or 24",
                 s_timing.bb_lines, BSP_LCD_V_RES);
    }

    const esp_lcd_rgb_panel_config_t panel_cfg = {
        /* Per board: the Sunton's reference configuration drives its bounce
         * buffers from PLL240M, and the pixel clock below is chosen for it. */
        .clk_src = LCD_CLK_SOURCE,
        .data_width = 16,
        .bits_per_pixel = 16,
        /* Two PSRAM frame buffers: LVGL renders straight into them and we page
         * flip on vsync, which is what makes the UI tear-free. */
        .num_fbs = 2,
        /* Stage frames through internal SRAM. Reading PSRAM directly at the
         * pixel clock leaves no margin for bandwidth spikes, and an underrun
         * desyncs the scan permanently. */
        .bounce_buffer_size_px = s_timing.bb_lines * BSP_LCD_H_RES,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .de_gpio_num = LCD_DE_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
        .disp_gpio_num = GPIO_NUM_NC, /* DISP hangs off the CH422G, not a GPIO */
        .data_gpio_nums = LCD_DATA_GPIOS,
        .timings = {
            .pclk_hz = s_timing.pclk_mhz * 1000 * 1000,
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .hsync_pulse_width = s_timing.hpw,
            .hsync_back_porch = s_timing.hbp,
            .hsync_front_porch = s_timing.hfp,
            .vsync_pulse_width = s_timing.vpw,
            .vsync_back_porch = s_timing.vbp,
            .vsync_front_porch = s_timing.vfp,
            .flags.hsync_idle_low = s_timing.hsync_idle_low,
            .flags.vsync_idle_low = s_timing.vsync_idle_low,
            .flags.de_idle_high = s_timing.de_idle_high,
            .flags.pclk_active_neg = s_timing.pclk_active_neg,
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel), TAG, "new rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return ESP_OK;
}

static esp_err_t init_touch(void)
{
#if BOARD_HAS_INT_ADDRESS_SELECT
    /*
     * The GT911 latches its I2C address from the INT pin on the rising edge of
     * reset: INT low -> 0x5D, INT high -> 0x14. Reset is on the expander rather
     * than a GPIO, so the driver cannot do this for us.
     */
    const gpio_config_t int_out = {
        .pin_bit_mask = 1ULL << TP_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_out), TAG, "int gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_INT_GPIO, 0), TAG, "int low");

    /* Datasheet sequence, and the hold times matter:
     *   both lines low >=100us, release RST >=5ms, then keep INT at the
     *   address-select level for >=50ms *after* RST rises — that is when the
     *   controller samples it and finishes booting. Releasing INT early leaves
     *   the chip answering on I2C but never reporting touches. */
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_TP_RST, false), TAG, "tp_rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, EXIO_TP_RST, true), TAG, "tp_rst high");
    vTaskDelay(pdMS_TO_TICKS(55));

    /* Only now is the address latched; hand INT back to the GT911. */
    ESP_RETURN_ON_ERROR(gpio_reset_pin(TP_INT_GPIO), TAG, "int release");
    vTaskDelay(pdMS_TO_TICKS(50));
#else
    /*
     * This board does not wire INT at all — there is an unpopulated R17 to
     * GPIO18 for anyone who wants interrupts — so the address cannot be chosen
     * and the controller comes up wherever its own pull-ups put it. Reset is a
     * real GPIO here, and the driver is left to poll.
     */
    const gpio_config_t rst_out = {
        .pin_bit_mask = 1ULL << TP_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_out), TAG, "tp rst gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST_GPIO, 0), TAG, "tp rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST_GPIO, 1), TAG, "tp rst high");
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = TP_I2C_HZ;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(s_i2c_bus, &io_cfg, &s_tp_io), TAG, "tp io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        /* Both lines are driven through the expander / already handled above. */
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(s_tp_io, &tp_cfg, &s_touch), TAG, "gt911");
    return ESP_OK;
}

const uint16_t *bsp_display_framebuffer(void)
{
    if (!s_panel) {
        return NULL;
    }
    /*
     * With two buffers in direct mode LVGL alternates between them, so the one
     * being scanned depends on when you ask. Buffer 0 is returned and the
     * caller is expected to have forced a full redraw first, which paints both
     * in turn — a screenshot is worth a repaint.
     */
    void *fb = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK) {
        return NULL;
    }
    return (const uint16_t *)fb;
}

esp_lcd_touch_handle_t bsp_touch_handle(void)
{
    return s_touch;
}

esp_err_t bsp_touch_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(s_tp_io && buf, ESP_ERR_INVALID_STATE, TAG, "touch io not ready");
    return esp_lcd_panel_io_rx_param(s_tp_io, reg, buf, len);
}

static esp_err_t init_lvgl(void)
{
    const lvgl_port_cfg_t port_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_panel,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            /* avoid_tearing hands LVGL the panel's own frame buffers, so it
             * must render in direct mode to absolute coordinates. */
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            /* In bounce mode the "frame is out" signal comes from the bounce
             * finish callback, not vsync — esp_lvgl_port picks the right one
             * off this flag, so it has to track the panel config. */
            .bb_mode = s_timing.bb_lines > 0,
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "add rgb disp");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = s_touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "add touch");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c_and_expander(), TAG, "i2c/expander");
    ESP_RETURN_ON_ERROR(init_lcd(), TAG, "lcd");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "touch");
    ESP_RETURN_ON_ERROR(init_lvgl(), TAG, "lvgl");
    ESP_LOGI(TAG, "board ready: %dx%d RGB565, GT911 touch, LVGL running",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t bsp_display_backlight(bool on)
{
#if BOARD_HAS_EXPANDER
    ESP_RETURN_ON_FALSE(s_expander, ESP_ERR_INVALID_STATE, TAG, "expander not ready");
    return ch422g_set_level(s_expander, EXIO_LCD_BL, on);
#else
    /* Full brightness or dark. The channel is configured for 10-bit duty, so
     * a dimming control has somewhere to go later without touching callers. */
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                      on ? 1023 : 0), TAG, "bl duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

/* ---- Panel alignment overlay -------------------------------------------- */

static lv_obj_t *s_grid;

/** @brief Undecorated filled rectangle on the grid overlay. */
static void grid_bar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void bsp_alignment_grid(bool on)
{
    if (!on) {
        if (s_grid) {
            lv_obj_delete(s_grid);
            s_grid = NULL;
        }
        return;
    }
    if (s_grid) {
        return;
    }

    const int w = LV_HOR_RES;
    const int h = LV_VER_RES;

    s_grid = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, w, h);
    lv_obj_set_pos(s_grid, 0, 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    const lv_color_t frame = lv_color_hex(0xFF3B30);
    const lv_color_t tick = lv_color_hex(0x34C759);
    const lv_color_t cross = lv_color_hex(0x0A84FF);

    /* 1px frame on the outermost pixels — the reference for "is it clipped?". */
    grid_bar(s_grid, 0, 0, w, 1, frame);
    grid_bar(s_grid, 0, h - 1, w, 1, frame);
    grid_bar(s_grid, 0, 0, 1, h, frame);
    grid_bar(s_grid, w - 1, 0, 1, h, frame);

    /* Corner brackets: thick enough to spot at a glance from across the desk. */
    const int arm = 40;
    const int thick = 4;
    grid_bar(s_grid, 0, 0, arm, thick, frame);
    grid_bar(s_grid, 0, 0, thick, arm, frame);
    grid_bar(s_grid, w - arm, 0, arm, thick, frame);
    grid_bar(s_grid, w - thick, 0, thick, arm, frame);
    grid_bar(s_grid, 0, h - thick, arm, thick, frame);
    grid_bar(s_grid, 0, h - arm, thick, arm, frame);
    grid_bar(s_grid, w - arm, h - thick, arm, thick, frame);
    grid_bar(s_grid, w - thick, h - arm, thick, arm, frame);

    /* Ticks every 50px, taller every 100px, for counting the offset. */
    for (int x = 50; x < w; x += 50) {
        int len = (x % 100 == 0) ? 20 : 10;
        grid_bar(s_grid, x, 0, 1, len, tick);
        grid_bar(s_grid, x, h - len, 1, len, tick);
    }
    for (int y = 50; y < h; y += 50) {
        int len = (y % 100 == 0) ? 20 : 10;
        grid_bar(s_grid, 0, y, len, 1, tick);
        grid_bar(s_grid, w - len, y, len, 1, tick);
    }

    grid_bar(s_grid, w / 2, h / 2 - 30, 1, 60, cross);
    grid_bar(s_grid, w / 2 - 30, h / 2, 60, 1, cross);

    lv_obj_t *hint = lv_label_create(s_grid);
    lv_label_set_text(hint, "alignment grid — all four red edges must be visible");
    lv_obj_set_style_text_font(hint, &lv_font_ui_16, 0);
    lv_obj_set_style_text_color(hint, frame, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);
}
