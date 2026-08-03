/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * The header, identical on every page.
 *
 * Callsign large on the left with QTH and locator as one line beneath it, both
 * clocks in the middle, and data freshness on the right. The two clocks are the
 * same size: neither is subordinate, and UTC is not a footnote to an operator
 * filling in a log.
 *
 * The left button is the whole navigation. On home it reads DX and opens the
 * cluster; on every other page it is a home icon and returns. That is why no
 * page has a Back button — one fixed position, one destination, and nothing to
 * learn.
 *
 * The Wi-Fi SSID and signal level used to sit here and were removed: once the
 * network is joined they never change and told the operator nothing. What does
 * matter is whether the data is current, so the space went to the update times.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/i18n.h"
#include "lib/station.h"
#include "lib/units.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

/* Only show a time once SNTP has set the clock. A plausible wrong time is worse
 * than an obvious placeholder. */
#define CLOCK_VALID_AFTER 1577836800   /* 2020-01-01 UTC */

/*
 * Column geometry. The callsign runs from the left margin to SEP1 and is the
 * largest text on the device, so it gets the space first; the clocks then split
 * what remains evenly and are centred within their own column rather than left
 * aligned against the divider. Widening the callsign is purely a matter of
 * moving SEP1 right — the clocks follow, and no font size changes.
 */
#define SEP1     340
#define SEP2     502
#define SEP3     664
#define CLOCK_W  (SEP2 - SEP1)
#define RIGHT_X  680

typedef struct {
    lv_obj_t *call;
    lv_obj_t *where;      /* "JO30xp · Unnau" as one string */
    lv_obj_t *utc_time;
    lv_obj_t *utc_date;
    lv_obj_t *loc_time;
    lv_obj_t *loc_date;
    lv_obj_t *updated;
    lv_obj_t *next;
} header_t;

static header_t s_hdr[UI_PAGE_COUNT];
static int s_utc_offset;
static time_t s_updated;
static time_t s_next;
static time_t s_dx_updated;
static time_t s_dx_next;
static lv_timer_t *s_tick;
static ui_page_t s_active_page;

/**
 * @brief The one navigation control: DX from home, home from anywhere else.
 *
 * The page is carried in the callback's user data rather than read from the
 * active page, so a header built for a screen that is not yet showing still
 * behaves correctly.
 */
static void on_nav(lv_event_t *e)
{
    ui_page_t from = (ui_page_t)(intptr_t)lv_event_get_user_data(e);
    if (from == UI_PAGE_HOME) {
        ui_show_dx();
    } else {
        ui_show_home();
    }
}

