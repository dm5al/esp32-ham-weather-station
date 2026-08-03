/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Settings.
 *
 * Station identity, language, unit system and the network. The map
 * projection used to live here and no longer does: it is now a pair of buttons
 * on the map itself, where the effect of pressing them is visible immediately.
 * A setting whose result you cannot see from the settings page is in the wrong
 * place.
 *
 * The data sources are listed at the bottom. For a project whose whole claim is
 * "measured, not modelled", saying where the numbers come from belongs on the
 * device and not only in a README.
 */
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* Pulled in for their version macros alone, so the About screen reports what was
 * actually linked rather than what someone remembered to write down. */
#include "cJSON.h"
#include "esp_idf_version.h"
#include "mbedtls/build_info.h"

#include "bsp/board_pins.h"

#include "lib/i18n.h"
#include "lib/prefs.h"
#include "lib/station.h"
#include "lib/units.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.settings";

/* Bumped by hand; there is no build system version to inherit from. */
#define APP_VERSION "1.1"

static lv_obj_t *s_scr;
static lv_obj_t *s_call;
static lv_obj_t *s_qth;
static lv_obj_t *s_lang_btn[LANG_COUNT];
static lv_obj_t *s_lang_lbl[LANG_COUNT];
static lv_obj_t *s_unit_btn[UNITS_COUNT];
static lv_obj_t *s_unit_lbl[UNITS_COUNT];
static lv_obj_t *s_net;
static lv_obj_t *s_rssi;

static void paint_choice_row(lv_obj_t **btn, lv_obj_t **lbl, int count, int selected)
{
    const theme_t *t = theme();
    for (int i = 0; i < count; i++) {
        bool on = (i == selected);
        lv_obj_set_style_bg_color(btn[i], on ? t->accent : t->card_hi, 0);
        lv_obj_set_style_text_color(lbl[i], on ? t->bg : t->text, 0);
    }
}

static void paint_choices(void)
{
    paint_choice_row(s_lang_btn, s_lang_lbl, LANG_COUNT, (int)prefs_lang());
    paint_choice_row(s_unit_btn, s_unit_lbl, UNITS_COUNT, (int)prefs_units());
}

static void on_language(lv_event_t *e)
{
    prefs_set_lang((lang_t)(intptr_t)lv_event_get_user_data(e));
    /* Every label on every screen was built with the old strings, so the only
     * honest response is to build them again. Deferred, because deleting this
     * screen while its own event is still being dispatched is not safe. */
    ui_rebuild_async();
}

static void on_units(lv_event_t *e)
{
    prefs_set_units((unit_system_t)(intptr_t)lv_event_get_user_data(e));
    ui_rebuild_async();
}

/*
 * Factory reset, behind a confirmation.
 *
 * This erases the callsign, the locator, the saved network and every display
 * preference — everything the operator ever typed. A single tap is not enough
 * of a gate for that, and an accidental one cannot be undone.
 */
static void close_msgbox(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}

