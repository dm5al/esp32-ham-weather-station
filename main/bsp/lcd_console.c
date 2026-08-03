/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "bsp/lcd_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "bsp/lcd_timing.h"
#include "lib/station.h"
#include "lib/prefs.h"
#include "net/dxcluster.h"
#include "ui/ui.h"
#include "lvgl.h"
#include "esp_console.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd_console";

/* Every settable field, so `set` needs no per-field branching. */
typedef enum { F_U32, F_U16, F_U8 } field_width_t;

typedef struct {
    const char *name;
    size_t offset;
    field_width_t width;
    uint32_t min;
    uint32_t max;
    const char *help;
} field_t;

#define FIELD(name_, member_, width_, min_, max_, help_)                                           \
    {name_, offsetof(lcd_timing_t, member_), width_, min_, max_, help_}

static const field_t k_fields[] = {
    FIELD("pclk", pclk_mhz, F_U32, 1, 40, "pixel clock in MHz"),
    FIELD("hpw", hpw, F_U16, 1, 255, "HSYNC pulse width"),
    FIELD("hbp", hbp, F_U16, 0, 255, "H back porch - moves picture RIGHT"),
    FIELD("hfp", hfp, F_U16, 0, 255, "H front porch"),
    FIELD("vpw", vpw, F_U16, 1, 255, "VSYNC pulse width"),
    FIELD("vbp", vbp, F_U16, 0, 255, "V back porch - moves picture DOWN"),
    FIELD("vfp", vfp, F_U16, 0, 255, "V front porch"),
    FIELD("bblines", bb_lines, F_U16, 0, 48, "bounce buffer height (0=off, must divide 480)"),
    FIELD("hpol", hsync_idle_low, F_U8, 0, 1, "HSYNC idles low"),
    FIELD("vpol", vsync_idle_low, F_U8, 0, 1, "VSYNC idles low"),
    FIELD("depol", de_idle_high, F_U8, 0, 1, "DE idles high"),
    FIELD("pclkneg", pclk_active_neg, F_U8, 0, 1, "latch on falling PCLK edge"),
};

#define FIELD_COUNT (sizeof(k_fields) / sizeof(k_fields[0]))

static uint32_t field_get(const lcd_timing_t *t, const field_t *f)
{
    const void *p = (const uint8_t *)t + f->offset;
    switch (f->width) {
    case F_U32: return *(const uint32_t *)p;
    case F_U16: return *(const uint16_t *)p;
    default:    return *(const uint8_t *)p;
    }
}

static void field_set(lcd_timing_t *t, const field_t *f, uint32_t v)
{
    void *p = (uint8_t *)t + f->offset;
    switch (f->width) {
    case F_U32: *(uint32_t *)p = v; break;
    case F_U16: *(uint16_t *)p = (uint16_t)v; break;
    default:    *(uint8_t *)p = (uint8_t)v; break;
    }
}

static const field_t *field_find(const char *name)
{
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        if (strcmp(k_fields[i].name, name) == 0) {
            return &k_fields[i];
        }
    }
    return NULL;
}

static void print_timings(void)
{
    lcd_timing_t t;
    lcd_timing_load(&t);

    printf("\nPanel timings (%s)\n", lcd_timing_is_overridden() ? "from NVS" : "compiled defaults");
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        printf("  %-8s %5" PRIu32 "   %s\n", k_fields[i].name, field_get(&t, &k_fields[i]),
               k_fields[i].help);
    }

    /* Total line/frame time is what the panel actually has to agree with. */
    uint32_t htotal = t.hpw + t.hbp + BSP_LCD_H_RES + t.hfp;
    uint32_t vtotal = t.vpw + t.vbp + BSP_LCD_V_RES + t.vfp;
    uint32_t refresh = (t.pclk_mhz * 1000000UL) / (htotal * vtotal);
    printf("  htotal %" PRIu32 ", vtotal %" PRIu32 ", refresh ~%" PRIu32 " Hz\n\n", htotal, vtotal,
           refresh);
}