static void on_settings(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

void ui_header_build(lv_obj_t *scr, ui_page_t page)
{
    const theme_t *t = theme();
    header_t *h = &s_hdr[page];
    memset(h, 0, sizeof(*h));

    h->call = ui_label(scr, &lv_font_ui_48, t->text, 20, 2, "");
    h->where = ui_label(scr, &lv_font_ui_16, t->accent, 20, 62, "");

    /*
     * The clocks are left aligned at a fixed offset, not centred.
     *
     * Montserrat's digits are proportional — a 1 is narrower than a 0 — so a
     * centred string re-centres itself every time the seconds tick and the
     * whole clock jitters left and right. Anchoring the left edge confines the
     * movement to the last digit, where it belongs.
     *
     * The offset is chosen so the usual string still sits visually centred in
     * its column; UTC is always 24-hour, while the local clock may carry an
     * "am"/"pm" and therefore starts further left.
     */
    const int utc_pad = 24;
    const int loc_pad = units_clock_12h() ? 6 : 24;

    ui_vline(scr, SEP1, 6, 78, t->card_hi);
    ui_label_centre(scr, &lv_font_ui_12, t->muted, SEP1, 4, CLOCK_W, "UTC");
    h->utc_time = ui_label(scr, &lv_font_ui_28, t->text, SEP1 + utc_pad, 20, "--:--:--");
    h->utc_date = ui_label_centre(scr, &lv_font_ui_16, t->muted, SEP1, 56, CLOCK_W, "");

    ui_vline(scr, SEP2, 6, 78, t->card_hi);
    ui_label_centre(scr, &lv_font_ui_12, t->muted, SEP2, 4, CLOCK_W, T(S_LOCAL_LBL));
    h->loc_time = ui_label(scr, &lv_font_ui_28, t->text, SEP2 + loc_pad, 20, "--:--:--");
    h->loc_date = ui_label_centre(scr, &lv_font_ui_16, t->muted, SEP2, 56, CLOCK_W, "");

    ui_vline(scr, SEP3, 6, 78, t->card_hi);

    lv_obj_t *nav = ui_button(scr, RIGHT_X, 4, 52, 40, NULL);
    lv_obj_add_event_cb(nav, on_nav, LV_EVENT_CLICKED, (void *)(intptr_t)page);
    lv_obj_t *nav_lbl = ui_label(nav, page == UI_PAGE_HOME ? &lv_font_ui_20 : &lv_font_ui_24,
                                 t->text, 0, 0,
                                 page == UI_PAGE_HOME ? "DX" : LV_SYMBOL_HOME);
    lv_obj_center(nav_lbl);

    lv_obj_t *s_btn = ui_button(scr, RIGHT_X + 56, 4, 52, 40, on_settings);
    lv_obj_t *gear = ui_label(s_btn, &lv_font_ui_24, t->text, 0, 0, LV_SYMBOL_SETTINGS);
    lv_obj_center(gear);

    h->updated = ui_label(scr, &lv_font_ui_12, t->muted, RIGHT_X, 50, "");
    h->next = ui_label(scr, &lv_font_ui_12, t->muted, RIGHT_X, 66, "");

    ui_hline(scr, 0, 90, 800, t->card_hi);
    ui_header_refresh(page);
}

void ui_header_set_utc_offset(int seconds)
{
    s_utc_offset = seconds;
}

void ui_header_set_link(wifi_mgr_state_t state, int8_t rssi)
{
    /* Kept so callers need not change; the header no longer shows link detail. */
    (void)state;
    (void)rssi;
}

void ui_header_set_refresh_times(time_t updated, time_t next)
{
    s_updated = updated;
    s_next = next;
}

/*
 * The DX page keeps its own clock. Propagation and space weather run on a
 * fifteen-minute cycle, but cluster spots are fetched when the page opens and
 * decay in minutes — showing the propagation timestamp there would claim the
 * spots were fresher, or staler, than they are.
 */
void ui_header_set_dx_time(time_t updated)
{
    s_dx_updated = updated;
}

void ui_header_set_dx_next(time_t next)
{
    s_dx_next = next;
}

void ui_header_set_active(ui_page_t page)
{
    s_active_page = page;
}

/**
 * @brief Repaint the header of one page from current state.
 *
 * Driven by a one-second timer so the seconds actually advance — previously
 * this only ran when data arrived, which left the clock frozen between
 * fifteen-minute refreshes.
 */
void ui_header_refresh(ui_page_t page)
{
    header_t *h = &s_hdr[page];
    if (!h->call) {
        return;
    }
    const station_t *st = station_get();
    char buf[32];

    lv_label_set_text(h->call, st->configured ? st->call : "----");
    if (st->configured) {
        if (st->qth[0]) {
            lv_label_set_text_fmt(h->where, "%s  ·  %s", st->locator, st->qth);
        } else {
            lv_label_set_text(h->where, st->locator);
        }
    } else {
        lv_label_set_text(h->where, T(S_NOT_COMMISSIONED));
    }

    time_t now = time(NULL);
    if (now < CLOCK_VALID_AFTER) {
        lv_label_set_text(h->utc_time, "--:--:--");
        lv_label_set_text(h->loc_time, "--:--:--");
        lv_label_set_text(h->utc_date, T(S_WAITING_TIME));
        lv_label_set_text(h->loc_date, "");
    } else {
        /* UTC keeps the 24-hour clock whatever the unit setting says. It is a
         * reference scale for logging, and "3:15 pm UTC" is not something any
         * operator writes down. The date beside it still follows the setting. */
        struct tm g;
        gmtime_r(&now, &g);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
        lv_label_set_text(h->utc_time, buf);
        units_format_date(buf, sizeof(buf), &g);
        lv_label_set_text(h->utc_date, buf);

        time_t local = now + s_utc_offset;
        struct tm l;
        gmtime_r(&local, &l);
        units_format_time(buf, sizeof(buf), &l, true);
        lv_label_set_text(h->loc_time, buf);
        units_format_date(buf, sizeof(buf), &l);
        lv_label_set_text(h->loc_date, buf);
    }

    /* On the DX page the freshness shown is the cluster's, not the refresh
     * cycle's, and there is no scheduled next fetch: it happens on open. */
    if (page == UI_PAGE_DX) {
        if (s_dx_updated == 0) {
            lv_label_set_text_fmt(h->updated, "%s %s", T(S_UPDATED), T(S_NEVER));
            lv_label_set_text(h->next, "");
        } else if (now >= CLOCK_VALID_AFTER) {
            struct tm d;
            time_t ld = s_dx_updated + s_utc_offset;
            gmtime_r(&ld, &d);
            units_format_time(buf, sizeof(buf), &d, false);
            lv_label_set_text_fmt(h->updated, "%s %s", T(S_UPDATED), buf);
            /* Counted in seconds, not minutes: the whole cycle is one minute,
             * so a minute-resolution countdown would read "in 1 min" or "now"
             * and nothing else. */
            long secs = (s_dx_next > now) ? (long)(s_dx_next - now) : 0;
            lv_label_set_text_fmt(h->next, "%s %lds", T(S_NEXT), secs);
        }
        return;
    }

    if (s_updated == 0) {
        lv_label_set_text_fmt(h->updated, "%s %s", T(S_UPDATED), T(S_NEVER));
        lv_label_set_text(h->next, "");
    } else if (now >= CLOCK_VALID_AFTER) {
        struct tm u;
        time_t lu = s_updated + s_utc_offset;
        gmtime_r(&lu, &u);
        units_format_time(buf, sizeof(buf), &u, false);
        lv_label_set_text_fmt(h->updated, "%s %s", T(S_UPDATED), buf);

        long mins = s_next > now ? (s_next - now + 59) / 60 : 0;
        if (mins > 0) {
            snprintf(buf, sizeof(buf), T(S_IN_MIN), mins);
            lv_label_set_text_fmt(h->next, "%s %s", T(S_NEXT), buf);
        } else {
            lv_label_set_text_fmt(h->next, "%s %s", T(S_NEXT), T(S_NOW));
        }
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    ui_header_refresh(s_active_page);
}

void ui_header_start_clock(void)
{
    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, 1000, NULL);
    }
}
