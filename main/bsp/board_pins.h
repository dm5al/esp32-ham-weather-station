/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Per-board wiring.
 *
 * Two panels are supported. They are both 800x480 RGB565 driven by the ESP32-S3
 * LCD_CAM peripheral, and everything above the BSP is identical between them —
 * but almost nothing below it is. The pin assignments share no numbers, the
 * timings differ, and the two boards do not agree on how the backlight and the
 * touch controller are even reached.
 *
 *   BOARD_WAVESHARE_7  Waveshare ESP32-S3-Touch-LCD-7, 7 inch.
 *                      Backlight and touch reset hang off a CH422G I2C
 *                      expander rather than GPIOs. The GT911's INT line is
 *                      wired, and is used to select the I2C address during
 *                      reset — which is the only reason touch works at 0x5D.
 *
 *   BOARD_SUNTON_5     Sunton ESP32-8048S050C, 5 inch.
 *                      No expander: backlight is a plain GPIO on an LEDC
 *                      channel, so it dims. The GT911's INT pin is NOT
 *                      connected on this board — there is an unpopulated R17
 *                      to GPIO18 for anyone who wants it — so the address
 *                      cannot be selected at reset and the driver must poll.
 *
 * Selected with -DBOARD=... at configure time; see tools/build_all.py, which
 * builds both into separate directories.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"

#if !defined(BOARD_WAVESHARE_7) && !defined(BOARD_SUNTON_5)
#define BOARD_WAVESHARE_7 1
#endif

#define BSP_LCD_H_RES 800
#define BSP_LCD_V_RES 480

/* ------------------------------------------------------------------------- */
#if defined(BOARD_SUNTON_5)

#define BOARD_NAME "Sunton ESP32-8048S050C 5\""

#define LCD_HSYNC_GPIO 39
#define LCD_VSYNC_GPIO 41
#define LCD_DE_GPIO    40
#define LCD_PCLK_GPIO  42

/* Blue 0-4, green 0-5, red 0-4, in the order the peripheral expects. */
#define LCD_DATA_GPIOS { 8, 3, 46, 9, 1,  5, 6, 7, 15, 16, 4,  45, 48, 47, 21, 14 }

#define TP_I2C_SDA 19
#define TP_I2C_SCL 20
#define TP_RST_GPIO 38
/* Not wired on this board. The driver polls, and the controller comes up on
 * its default address because nothing can hold INT during reset. */
#define TP_INT_GPIO GPIO_NUM_NC
#define BOARD_HAS_INT_ADDRESS_SELECT 0

/* A real GPIO, so the backlight dims instead of being on or off. */
#define BL_GPIO 2
#define BOARD_HAS_EXPANDER 0
#define BOARD_BACKLIGHT_PWM 1

/*
 * From the vendor's own ESP-IDF board support, and matched to how we drive it.
 *
 * That reference offers two pairings, and they are not interchangeable:
 *
 *   with bounce buffers     18 MHz from PLL240M
 *   without bounce buffers  14 MHz from PLL160M
 *
 * We use bounce buffers, so the first applies. An earlier 16 MHz on whatever
 * LCD_CLK_SRC_DEFAULT resolves to was a guess splitting the difference between
 * the two, and belonged to neither.
 */
/*
 * 24 MHz, above the reference's 18, and deliberately so.
 *
 * The reference value is conservative and yields only 40 Hz with these porches
 * — 18e6 / (887 * 507) — which is low enough to see as flicker. Raising the
 * clock is what buys refresh rate:
 *
 *     18 MHz -> 40 Hz     21 MHz -> 47 Hz     24 MHz -> 53 Hz
 *
 * The noise that appeared at 16 MHz was not caused by the rate: it went away
 * when only the clock source changed, from whatever LCD_CLK_SRC_DEFAULT
 * resolves to over to PLL240M. So there is room to go faster on this source.
 *
 * The cost is bus bandwidth — 24 MHz is 48 MB/s out of PSRAM against the
 * Waveshare's 42 — which is the thing that starves the bounce buffers and makes
 * the picture tremble.
 *
 * 24 MHz turned out to be over the line, and the symptom was not a tremble but
 * a DMA resync: press a button, the repaint blits into the PSRAM frame buffer,
 * the bounce buffers lose the race and the scan shifts — permanently, because
 * once the RGB DMA is out of step nothing puts it back before a restart. That is
 * why it looked like it "came and went": every reboot cleared it.
 *
 * So this sits at 21 MHz, the same 42 MB/s the Waveshare has always run at
 * without trouble. It costs refresh rate — 46 Hz against 53 — and that is the
 * cheaper thing to give up. The remaining lever is "lcd set bblines 20" for more
 * refill margin, at 25 KB of internal RAM that TLS would rather have.
 */
#define LCD_CLK_SOURCE LCD_CLK_SRC_PLL240M
#define LCD_DEFAULT_PCLK_MHZ 21
#define LCD_DEFAULT_HPW  7
#define LCD_DEFAULT_HBP  40
#define LCD_DEFAULT_HFP  40
#define LCD_DEFAULT_VPW  7
#define LCD_DEFAULT_VBP  10
#define LCD_DEFAULT_VFP  10
#define LCD_DEFAULT_PCLK_NEG 1

/* ------------------------------------------------------------------------- */
#else /* BOARD_WAVESHARE_7 */

#define BOARD_NAME "Waveshare ESP32-S3-Touch-LCD-7 7\""

#define LCD_HSYNC_GPIO 46
#define LCD_VSYNC_GPIO 3
#define LCD_DE_GPIO    5
#define LCD_PCLK_GPIO  7

#define LCD_DATA_GPIOS { 14, 38, 18, 17, 10,  39, 0, 45, 48, 47, 21,  1, 2, 42, 41, 40 }

#define TP_I2C_SDA 8
#define TP_I2C_SCL 9
/* Reset is on the expander; INT is a GPIO and is held low through reset to
 * latch the 0x5D address. */
#define TP_RST_GPIO GPIO_NUM_NC
#define TP_INT_GPIO 4
#define BOARD_HAS_INT_ADDRESS_SELECT 1

#define BL_GPIO GPIO_NUM_NC
#define BOARD_HAS_EXPANDER 1
#define BOARD_BACKLIGHT_PWM 0

#define LCD_CLK_SOURCE LCD_CLK_SRC_DEFAULT
#define LCD_DEFAULT_PCLK_MHZ 21
#define LCD_DEFAULT_HPW  4
#define LCD_DEFAULT_HBP  40
#define LCD_DEFAULT_HFP  20
#define LCD_DEFAULT_VPW  4
#define LCD_DEFAULT_VBP  16
#define LCD_DEFAULT_VFP  16
#define LCD_DEFAULT_PCLK_NEG 1

#endif

/* Expander lines, Waveshare only. Silkscreen EXIOn is chip IOn, so EXIO1 is
 * bit 1 — the off-by-one that costs an afternoon if assumed otherwise.
 *
 * The header is pulled in here rather than by the caller so this file stays
 * self-contained: whoever includes it gets a complete, consistent board. */
#if BOARD_HAS_EXPANDER
#include "bsp/ch422g.h"

#define EXIO_TP_RST  CH422G_IO1
#define EXIO_LCD_BL  CH422G_IO2   /* panel DISP: on/off only, no PWM */
#define EXIO_LCD_RST CH422G_IO3
#define EXIO_SD_CS   CH422G_IO4
#define EXIO_USB_SEL CH422G_IO5
#endif

#define TP_I2C_PORT I2C_NUM_0
#define TP_I2C_HZ   400000