static int cmd_lcd(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "show") == 0) {
        print_timings();
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        lcd_timing_clear();
        printf("Cleared. Rebooting on compiled defaults...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }

    if (strcmp(argv[1], "grid") == 0) {
        bool on = (argc >= 3) && strcmp(argv[2], "on") == 0;
        if (bsp_display_lock(1000)) {
            bsp_alignment_grid(on);
            bsp_display_unlock();
            printf("Alignment grid %s\n", on ? "on" : "off");
        } else {
            printf("Could not take the display lock\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4 || ((argc - 2) % 2) != 0) {
            printf("Usage: lcd set <field> <value> [<field> <value> ...]\n");
            return 1;
        }

        lcd_timing_t t;
        lcd_timing_load(&t);

        /* Validate everything before writing anything — a half-applied set that
         * then reboots into a bad mode is a miserable thing to debug. */
        for (int i = 2; i < argc; i += 2) {
            const field_t *f = field_find(argv[i]);
            if (!f) {
                printf("Unknown field '%s'. Try: lcd show\n", argv[i]);
                return 1;
            }
            char *end = NULL;
            unsigned long v = strtoul(argv[i + 1], &end, 0);
            if (!end || *end != '\0') {
                printf("'%s' is not a number\n", argv[i + 1]);
                return 1;
            }
            if (v < f->min || v > f->max) {
                printf("%s must be %" PRIu32 "..%" PRIu32 "\n", f->name, f->min, f->max);
                return 1;
            }
            field_set(&t, f, (uint32_t)v);
        }

        /* Guard the one constraint the min/max range cannot express. */
        if (t.bb_lines && (BSP_LCD_V_RES % t.bb_lines) != 0) {
            printf("bblines must divide %d evenly (try 8, 10, 12, 16, 20, 24)\n", BSP_LCD_V_RES);
            return 1;
        }

        esp_err_t err = lcd_timing_save(&t);
        if (err != ESP_OK) {
            printf("Save failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("Saved. Rebooting to apply...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }

    printf("Usage:\n"
           "  lcd [show]                  show timings in use\n"
           "  lcd set <field> <val> ...   change, save and reboot\n"
           "  lcd reset                   forget override, reboot on defaults\n"
           "  lcd grid on|off             alignment overlay\n");
    return 0;
}

/* GT911 register map, the few that matter for diagnosis. */
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS     0x814E

static int cmd_touch(int argc, char **argv)
{
    esp_lcd_touch_handle_t tp = bsp_touch_handle();
    if (!tp) {
        printf("Touch not initialised\n");
        return 1;
    }

    uint8_t id[4] = {0};
    if (bsp_touch_read_reg(GT911_REG_PRODUCT_ID, id, sizeof(id)) == ESP_OK) {
        printf("GT911 product id: '%c%c%c' cfg ver %u\n", id[0], id[1], id[2], id[3]);
    } else {
        printf("GT911 does not answer on I2C — check wiring/address\n");
        return 1;
    }

    int seconds = (argc >= 2) ? atoi(argv[1]) : 6;
    if (seconds < 1 || seconds > 60) {
        seconds = 6;
    }
    printf("Touch the panel now — polling for %d s\n", seconds);
    printf("  status = GT911 register 0x814E: bit7 = data ready, low nibble = point count\n\n");

    int samples = seconds * 20;
    int reported = 0;
    uint8_t last_status = 0xFF;

    for (int i = 0; i < samples; i++) {
        uint8_t status = 0;
        bsp_touch_read_reg(GT911_REG_STATUS, &status, 1);

        /* Same calls esp_lvgl_port makes, so this reflects what LVGL sees. */
        esp_lcd_touch_read_data(tp);
        uint16_t x[1] = {0};
        uint16_t y[1] = {0};
        uint8_t cnt = 0;
        bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 1);

        if (pressed && cnt > 0) {
            printf("  touch: x=%4u y=%4u  (raw status 0x%02x)\n", x[0], y[0], status);
            reported++;
        } else if (status != last_status && (status & 0x0F)) {
            /* Chip says it has points but the driver handed us nothing. */
            printf("  status 0x%02x reports %u point(s), driver returned none\n", status,
                   status & 0x0F);
        }
        last_status = status;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    printf("\n%d touch reports in %d s.\n", reported, seconds);
    if (reported == 0) {
        printf("Nothing registered. If 'status' stayed 0x00 the controller never saw a\n"
               "touch; if it showed points, the problem is downstream of the driver.\n");
    }
    return 0;
}

/*
 * Escape hatch for the station settings.
 *
 * Until the Settings page exists there is no way to correct a mistyped locator
 * from the panel, and a wrong locator poisons every bearing and band query. A
 * console command is a small price for not being stuck.
 */
static int cmd_station(int argc, char **argv)
{
    const station_t *st = station_get();

    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        station_clear();
        printf("Station cleared. Reboot to re-run commissioning.\n");
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "set") == 0) {
        const char *qth = (argc >= 5) ? argv[3] : "";
        const char *loc = (argc >= 5) ? argv[4] : argv[3];
        if (station_set(argv[2], qth, loc) != ESP_OK) {
            printf("Rejected: '%s' is not a valid Maidenhead locator.\n", loc);
            return 1;
        }
        st = station_get();
        printf("Saved %s / %s / %s  ->  %.4f %.4f\n", st->call, st->qth, st->locator,
               st->pos.lat, st->pos.lon);
        return 0;
    }

    if (!st->configured) {
        printf("Not commissioned.\n");
    } else {
        printf("callsign   %s\n", st->call);
        printf("qth        %s\n", st->qth);
        printf("locator    %s  ->  %.4f %.4f\n", st->locator, st->pos.lat, st->pos.lon);
        printf("grid field %s   (the only part ever sent to any service)\n",
               station_grid_field());
    }
    printf("\nUsage:\n"
           "  station                          show\n"
           "  station set <call> <qth> <loc>   e.g. station set DM5AL Unnau JO30wp\n"
           "  station clear                    forget, re-run commissioning\n");
    return 0;
}

/*
 * Fetch and dump cluster spots.
 *
 * The DX page only fetches when it is opened, which makes the parser awkward to
 * exercise: there is no way to reach it without a finger on the panel. This
 * runs the same code path and prints every field, so a parsing fault shows up
 * as wrong text here rather than as a plausible-looking table on the screen.
 */
static int cmd_dx(int argc, char **argv)
{
    /* Source mask, so each feed can be exercised on its own. Merged, the two
     * overlap almost completely and a broken parser would hide behind the
     * deduplication as "0 new spots". */
    uint8_t sources = (1u << DXS_HAMQTH) | (1u << DXS_DXWATCH);
    if (argc >= 2) {
        sources = (uint8_t)atoi(argv[1]);
    }

    static EXT_RAM_BSS_ATTR dx_feed_t feed;
    esp_err_t err = dx_fetch(&feed, sources);
    if (err != ESP_OK) {
        printf("Fetch failed: %s\n", esp_err_to_name(err));
        printf("These are volunteer services; treat absence as normal.\n");
        return 1;
    }

    printf("%-6s %9s  %-12s %-5s %-5s %-16s %-10s %-8s %s\n",
           "UTC", "kHz", "DX", "BAND", "MODE", "COUNTRY", "SPOTTER", "SOURCE", "COMMENT");
    for (int i = 0; i < feed.count; i++) {
        const dx_spot_t *d = &feed.spots[i];
        printf("%02d:%02d  %9.1f  %-12s %-5s %-5s %-16s %-10s %-8s %s\n",
               d->hour, d->minute, (double)d->freq_khz, d->dx, d->band, d->mode,
               d->country, d->spotter, dx_source_name((dx_source_t)d->source),
               d->comment);
    }
    printf("\n%d spots. No callsign or identifier was sent to obtain them.\n", feed.count);
    printf("Usage: dx [mask]   1 = HamQTH, 2 = DXWatch, 3 = both\n");
    return 0;
}

/*
 * Open a band's propagation map.
 *
 * Exists to make the repaint measurable. The map is otherwise only reachable by
 * touching a band button, so the one operation heavy enough to disturb the LCD
 * scan was also the one that could not be triggered while watching the log.
 */
static int cmd_map(int argc, char **argv)
{
    int band = (argc >= 2) ? atoi(argv[1]) : 6;
    if (!prop_band(band)) {
        printf("Band index out of range.\n");
        return 1;
    }
    /* Optional projection, so both repaint paths can be timed without a finger
     * on the panel: 0 azimuthal, 1 grey line. */
    if (argc >= 3) {
        prefs_set_projection(atoi(argv[2]) ? MAP_EQUIRECT : MAP_AZIMUTHAL);
    }
    if (!bsp_display_lock(2000)) {
        printf("Could not take the display lock.\n");
        return 1;
    }
    ui_show_map(band);
    bsp_display_unlock();
    printf("Opened %s. Watch for the repaint timing.\n", prop_band(band)->name);
    return 0;
}

/*
 * Navigate, so a screenshot can be taken of a page without reaching for the
 * panel. The map has its own command because it needs a band and a projection.
 */
static int cmd_page(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: page home|dx|settings\n");
        return 1;
    }
    if (!bsp_display_lock(2000)) {
        printf("Could not take the display lock.\n");
        return 1;
    }
    if (strcmp(argv[1], "home") == 0) {
        ui_show_home();
    } else if (strcmp(argv[1], "dx") == 0) {
        ui_show_dx();
    } else if (strcmp(argv[1], "settings") == 0) {
        ui_show_settings();
    } else if (strcmp(argv[1], "station") == 0) {
        ui_show_station_setup();
    } else if (strcmp(argv[1], "wifi") == 0) {
        ui_show_wifi_setup();
    } else if (strcmp(argv[1], "about") == 0) {
        /* A sheet rather than a screen, and it can only exist on top of
         * Settings, so bring that up first. */
        ui_show_settings();
        ui_settings_open_about();
    } else {
        bsp_display_unlock();
        printf("Unknown page '%s'. Try home, dx, settings, station, wifi or about.\n",
               argv[1]);
        return 1;
    }
    bsp_display_unlock();
    printf("Showing %s.\n", argv[1]);
    return 0;
}

/*
 * Dump the frame buffer as a screenshot.
 *
 * A photograph of an LCD is never square-on, never colour-accurate and always
 * shows the backlight; reading the pixels gives exactly what was drawn. The
 * cost is the link: 800x480 at two bytes is 768 KB, and 115200 baud moves about
 * 11 KB a second, so raw would take over a minute and any glitch would ruin it.
 *
 * So it is run-length encoded. UI screens are mostly flat card and background
 * colour and collapse by one or two orders of magnitude; the propagation maps
 * are the worst case and still compress, since sea and land are each one colour
 * per shade band.
 *
 * The wire format is deliberately dull, one hex line per run — colour, then how
 * many pixels of it — bracketed by markers so the host can find the payload
 * among the log output that keeps arriving while this runs.
 */
static int cmd_shot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!bsp_display_lock(2000)) {
        printf("Could not take the display lock.\n");
        return 1;
    }
    /*
     * Repaint twice.
     *
     * There are two frame buffers and LVGL alternates between them, so one
     * full redraw fills whichever is currently the back buffer and leaves the
     * other holding whatever it last had. Reading a fixed buffer after a single
     * refresh therefore captures a screen that is part current and part
     * stale — a correct header over an empty body, in the case that found this.
     * A second round paints the other one.
     */
    for (int pass = 0; pass < 2; pass++) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
    }
    const uint16_t *fb = bsp_display_framebuffer();
    bsp_display_unlock();

    if (!fb) {
        printf("No frame buffer.\n");
        return 1;
    }

    const size_t px = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES;
    printf("\n#SHOT %d %d\n", BSP_LCD_H_RES, BSP_LCD_V_RES);

    size_t runs = 0;
    size_t i = 0;
    while (i < px) {
        uint16_t c = fb[i];
        size_t n = 1;
        while (i + n < px && fb[i + n] == c && n < 0xFFFE) {
            n++;
        }
        printf("%04X%04X\n", c, (unsigned)n);
        runs++;
        i += n;
        /* The console task must not hold the CPU for the whole transfer. */
        if ((runs & 0x3F) == 0) {
            vTaskDelay(1);
        }
    }
    printf("#ENDSHOT %u\n", (unsigned)runs);
    return 0;
}

