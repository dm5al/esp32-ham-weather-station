/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * DX cluster spots.
 *
 * A plain table and nothing else. Spots carry no coordinates — HamQTH gives a
 * country name and a DXCC number, not a position — so there is nothing
 * defensible to plot, and inventing a location from a country centroid would
 * put a Russian station somewhere in Siberia regardless of where it actually
 * transmitted. A table states exactly what is known.
 *
 * Newest first, because the value of a spot decays in minutes. Three pages of
 * twelve, paged rather than scrolled: a scroll position is invisible from
 * across the room, and a page number is not.
 *
 * Rows are built once and their text replaced on refresh. Rebuilding rows of
 * eight labels on every update would churn a hundred LVGL objects a minute.
 */
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>

#include "lib/i18n.h"
#include "lib/prefs.h"
#include "lib/units.h"
#include "net/dxcluster.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

#define ROWS      DX_ROWS_PER_PAGE
#define ROW_H     22
#define ROW_TOP   174
#define HDR_Y     152

/* Column left edges. The comment gets whatever is left, which is most of it —
 * it is the only field that carries anything a table cannot encode. */
#define COL_TIME  16
#define COL_FREQ  116    /* right edge: frequencies align on the decimal point */
#define COL_DX    128
#define COL_BAND  226
#define COL_MODE  272
#define COL_CTRY  322
#define COL_SPOT  462
#define COL_CMT   558

/* Control strip, level with the page title. */
#define BTN_Y     94
#define BTN_W     50
#define BTN_H     36
#define BTN_FILT  624
#define BTN_PREV  680
#define BTN_NEXT  736

static lv_obj_t *s_scr;
static lv_obj_t *s_time[ROWS];
static lv_obj_t *s_freq[ROWS];
static lv_obj_t *s_call[ROWS];
static lv_obj_t *s_band[ROWS];
static lv_obj_t *s_mode[ROWS];
static lv_obj_t *s_ctry[ROWS];
static lv_obj_t *s_spot[ROWS];
static lv_obj_t *s_cmt[ROWS];
static lv_obj_t *s_status;
static lv_obj_t *s_page_lbl;

/* Filter sheet, built on demand and destroyed on close. */
static lv_obj_t *s_sheet;
static lv_obj_t *s_band_btn[DXB_COUNT];
static lv_obj_t *s_mode_btn[DXM_COUNT];
static lv_obj_t *s_cat_btn[3];
static lv_obj_t *s_src_btn[DXS_COUNT];

static EXT_RAM_BSS_ATTR dx_feed_t s_feed;
/* Zeroed masks mean "no restriction"; sources start at HamQTH alone because it
 * is the one that carries continent, and therefore the only one that can tell
 * a near station from a distant one on its own. */
static dx_filter_t s_filter = {.sources = 1u << DXS_HAMQTH};
/* Snapshot taken when the sheet opens, so Cancel means something. */
static dx_filter_t s_filter_backup;
static int s_page;

/* Indices into s_feed that survive the filter, recomputed on any change. */
static int s_visible[DX_MAX_SPOTS];
static int s_visible_count;

/*
 * Category colours, matching the info panel on the home screen so the two mean
 * the same thing: green is the good news, amber is worth a look, plain text is
 * routine.
 *
 * Deliberately not WSJT-X's scheme. Its colours encode worked-before status
 * from your own log — New DXCC, New Call, New Grid — which needs a logbook this
 * device does not have and never will, since it is told nothing about what you
 * have worked. These say something the device can actually know.
 */
static void recompute_visible(void);
static void close_sheet(lv_event_t *e);

static lv_color_t category_colour(dx_category_t c)
{
    const theme_t *t = theme();
    switch (c) {
    case DX_RARE:    return t->good;
    case DX_DISTANT: return t->fair;
    default:         return t->text;
    }
}

static void recompute_visible(void)
{
    s_visible_count = 0;
    if (s_feed.valid) {
        for (int i = 0; i < s_feed.count && s_visible_count < DX_MAX_SPOTS; i++) {
            if (dx_spot_matches(&s_feed.spots[i], &s_filter)) {
                s_visible[s_visible_count++] = i;
            }
        }
    }
    int pages = (s_visible_count + ROWS - 1) / ROWS;
    if (pages < 1) {
        pages = 1;
    }
    if (s_page >= pages) {
        s_page = pages - 1;   /* filtering can shrink the list under our feet */
    }
}

static void on_page(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int pages = (s_visible_count + ROWS - 1) / ROWS;
    if (pages < 1) {
        pages = 1;
    }
    s_page = (s_page + delta + pages) % pages;   /* wraps both ways */
    ui_dx_refresh();
}

/* ---- filter sheet -------------------------------------------------------- */