static void on_reset_confirmed(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "factory reset: erasing NVS and restarting");
    nvs_flash_erase();
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void on_factory_reset(lv_event_t *e)
{
    (void)e;
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_msgbox_add_title(box, T(S_FACTORY_RESET));
    lv_msgbox_add_text(box, T(S_CONFIRM_RESET));

    lv_obj_t *no = lv_msgbox_add_footer_button(box, T(S_CANCEL));
    lv_obj_add_event_cb(no, close_msgbox, LV_EVENT_CLICKED, box);

    lv_obj_t *yes = lv_msgbox_add_footer_button(box, T(S_OK));
    lv_obj_add_event_cb(yes, on_reset_confirmed, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(yes, theme()->poor, 0);
}

/*
 * About.
 *
 * Three jobs, and only the first is courtesy. The licence requires the author
 * credit to survive redistribution, so it has to be reachable from the interface
 * — a LICENSE file is no use on a panel with no keyboard and no file manager.
 * The contact address is here so an operator holding a misbehaving device can
 * find it without the repository.
 *
 * The third job is the component list, and it is the one worth explaining. The
 * EU Cyber Resilience Act obliges manufacturers of products with digital
 * elements to publish a bill of materials; a noncommercial hobby project is out
 * of scope and owes nobody this. It is here anyway, because the argument for an
 * SBOM does not actually depend on who is compelled to produce one: when the
 * next mbedTLS advisory lands, the only question that matters is which version
 * is on the device, and the person holding it should not have to rebuild the
 * firmware to find out.
 *
 * Every version below one comes from the compiler, read out of the dependency's
 * own header, so it cannot drift from what was actually linked. A hand-copied
 * SBOM is worse than none: it is wrong silently, and it is trusted.
 *
 * The contact address appears here and nowhere else. It is deliberately absent
 * from the licence and the documentation: those are public and get scraped,
 * whereas this is on a panel in someone's shack.
 */
static void close_sheet(lv_event_t *e)
{
    lv_obj_del((lv_obj_t *)lv_event_get_user_data(e));
}

/* One row of the component list: name left, version right-aligned against the
 * column edge so the numbers line up and can be scanned down. */
static void component_row(lv_obj_t *parent, int x, int y, int w, const char *name,
                          const char *version)
{
    const theme_t *t = theme();
    ui_label(parent, &lv_font_ui_14, t->muted, x, y, name);
    lv_obj_t *v = ui_label(parent, &lv_font_ui_14, t->text, x, y, version);
    lv_obj_set_width(v, w);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
}

static void on_about(lv_event_t *e)
{
    (void)e;
    const theme_t *t = theme();

    /* Modal by construction: a full-screen catcher takes every press that is
     * not on the card, so the settings underneath cannot be operated blind. */
    lv_obj_t *shade = ui_box(s_scr, 800, 480, 0, 0, t->bg, 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);

    ui_card(shade, 40, 30, 720, 420);

    ui_label(shade, &lv_font_ui_24, t->text, 64, 48, "Ham Weather Station");

    /* ---- left column: who, how to reach them, on what terms ---- */
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 90, T(S_AUTHOR));
    ui_label(shade, &lv_font_ui_18, t->text, 64, 108, "DM5AL");
    ui_label(shade, &lv_font_ui_14, t->muted, 64, 134,
             "with tremendous help from Claude Opus 5");

    ui_label(shade, &lv_font_ui_12, t->muted, 64, 170, T(S_CONTACT));
    ui_label(shade, &lv_font_ui_16, t->accent, 64, 188, "support@dm5al.de");

    ui_label(shade, &lv_font_ui_12, t->muted, 64, 224, T(S_LICENCE));
    ui_label(shade, &lv_font_ui_14, t->text, 64, 242, "PolyForm Noncommercial 1.0.0");
    /*
     * "Source available", not "open source", and the distinction is not
     * pedantry: the OSI and FSF definitions both forbid restricting the field of
     * endeavour, so no licence can forbid selling and be open source. Claiming
     * otherwise on the device would be a claim the LICENSE file contradicts.
     */
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 264,
             "Source available. Free for personal, club, educational");
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 280,
             "and research use. Selling it, in any form, is not");
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 296,
             "permitted. This credit must be preserved.");
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 316, "No warranty of any kind.");

    /* The services themselves are listed on the Settings page behind this sheet;
     * repeating them here would cost the room this statement needs. */
    ui_label(shade, &lv_font_ui_12, t->muted, 64, 342, T(S_PRIVACY_TITLE));
    lv_obj_t *priv = ui_label(shade, &lv_font_ui_12, t->good, 64, 360,
                              T(S_PRIVACY_WEATHER));
    lv_obj_set_width(priv, 340);
    lv_label_set_long_mode(priv, LV_LABEL_LONG_WRAP);

    /* ---- right column: firmware and its bill of materials ---- */
    const int cx = 430;
    const int cw = 300;

    ui_label(shade, &lv_font_ui_12, t->muted, cx, 90, T(S_FIRMWARE_VER));
    ui_label(shade, &lv_font_ui_18, t->text, cx, 108, "v" APP_VERSION);
    ui_label(shade, &lv_font_ui_14, t->muted, cx, 134, BOARD_NAME);

    ui_label(shade, &lv_font_ui_12, t->muted, cx, 170, T(S_COMPONENTS));

    char buf[32];
    int y = 190;

    component_row(shade, cx, y, cw, "ESP-IDF", IDF_VER);
    y += 20;
    /* Ships as "V10.5.1"; the leading V would read as part of the number. */
    component_row(shade, cx, y, cw, "FreeRTOS", tskKERNEL_VERSION_NUMBER + 1);
    y += 20;

    snprintf(buf, sizeof(buf), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH);
    component_row(shade, cx, y, cw, "LVGL", buf);
    y += 20;

    component_row(shade, cx, y, cw, "Mbed TLS", MBEDTLS_VERSION_STRING);
    y += 20;

    snprintf(buf, sizeof(buf), "%d.%d.%d", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR,
             CJSON_VERSION_PATCH);
    component_row(shade, cx, y, cw, "cJSON", buf);
    y += 20;

    /*
     * These two publish no version macro, so unlike everything above they are
     * transcribed from dependencies.lock and can go stale. Kept because omitting
     * a component from a bill of materials is the worse failure; the version
     * requirement is pinned in main/idf_component.yml.
     */
    component_row(shade, cx, y, cw, "esp_lvgl_port", "2.6.3");
    y += 20;
    component_row(shade, cx, y, cw, "esp_lcd_touch_gt911", "1.1.3");
    y += 20;

    ui_label(shade, &lv_font_ui_12, t->muted, cx, y + 10, "Montserrat  ·  SIL OFL 1.1");

    /* NULL here on purpose: ui_button() would register the handler with no user
     * data, and the sheet to delete is passed as user data below. */
    lv_obj_t *close = ui_button(shade, 590, 392, 140, 40, NULL);
    lv_obj_set_style_bg_color(close, t->accent, 0);
    lv_obj_add_event_cb(close, close_sheet, LV_EVENT_CLICKED, shade);
    lv_obj_t *cl = ui_label(close, &lv_font_ui_16, t->bg, 0, 0, T(S_OK));
    lv_obj_center(cl);
}