esp_err_t lcd_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "weather>";
    repl_cfg.max_cmdline_length = 128;
    /*
     * The default 4 KB is fine for reading registers but not for "dx", which
     * opens a TLS connection from this task — mbedTLS alone wants more than
     * that for its handshake buffers, and the REPL panicked with a stack
     * overflow the first time the command ran. The app task does the same work
     * on 8 KB; the extra here covers line editing on top.
     */
    repl_cfg.task_stack_size = 9216;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_console_cmd_t cmd = {
        .command = "lcd",
        .help = "Show or tune the RGB panel timings",
        .hint = NULL,
        .func = cmd_lcd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    const esp_console_cmd_t touch_cmd = {
        .command = "touch",
        .help = "Poll the GT911 and report raw touch data",
        .hint = "[seconds]",
        .func = cmd_touch,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&touch_cmd));

    const esp_console_cmd_t station_cmd = {
        .command = "station",
        .help = "Show or correct the callsign, QTH and locator",
        .hint = NULL,
        .func = cmd_station,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&station_cmd));

    const esp_console_cmd_t dx_cmd = {
        .command = "dx",
        .help = "Fetch DX cluster spots from HamQTH and dump every parsed field",
        .hint = NULL,
        .func = cmd_dx,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&dx_cmd));

    const esp_console_cmd_t map_cmd = {
        .command = "map",
        .help = "Open a band's propagation map and report the repaint time",
        .hint = "[band index 0-11]",
        .func = cmd_map,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&map_cmd));

    const esp_console_cmd_t page_cmd = {
        .command = "page",
        .help = "Show a page: home, dx, settings, station or wifi",
        .hint = "<home|dx|settings|station|wifi>",
        .func = cmd_page,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&page_cmd));

    const esp_console_cmd_t shot_cmd = {
        .command = "shot",
        .help = "Dump the framebuffer as RLE hex; see tools/grab_screenshot.py",
        .hint = NULL,
        .func = cmd_shot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&shot_cmd));
    ESP_ERROR_CHECK(esp_console_register_help_command());

    ESP_LOGI(TAG, "console ready — type 'lcd' to see the panel timings");
    return esp_console_start_repl(repl);
}