static void paint_filter_buttons(void)
{
    const theme_t *t = theme();
    for (int i = 0; i < DXB_COUNT; i++) {
        bool on = (s_filter.bands == 0) || (s_filter.bands & (1u << i));
        lv_obj_set_style_bg_color(s_band_btn[i], on ? t->accent : t->card_hi, 0);
    }
    for (int i = 0; i < DXM_COUNT; i++) {
        bool on = (s_filter.modes == 0) || (s_filter.modes & (1u << i));
        lv_obj_set_style_bg_color(s_mode_btn[i], on ? t->accent : t->card_hi, 0);
    }
    for (int i = 0; i < 3; i++) {
        bool on = (s_filter.cats == 0) || (s_filter.cats & (1u << i));
        lv_obj_set_style_bg_color(s_cat_btn[i], on ? t->accent : t->card_hi, 0);
    }
    for (int i = 0; i < DXS_COUNT; i++) {
        bool on = (s_filter.sources & (1u << i)) != 0;
        lv_obj_set_style_bg_color(s_src_btn[i], on ? t->accent : t->card_hi, 0);
    }
}

static void on_toggle_band(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    /* An all-clear mask means "no restriction". Turning one off from that state
     * has to first select everything, or the single tap would read as "show only
     * the band I just switched off". */
    if (s_filter.bands == 0) {
        s_filter.bands = (1u << DXB_COUNT) - 1;
    }
    s_filter.bands ^= (1u << i);
    paint_filter_buttons();
    recompute_visible();
    ui_dx_refresh();
}

static void on_toggle_mode(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_filter.modes == 0) {
        s_filter.modes = (1u << DXM_COUNT) - 1;
    }
    s_filter.modes ^= (1u << i);
    paint_filter_buttons();
    recompute_visible();
    ui_dx_refresh();
}

static void on_toggle_category(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    /* All three are independent checkboxes: an operator may well want the rare
     * ones and the near ones and nothing in between. */
    if (s_filter.cats == 0) {
        s_filter.cats = 0x7;
    }
    s_filter.cats ^= (1u << i);
    paint_filter_buttons();
    recompute_visible();
    ui_dx_refresh();
}

static void on_toggle_source(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t want = s_filter.sources ^ (1u << i);
    /* Refuse to turn the last one off: an empty source set is a blank page with
     * no way back except through this same sheet. */
    if (want != 0) {
        s_filter.sources = want;
    }
    paint_filter_buttons();
    /* Takes effect on the next cycle; the current spots stay until then. */
}

uint8_t ui_dx_selected_sources(void)
{
    return s_filter.sources;
}

/** @brief Restore the filter as it was when the sheet opened. */
static void on_sheet_cancel(lv_event_t *e)
{
    s_filter = s_filter_backup;
    recompute_visible();
    ui_dx_refresh();
    close_sheet(e);
}

static void close_sheet(lv_event_t *e)
{
    (void)e;
    /* OK is the commit point. Cancel restores the snapshot first, so this saves
     * whatever the filter is by the time it runs, either way. */
    prefs_set_dx_filter(&s_filter, sizeof(s_filter));
    if (s_sheet) {
        /*
         * Async, because OK is a child of the sheet and this runs as its click
         * handler. Deleting the parent synchronously frees the button LVGL is
         * still dispatching the event on, and it carries on touching it after
         * this returns. Deferring to the next event loop lets the dispatch
         * finish against a live object.
         */
        lv_obj_delete_async(s_sheet);
        s_sheet = NULL;
    }
}