void ui_settings_open_about(void)
{
    if (s_scr) {
        on_about(NULL);
    }
}

static void on_edit_station(lv_event_t *e)
{
    (void)e;
    ui_show_station_setup();
}

static void on_wifi(lv_event_t *e)
{
    (void)e;
    ui_show_wifi_setup();
}

static lv_obj_t *choice(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                        const lv_font_t *font, lv_event_cb_t cb, int index,
                        lv_obj_t **out_label)
{
    const theme_t *t = theme();
    lv_obj_t *b = ui_button(parent, x, y, w, h, NULL);
    lv_obj_set_style_bg_color(b, t->card_hi, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *l = ui_label_centre(b, font, t->text, 0, (h - 22) / 2, w, text);
    *out_label = l;
    return b;
}

static void on_screen_deleted(lv_event_t *e)
{
    (void)e;
    s_scr = NULL;
}

lv_obj_t *ui_settings_create(void)
{
    const theme_t *t = theme();

    s_scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_header_build(s_scr, UI_PAGE_SETTINGS);
    ui_label(s_scr, &lv_font_ui_24, t->text, 20, 100, T(S_SETTINGS));

    /* ---- station ---- */
    ui_card(s_scr, 12, 136, 380, 100);
    ui_label(s_scr, &lv_font_ui_12, t->muted, 28, 148, T(S_STATION));
    s_call = ui_label(s_scr, &lv_font_ui_24, t->text, 28, 168, "----");
    s_qth = ui_label(s_scr, &lv_font_ui_14, t->muted, 28, 202, "");

    lv_obj_t *edit = ui_button(s_scr, 268, 190, 110, 36, on_edit_station);
    lv_obj_set_style_bg_color(edit, t->card_hi, 0);
    lv_obj_t *el = ui_label(edit, &lv_font_ui_16, t->text, 0, 0, T(S_EDIT));
    lv_obj_center(el);

    /* ---- language, three across ---- */
    ui_card(s_scr, 400, 136, 388, 160);
    ui_label(s_scr, &lv_font_ui_12, t->muted, 416, 148, T(S_LANGUAGE));
    for (int i = 0; i < LANG_COUNT; i++) {
        /* Native names, so someone hunting for their own language scans for a
         * word they recognise rather than its English exonym. */
        s_lang_btn[i] = choice(s_scr, 416 + (i % 3) * 120, 168 + (i / 3) * 60, 112, 52,
                               lang_name((lang_t)i), &lv_font_ui_16, on_language, i,
                               &s_lang_lbl[i]);
    }

    /* ---- units ---- */
    ui_card(s_scr, 12, 244, 380, 70);
    ui_label(s_scr, &lv_font_ui_12, t->muted, 28, 254, T(S_UNITS));
    for (int i = 0; i < UNITS_COUNT; i++) {
        s_unit_btn[i] = choice(s_scr, 28 + i * 120, 272, 112, 34,
                               units_name((unit_system_t)i), &lv_font_ui_16, on_units, i,
                               &s_unit_lbl[i]);
    }

    /* ---- about and factory reset ----
     *
     * Their own card under Units, in the left column, rather than floating on
     * the footer line. Sharing that line with the privacy text meant the two
     * fought for the same pixels: the services list ran underneath the buttons
     * and the last line of the statement was cut off by the bottom edge. A
     * button and a paragraph should not be asked to occupy one row. */
    ui_card(s_scr, 12, 324, 380, 72);
    lv_obj_t *about = ui_button(s_scr, 28, 340, 168, 40, on_about);
    lv_obj_set_style_bg_color(about, t->card_hi, 0);
    lv_obj_t *al = ui_label(about, &lv_font_ui_14, t->text, 0, 0, T(S_ABOUT));
    lv_obj_center(al);

    lv_obj_t *reset = ui_button(s_scr, 208, 340, 168, 40, on_factory_reset);
    lv_obj_set_style_bg_color(reset, t->card_hi, 0);
    lv_obj_t *rl = ui_label(reset, &lv_font_ui_14, t->poor, 0, 0, T(S_FACTORY_RESET));
    lv_obj_center(rl);

    /* ---- wifi ---- */
    /* Under the language card and the same width. Name on the left, signal on
     * the right of the same line — the two belong together and neither needs a
     * label to say what it is. */
    ui_card(s_scr, 400, 306, 388, 100);
    ui_label(s_scr, &lv_font_ui_12, t->muted, 416, 316, "WIFI");
    s_net = ui_label(s_scr, &lv_font_ui_16, t->text, 416, 336, "");
    /*
     * Right-aligned inside its own narrow box. ui_label_right() anchors at x=0,
     * which for a card this far right made a 772 px wide label starting at the
     * screen edge — it drew, but overlapped everything to its left and was the
     * reason the signal reading never appeared where it was meant to.
     */
    s_rssi = ui_label(s_scr, &lv_font_ui_16, t->muted, 620, 336, "");
    lv_obj_set_width(s_rssi, 152);
    lv_obj_set_style_text_align(s_rssi, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *wifi = ui_button(s_scr, 622, 362, 150, 34, on_wifi);
    lv_obj_set_style_bg_color(wifi, t->accent, 0);
    lv_obj_t *wl = ui_label(wifi, &lv_font_ui_16, t->bg, 0, 0, T(S_CHANGE));
    lv_obj_center(wl);

    /* ---- footer ----
     *
     * Three lines with the whole width to themselves now that the buttons have
     * moved up, and the order is the argument: here is everyone the device talks
     * to, and here is what it does not tell them. The full statement — including
     * the coordinates the weather forecast needs — is on the About sheet, where
     * there is room to state it properly rather than truncate it. */
    ui_label(s_scr, &lv_font_ui_12, t->muted, 20, 412, T(S_SERVICES_USED));
    ui_label(s_scr, &lv_font_ui_12, t->text, 20, 430,
             "NOAA SWPC  ·  wspr.live  ·  prop.kc2g.com  ·  Open-Meteo  ·  HamQTH  ·  DXWatch");
    ui_label(s_scr, &lv_font_ui_12, t->good, 20, 452, T(S_PRIVACY));

    ui_settings_refresh();
    return s_scr;
}

void ui_settings_refresh(void)
{
    if (!s_scr) {
        return;
    }
    const station_t *st = station_get();
    lv_label_set_text(s_call, st->configured ? st->call : T(S_NOT_SET));
    if (st->configured) {
        lv_label_set_text_fmt(s_qth, "%s  ·  %s", st->qth[0] ? st->qth : "-", st->locator);
    } else {
        lv_label_set_text(s_qth, T(S_TAP_EDIT));
    }

    /*
     * Two labels on one line, not one label with a newline inside it. That
     * embedded "\n" is why the signal reading sat under the network name, and
     * why the right-aligned label looked broken — nothing ever wrote to it.
     */
    if (wifi_mgr_get_state() == WIFI_MGR_CONNECTED) {
        lv_label_set_text(s_net, wifi_mgr_current_ssid());
        lv_label_set_text_fmt(s_rssi, "%d dBm", wifi_mgr_rssi());
    } else {
        lv_label_set_text(s_net, T(S_OFFLINE));
        lv_label_set_text(s_rssi, "");
    }
    paint_choices();
}