static void on_filter(lv_event_t *e)
{
    (void)e;
    if (s_sheet) {
        close_sheet(NULL);
        return;
    }
    const theme_t *t = theme();

    s_filter_backup = s_filter;

    /*
     * Covers everything below the header, not a floating card. The header is
     * left visible on purpose — callsign, both clocks and the way out stay
     * where they are on every other page, so the filter reads as part of the
     * device rather than as something that has taken it over.
     */
    s_sheet = ui_box(s_scr, 800, 390, 0, 90, t->card, 0);

    ui_label(s_sheet, &lv_font_ui_12, t->muted, 20, 16, T(S_DX_BAND));
    for (int i = 0; i < DXB_COUNT; i++) {
        lv_obj_t *b = ui_button(s_sheet, 20 + (i % 6) * 128, 38 + (i / 6) * 50, 120, 44, NULL);
        lv_obj_add_event_cb(b, on_toggle_band, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label_centre(b, &lv_font_ui_16, t->bg, 0, 12, 120, dx_band_name((dx_band_t)i));
        s_band_btn[i] = b;
    }

    ui_label(s_sheet, &lv_font_ui_12, t->muted, 20, 144, "MODE");
    for (int i = 0; i < DXM_COUNT; i++) {
        lv_obj_t *b = ui_button(s_sheet, 20 + i * 128, 166, 120, 44, NULL);
        lv_obj_add_event_cb(b, on_toggle_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label_centre(b, &lv_font_ui_16, t->bg, 0, 12, 120, dx_mode_name((dx_mode_t)i));
        s_mode_btn[i] = b;
    }

    ui_label(s_sheet, &lv_font_ui_12, t->muted, 20, 226, T(S_DX_COUNTRY));
    static const dx_category_t k_cats[3] = {DX_COMMON, DX_DISTANT, DX_RARE};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = ui_button(s_sheet, 20 + i * 164, 248, 156, 44, NULL);
        lv_obj_add_event_cb(b, on_toggle_category, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label_centre(b, &lv_font_ui_14, t->bg, 0, 14, 156, dx_category_name(k_cats[i]));
        s_cat_btn[i] = b;
    }

    ui_label(s_sheet, &lv_font_ui_12, t->muted, 524, 226, T(S_DX_SOURCES));
    for (int i = 0; i < DXS_COUNT; i++) {
        lv_obj_t *b = ui_button(s_sheet, 524 + i * 132, 248, 124, 44, NULL);
        lv_obj_add_event_cb(b, on_toggle_source, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label_centre(b, &lv_font_ui_14, t->bg, 0, 14, 124,
                        dx_source_name((dx_source_t)i));
        s_src_btn[i] = b;
    }

    /* Centred at the bottom, Cancel on the left: the destructive one is never
     * where the thumb lands by habit. */
    lv_obj_t *cancel = ui_button(s_sheet, 244, 316, 150, 46, on_sheet_cancel);
    lv_obj_set_style_bg_color(cancel, t->card_hi, 0);
    lv_obj_t *cl = ui_label(cancel, &lv_font_ui_16, t->text, 0, 0, T(S_CANCEL));
    lv_obj_center(cl);

    lv_obj_t *done = ui_button(s_sheet, 406, 316, 150, 46, close_sheet);
    lv_obj_set_style_bg_color(done, t->good, 0);
    lv_obj_t *dl = ui_label(done, &lv_font_ui_16, t->bg, 0, 0, T(S_OK));
    lv_obj_center(dl);

    paint_filter_buttons();
}

/** @brief Three horizontal bars. LVGL's symbol font has no hamburger glyph. */
static void draw_filter_icon(lv_obj_t *parent)
{
    const theme_t *t = theme();
    for (int i = 0; i < 3; i++) {
        ui_box(parent, 22, 3, 14, 11 + i * 7, t->text, 1);
    }
}

/* ---- screen -------------------------------------------------------------- */

/*
 * Screens are destroyed wholesale on a language or unit change, and every
 * pointer below belongs to the screen being freed. Nothing clears them, so the
 * one-minute spot refresh would go on writing into freed labels — a fault that
 * looks periodic and random because it fires on whichever cycle lands first.
 */
static void on_screen_deleted(lv_event_t *e)
{
    (void)e;
    s_scr = NULL;
    s_sheet = NULL;
    s_status = NULL;
    s_page_lbl = NULL;
}

lv_obj_t *ui_dx_create(void)
{
    const theme_t *t = theme();

    /* Restore the saved filter before anything reads it; sources especially,
     * since switching DXWatch on and losing it at the next boot is worse than
     * not offering the choice. */
    prefs_get_dx_filter(&s_filter, sizeof(s_filter));

    s_scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    s_sheet = NULL;

    ui_header_build(s_scr, UI_PAGE_DX);

    ui_label(s_scr, &lv_font_ui_24, t->text, 20, 98, "DX Cluster");

    lv_obj_t *fb = ui_button(s_scr, BTN_FILT, BTN_Y, BTN_W, BTN_H, on_filter);
    draw_filter_icon(fb);

    lv_obj_t *pb = ui_button(s_scr, BTN_PREV, BTN_Y, BTN_W, BTN_H, NULL);
    lv_obj_add_event_cb(pb, on_page, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_center(ui_label(pb, &lv_font_ui_20, t->text, 0, 0, LV_SYMBOL_LEFT));

    lv_obj_t *nb = ui_button(s_scr, BTN_NEXT, BTN_Y, BTN_W, BTN_H, NULL);
    lv_obj_add_event_cb(nb, on_page, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_center(ui_label(nb, &lv_font_ui_20, t->text, 0, 0, LV_SYMBOL_RIGHT));

    s_page_lbl = ui_label_right(s_scr, &lv_font_ui_12, t->muted, 788, BTN_Y + BTN_H + 4, "");

    ui_hline(s_scr, 12, HDR_Y + 18, 776, t->card_hi);
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_TIME, HDR_Y, "UTC");
    ui_label_right(s_scr, &lv_font_ui_12, t->muted, COL_FREQ, HDR_Y, T(S_DX_FREQ));
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_DX, HDR_Y, "DX");
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_BAND, HDR_Y, T(S_DX_BAND));
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_MODE, HDR_Y, "MODE");
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_CTRY, HDR_Y, T(S_DX_COUNTRY));
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_SPOT, HDR_Y, T(S_DX_SPOTTER));
    ui_label(s_scr, &lv_font_ui_12, t->muted, COL_CMT, HDR_Y, T(S_DX_COMMENT));

    for (int i = 0; i < ROWS; i++) {
        int y = ROW_TOP + i * ROW_H;
        /* Banded rows: at this density the eye loses the line otherwise. */
        if (i % 2 == 0) {
            ui_box(s_scr, 776, ROW_H, 12, y - 3, t->card, 4);
        }
        s_time[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_TIME, y, "");
        s_freq[i] = ui_label_right(s_scr, &lv_font_ui_14, t->accent, COL_FREQ, y, "");
        s_call[i] = ui_label(s_scr, &lv_font_ui_14, t->text, COL_DX, y, "");
        s_band[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_BAND, y, "");
        s_mode[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_MODE, y, "");
        s_ctry[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_CTRY, y, "");
        s_spot[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_SPOT, y, "");
        s_cmt[i] = ui_label(s_scr, &lv_font_ui_14, t->muted, COL_CMT, y, "");
        /* Clip rather than wrap: a long comment must never push the row height
         * out and cascade the whole table downward. */
        lv_label_set_long_mode(s_cmt[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_cmt[i], 230);
        lv_label_set_long_mode(s_ctry[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_ctry[i], 136);
    }

    /* Bottom right, beside the legend: a status line in the middle of an empty
     * table reads as a row that failed rather than as the state of the page. */
    s_status = ui_label_right(s_scr, &lv_font_ui_14, t->muted, 788, 449, T(S_DX_LOADING));

    /* Legend, clear of the last row which now ends at 425. */
    static const dx_category_t k_legend[3] = {DX_RARE, DX_DISTANT, DX_COMMON};
    for (int i = 0; i < 3; i++) {
        int x = 20 + i * 190;
        ui_box(s_scr, 10, 10, x, 452, category_colour(k_legend[i]), 0);
        ui_label(s_scr, &lv_font_ui_12, t->muted, x + 18, 449, dx_category_name(k_legend[i]));
    }

    recompute_visible();
    ui_dx_refresh();
    return s_scr;
}

void ui_dx_set_spots(const dx_feed_t *feed)
{
    if (feed) {
        s_feed = *feed;
        ui_header_set_dx_time(s_feed.updated);
    }
    recompute_visible();
    ui_dx_refresh();
}

static void clear_row(int i)
{
    lv_label_set_text(s_time[i], "");
    lv_label_set_text(s_freq[i], "");
    lv_label_set_text(s_call[i], "");
    lv_label_set_text(s_band[i], "");
    lv_label_set_text(s_mode[i], "");
    lv_label_set_text(s_ctry[i], "");
    lv_label_set_text(s_spot[i], "");
    lv_label_set_text(s_cmt[i], "");
}

void ui_dx_refresh(void)
{
    if (!s_scr) {
        return;
    }

    int pages = (s_visible_count + ROWS - 1) / ROWS;
    if (pages < 1) {
        pages = 1;
    }
    lv_label_set_text_fmt(s_page_lbl, "%d / %d  ·  %d", s_page + 1, pages, s_visible_count);

    if (s_visible_count == 0) {
        lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status, s_feed.updated ? T(S_DX_EMPTY) : T(S_DX_LOADING));
        for (int i = 0; i < ROWS; i++) {
            clear_row(i);
        }
        return;
    }
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < ROWS; i++) {
        int idx = s_page * ROWS + i;
        if (idx >= s_visible_count) {
            clear_row(i);
            continue;
        }
        const dx_spot_t *s = &s_feed.spots[s_visible[idx]];

        /* Spot times are published in UTC and stay in UTC whatever the unit
         * setting says: a cluster spot is a log entry, and logs are UTC. */
        if (s->hour >= 0) {
            lv_label_set_text_fmt(s_time[i], "%02d:%02d", s->hour, s->minute);
        } else {
            lv_label_set_text(s_time[i], "--:--");
        }
        lv_label_set_text_fmt(s_freq[i], "%.1f", (double)s->freq_khz);
        /* Category shows on the callsign itself rather than as a separate
         * column: it is the field the eye lands on, and a colour there costs no
         * width in a table that has none to spare. */
        lv_label_set_text(s_call[i], s->dx);
        lv_obj_set_style_text_color(s_call[i], category_colour(s->category), 0);
        lv_label_set_text(s_band[i], s->band);
        lv_label_set_text(s_mode[i], s->mode);
        lv_label_set_text(s_ctry[i], s->country);
        lv_label_set_text(s_spot[i], s->spotter);
        lv_label_set_text(s_cmt[i], s->comment);
    }
}
