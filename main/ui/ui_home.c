/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Main screen.
 *
 * Left column is "how things are now": weather, solar indices, R/S/G scales and
 * the Kp forecast. Right column is "what is coming and where to go": the
 * rotating warning card, then the band keypad, which doubles as the primary
 * navigation.
 *
 * Layout, 800x480, header occupies y 0..90:
 *   left    x  12..534    four stacked cards
 *   alerts  x 546..788    y 106..200, matching the weather card opposite
 *   keypad  x 546..788    12 bands, 4 wide, 3 rows ending clear of the edge
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lib/i18n.h"
#include "lib/solar.h"
#include "lib/station.h"
#include "lib/units.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

#define LEFT_X   12
#define LEFT_W  522
#define RIGHT_X 546
#define RIGHT_W 242

#define BAND_W  56
#define BAND_H  75
#define BAND_GAP 6

/*
 * The keypad's last row used to end at y=493 — thirteen pixels past the bottom
 * of the panel — because the MUF line above it pushed everything down. MUF has
 * since moved to the map pages, where it belongs next to the ionosonde that
 * measured it, and the rows now finish at 475.
 */
#define KEYPAD_LABEL_Y 216
#define KEYPAD_Y       238

/*
 * One card matching the LOCAL WEATHER block exactly, so the two columns align.
 * Warnings rotate through a single slot every few seconds rather than being
 * stacked: the card is narrow, and a queue of three truncated lines is harder
 * to read than one at a time. When nothing is wrong it says so explicitly —
 * silence would be ambiguous.
 */
#define ALERT_MAX      4
#define ALERT_Y        106
#define ALERT_CARD_H   94
#define ALERT_ROTATE_MS 3000

/* Sun times as three aligned rows rather than three positions chosen one at a
 * time, which is how sunset ended up 72 px right of sunrise. */
#define SUN_LABEL_X (LEFT_X + 348)
#define SUN_VALUE_X (LEFT_X + 430)
#define SUN_ROW_Y   118
#define SUN_ROW_H   26

static lv_obj_t *s_scr;

static lv_obj_t *s_wx_temp;
static lv_obj_t *s_wx_cond;
static lv_obj_t *s_wx_detail;
static lv_obj_t *s_sun_rise;
static lv_obj_t *s_sun_set;
static lv_obj_t *s_greyline;

static lv_obj_t *s_solar_val[7];
static lv_obj_t *s_scale_lvl[3];
static lv_obj_t *s_scale_note[3];
static lv_obj_t *s_kp_bar[SPACEWX_KP_PERIODS];
static lv_obj_t *s_kp_day[3];

/*
 * Three severities, so a glance is enough to know whether to act.
 *
 *   RED    something is happening now that should change what you do: a storm
 *          overhead, a severe geomagnetic event, wind that threatens antennas.
 *   YELLOW conditions are degrading, or forecast to. Worth knowing, not urgent.
 *   GREEN  nothing is wrong, and here is something good — a most-wanted entity
 *          just spotted on the cluster.
 *
 * Green is deliberately not merely "no warnings". A panel that only ever says
 * nothing teaches the eye to skip it; giving it something worth reading when all
 * is well is what keeps it being read when all is not.
 */
typedef enum { INFO_GREEN = 0, INFO_YELLOW, INFO_RED } info_level_t;

typedef struct {
    char title[40];
    char detail[64];
    info_level_t level;
} alert_t;

/*
 * Wind thresholds, on the Beaufort scale rather than round numbers.
 *
 * Force 8 is a gale at 17.2 m/s — the point at which a wire antenna's supports
 * are genuinely at risk and the answer is to lower the mast. Force 6, a strong
 * breeze at 10.8 m/s, is when it is worth going to look.
 */
#define WIND_GALE_MS   17.2f
#define WIND_STRONG_MS 10.8f

static alert_t s_alerts[ALERT_MAX];        /* what the panel is showing */
static int s_alert_count;
static alert_t s_sw_alerts[ALERT_MAX];     /* staged space-weather entries */
static int s_sw_alert_count;

/* Latest weather and the freshest most-wanted spot, kept so the panel can be
 * rebuilt when any one source updates without waiting for the others. */
static weather_current_t s_wx_now;
static bool s_have_wx;
/* Callsign plus entity name, which for the longest entities runs to about 45
 * characters ("Fed. Rep. of Germany" and friends). Sized to hold them whole
 * rather than truncate mid-word in the one line meant to catch the eye. */
static char s_rare_call[48];

static void rebuild_info(void);
static int s_alert_shown;
static lv_timer_t *s_alert_timer;
static lv_timer_t *s_greyline_timer;

static lv_obj_t *s_alert_bar;
static lv_obj_t *s_alert_title;
static lv_obj_t *s_alert_detail;
static lv_obj_t *s_alert_dots;

static lv_obj_t *s_band_btn[PROP_BAND_COUNT];
static lv_obj_t *s_band_bar[PROP_BAND_COUNT];
static lv_obj_t *s_band_cond[PROP_BAND_COUNT];
static lv_obj_t *s_band_grids[PROP_BAND_COUNT];

static void on_band(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_show_map(idx);
}

static lv_color_t cond_colour(prop_cond_t c)
{
    const theme_t *t = theme();
    switch (c) {
    case PROP_GOOD: return t->good;
    case PROP_FAIR: return t->fair;
    case PROP_POOR: return t->poor;
    default:        return t->muted;
    }
}

/**
 * @brief Render "HH:MM" out of an Open-Meteo local timestamp, in local format.
 *
 * The API returns "2026-08-02T05:32". Only the clock part is wanted, and it has
 * to pass through the unit system so a USA setting shows "5:32 am".
 */
static void set_clock_from_iso(lv_obj_t *label, const char *iso)
{
    const char *tpos = iso ? strchr(iso, 'T') : NULL;
    if (!tpos) {
        lv_label_set_text(label, "--:--");
        return;
    }
    struct tm tm = {0};
    if (sscanf(tpos + 1, "%2d:%2d", &tm.tm_hour, &tm.tm_min) != 2) {
        lv_label_set_text(label, "--:--");
        return;
    }
    char buf[16];
    units_format_time(buf, sizeof(buf), &tm, false);
    lv_label_set_text(label, buf);
}

static void build_weather(void)
{
    const theme_t *t = theme();
    ui_card(s_scr, LEFT_X, 106, LEFT_W, 94);
    ui_label(s_scr, &lv_font_ui_12, t->muted, LEFT_X + 16, SUN_ROW_Y, T(S_LOCAL_WEATHER));

    s_wx_temp = ui_label(s_scr, &lv_font_ui_28, t->text, LEFT_X + 16, 146, "--");
    s_wx_cond = ui_label(s_scr, &lv_font_ui_18, t->accent, LEFT_X + 116, 142, "");
    s_wx_detail = ui_label(s_scr, &lv_font_ui_12, t->muted, LEFT_X + 16, 178, "");

    static const str_id_t k_rows[3] = {S_SUNRISE, S_SUNSET, S_GREYLINE};
    lv_obj_t **slot[3] = {&s_sun_rise, &s_sun_set, &s_greyline};
    const lv_color_t colour[3] = {t->fair, t->orange, t->text};

    for (int i = 0; i < 3; i++) {
        int y = SUN_ROW_Y + i * SUN_ROW_H;
        /* Label sits two pixels lower so the 12 px caption and the 16 px value
         * share a baseline rather than a top edge. */
        ui_label(s_scr, &lv_font_ui_12, t->muted, SUN_LABEL_X, y + 2, T(k_rows[i]));
        *slot[i] = ui_label(s_scr, &lv_font_ui_16, colour[i], SUN_VALUE_X, y, "--:--");
    }
}

static void build_solar(void)
{
    const theme_t *t = theme();
    static const char *k[7] = {"SFI", "A", "K", "X-ray", "Wind", "Bt", "Bz"};

    ui_card(s_scr, LEFT_X, 206, LEFT_W, 88);
    ui_label(s_scr, &lv_font_ui_12, t->muted, LEFT_X + 16, 218, T(S_SOLAR));
    for (int i = 0; i < 7; i++) {
        int x = LEFT_X + 16 + i * 71;
        ui_label(s_scr, &lv_font_ui_12, t->muted, x, 242, k[i]);
        s_solar_val[i] = ui_label(s_scr, &lv_font_ui_24, t->text, x, 260, "--");
    }
}

static void build_scales(void)
{
    const theme_t *t = theme();
    static const str_id_t k[3] = {S_RADIO_BLACKOUT, S_SOLAR_RADIATION, S_GEOMAGNETIC};

    ui_card(s_scr, LEFT_X, 300, LEFT_W, 72);
    for (int i = 0; i < 3; i++) {
        int x = LEFT_X + 16 + i * 172;
        ui_label(s_scr, &lv_font_ui_12, t->muted, x, 312, T(k[i]));
        s_scale_lvl[i] = ui_label(s_scr, &lv_font_ui_24, t->good, x, 334, "--");
        s_scale_note[i] = ui_label(s_scr, &lv_font_ui_12, t->muted, x + 44, 342, "");
    }
}

static void build_kp(void)
{
    const theme_t *t = theme();
    ui_card(s_scr, LEFT_X, 378, LEFT_W, 94);
    ui_label(s_scr, &lv_font_ui_12, t->muted, LEFT_X + 16, 390, T(S_KP_FORECAST));
    for (int i = 0; i < SPACEWX_KP_PERIODS; i++) {
        s_kp_bar[i] = ui_box(s_scr, 16, 2, LEFT_X + 16 + i * 20, 454, t->muted, 2);
    }
    for (int d = 0; d < 3; d++) {
        s_kp_day[d] = ui_label_centre(s_scr, &lv_font_ui_12, t->muted,
                                      LEFT_X + 16 + d * 160, 456, 144, "");
    }
}

static void paint_alert(void)
{
    const theme_t *t = theme();

    /* The rotation timer is global and outlives the screen. After a rebuild it
     * keeps firing every three seconds against labels that were freed with the
     * old home page — the most reliable way to produce a fault that looks
     * periodic and unrelated to anything the operator did. */
    if (!s_scr || !s_alert_bar) {
        return;
    }

    if (s_alert_count == 0) {
        lv_obj_add_flag(s_alert_bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_alert_title, T(S_ALL_QUIET));
        lv_obj_set_style_text_color(s_alert_title, t->good, 0);
        lv_label_set_text(s_alert_detail, "");
        lv_label_set_text(s_alert_dots, "");
        return;
    }

    if (s_alert_shown >= s_alert_count) {
        s_alert_shown = 0;
    }
    const alert_t *a = &s_alerts[s_alert_shown];
    lv_color_t c = (a->level == INFO_RED)    ? t->poor
                   : (a->level == INFO_YELLOW) ? t->fair
                                               : t->good;

    lv_obj_clear_flag(s_alert_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_alert_bar, c, 0);
    lv_label_set_text(s_alert_title, a->title);
    lv_obj_set_style_text_color(s_alert_title, c, 0);
    lv_label_set_text(s_alert_detail, a->detail);

    /* Position within the rotation, so a passing glance can tell there is more. */
    if (s_alert_count > 1) {
        char dots[ALERT_MAX * 2 + 1] = {0};
        for (int i = 0; i < s_alert_count && i < ALERT_MAX; i++) {
            dots[i * 2] = (i == s_alert_shown) ? '*' : '.';
            dots[i * 2 + 1] = ' ';
        }
        lv_label_set_text(s_alert_dots, dots);
    } else {
        lv_label_set_text(s_alert_dots, "");
    }
}

static void alert_tick(lv_timer_t *t)
{
    (void)t;
    if (s_alert_count > 1) {
        s_alert_shown = (s_alert_shown + 1) % s_alert_count;
        paint_alert();
    }
}

/*
 * Time to the next grey line.
 *
 * This lived inside ui_home_set_weather() and was therefore recomputed only
 * when a forecast arrived — once every fifteen minutes. A countdown that
 * updates every fifteen minutes does not count down, it jumps, and it was worse
 * than that: since v1.1 the weather is only pushed to the UI when the fetch
 * succeeds, so a run of failed fetches froze the number completely while the
 * clock beside it carried on.
 *
 * Nothing here needs the weather. The grey line follows from the station's
 * position and the current time, both of which are always available.
 */
static void update_greyline(void)
{
    if (!s_scr || !s_greyline) {
        return;
    }
    const station_t *st = station_get();
    if (!st->configured) {
        return;
    }
    long secs = solar_seconds_to_greyline(st->pos, time(NULL));
    if (secs == 0) {
        lv_label_set_text(s_greyline, T(S_NOW));
    } else if (secs > 0) {
        lv_label_set_text_fmt(s_greyline, "%ldh %02ldm", secs / 3600, (secs % 3600) / 60);
    } else {
        lv_label_set_text(s_greyline, "--");
    }
}

/* Half a minute, so the displayed minute is never more than 30 s behind. The
 * work is one solar position and a label — far cheaper than the second-by-second
 * clock already running in the header. */
#define GREYLINE_TICK_MS 30000

static void greyline_tick(lv_timer_t *t)
{
    (void)t;
    update_greyline();
}

static void build_alerts(void)
{
    const theme_t *t = theme();
    ui_card(s_scr, RIGHT_X, ALERT_Y, RIGHT_W, ALERT_CARD_H);
    /* Heading on the same baseline as LOCAL WEATHER opposite. */
    ui_label(s_scr, &lv_font_ui_12, t->muted, RIGHT_X + 16, ALERT_Y + 12, T(S_INFO));

    /*
     * The rotation dots move up beside the heading, which frees the bottom of
     * the card for a second line of detail. That second line is not optional
     * padding: "HF degradation likely" fits on one line in English and its
     * Russian equivalent does not, and a warning that runs off the edge of its
     * own container is worse than no warning.
     *
     * The message block sits 4 px higher than before so the gap under the
     * WARNINGS heading reads as deliberate spacing rather than a collision.
     */
    s_alert_dots = ui_label_right(s_scr, &lv_font_ui_12, t->muted, RIGHT_X + RIGHT_W - 16,
                                  ALERT_Y + 12, "");

    s_alert_bar = ui_box(s_scr, 4, 44, RIGHT_X + 16, ALERT_Y + 34, t->poor, 2);
    s_alert_title = ui_label(s_scr, &lv_font_ui_14, t->good, RIGHT_X + 26, ALERT_Y + 32, "");
    s_alert_detail = ui_label(s_scr, &lv_font_ui_12, t->muted, RIGHT_X + 26, ALERT_Y + 52, "");
    lv_label_set_long_mode(s_alert_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_alert_detail, RIGHT_W - 42);

    if (!s_alert_timer) {
        s_alert_timer = lv_timer_create(alert_tick, ALERT_ROTATE_MS, NULL);
    }
    if (!s_greyline_timer) {
        s_greyline_timer = lv_timer_create(greyline_tick, GREYLINE_TICK_MS, NULL);
    }
    paint_alert();
}

static void build_keypad(void)
{
    const theme_t *t = theme();
    ui_label(s_scr, &lv_font_ui_12, t->muted, RIGHT_X, KEYPAD_LABEL_Y, T(S_BAND_CONDITIONS));

    for (int i = 0; i < PROP_BAND_COUNT; i++) {
        const prop_band_t *b = prop_band(i);
        int x = RIGHT_X + (i % 4) * (BAND_W + BAND_GAP);
        int y = KEYPAD_Y + (i / 4) * (BAND_H + BAND_GAP);

        /* NULL: the second registration below carries the band index, and
         * passing on_band twice would open the map twice per press. */
        s_band_btn[i] = ui_button(s_scr, x, y, BAND_W, BAND_H, NULL);
        lv_obj_add_event_cb(s_band_btn[i], on_band, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_band_bar[i] = ui_box(s_band_btn[i], BAND_W, 5, 0, 0, t->muted, 2);

        ui_label_centre(s_band_btn[i], &lv_font_ui_18, t->text, 0, 16, BAND_W, b->name);
        s_band_cond[i] = ui_label_centre(s_band_btn[i], &lv_font_ui_12, t->muted, 0, 42,
                                         BAND_W, "--");
        s_band_grids[i] = ui_label_centre(s_band_btn[i], &lv_font_ui_12, t->muted, 0, 58,
                                          BAND_W, "");
    }
}

static void on_screen_deleted(lv_event_t *e)
{
    (void)e;
    s_scr = NULL;
    s_alert_bar = NULL;
    s_alert_title = NULL;
    s_alert_detail = NULL;
    s_alert_dots = NULL;
}

lv_obj_t *ui_home_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_bg_color(s_scr, theme()->bg, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_header_build(s_scr, UI_PAGE_HOME);
    build_weather();
    build_solar();
    build_scales();
    build_kp();
    build_alerts();
    build_keypad();
    return s_scr;
}

/* ---- data in ------------------------------------------------------------ */

void ui_home_set_bands(const prop_band_state_t *bands)
{
    if (!s_scr || !bands) {
        return;
    }
    for (int i = 0; i < PROP_BAND_COUNT; i++) {
        lv_color_t c = cond_colour(bands[i].cond);
        lv_obj_set_style_bg_color(s_band_bar[i], c, 0);
        lv_label_set_text(s_band_cond[i], prop_cond_text(bands[i].cond));
        lv_obj_set_style_text_color(s_band_cond[i], c, 0);
        if (bands[i].grids > 0) {
            lv_label_set_text_fmt(s_band_grids[i], "%d", bands[i].grids);
        } else {
            lv_label_set_text(s_band_grids[i], "");
        }
    }
}

void ui_home_set_spacewx(const spacewx_t *sw)
{
    if (!s_scr || !sw || !sw->valid) {
        return;
    }
    const theme_t *t = theme();
    char buf[32];

    lv_label_set_text_fmt(s_solar_val[0], "%d", sw->sfi);
    lv_label_set_text_fmt(s_solar_val[1], "%d", sw->a_index);
    lv_label_set_text_fmt(s_solar_val[2], "%.0f", sw->kp);
    lv_label_set_text(s_solar_val[3], sw->xray[0] ? sw->xray : "--");
    lv_label_set_text_fmt(s_solar_val[4], "%d", sw->wind_speed);
    lv_label_set_text_fmt(s_solar_val[5], "%.0f", sw->bt);
    /* Signed: a southward Bz is what couples to Earth's field. */
    lv_label_set_text_fmt(s_solar_val[6], "%+.0f", sw->bz);

    lv_obj_set_style_text_color(s_solar_val[2], sw->kp >= 5.0f ? t->poor
                                                : (sw->kp >= 4.0f ? t->fair : t->good), 0);
    lv_obj_set_style_text_color(s_solar_val[6], sw->bz < -5.0f ? t->fair : t->good, 0);

    const int lv[3] = {sw->r_scale, sw->s_scale, sw->g_scale};
    for (int i = 0; i < 3; i++) {
        lv_label_set_text_fmt(s_scale_lvl[i], "%c%d", "RSG"[i], lv[i]);
        lv_obj_set_style_text_color(s_scale_lvl[i], lv[i] == 0 ? t->good
                                                    : (lv[i] < 3 ? t->fair : t->poor), 0);
        lv_label_set_text(s_scale_note[i], lv[i] == 0 ? T(S_NONE) : T(S_ACTIVE));
    }

    for (int i = 0; i < SPACEWX_KP_PERIODS; i++) {
        float v = sw->kp_forecast[i];
        int h = (int)(v / 9.0f * 44.0f);
        if (h < 2) {
            h = 2;
        }
        lv_obj_set_size(s_kp_bar[i], 16, h);
        lv_obj_set_pos(s_kp_bar[i], LEFT_X + 16 + i * 20, 454 - h);
        lv_obj_set_style_bg_color(s_kp_bar[i],
                                  v >= 5.0f ? t->poor : (v >= 4.0f ? t->fair : t->good), 0);
    }

    /* NOAA labels these "Aug 02"; convert so every date on the device reads in
     * whichever order the operator chose. */
    static const char *k_mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int d = 0; d < 3; d++) {
        char mon[8] = {0};
        int day = 0;
        if (sscanf(sw->forecast_days[d], "%3s %d", mon, &day) == 2) {
            int m = 0;
            for (int i = 0; i < 12; i++) {
                if (strncmp(mon, k_mon[i], 3) == 0) {
                    m = i + 1;
                    break;
                }
            }
            if (m) {
                units_format_date_short(buf, sizeof(buf), day, m);
                lv_label_set_text(s_kp_day[d], buf);
                continue;
            }
        }
        lv_label_set_text(s_kp_day[d], sw->forecast_days[d]);
    }

    /* Space weather entries are staged here rather than written straight to the
     * panel: the panel interleaves them with weather by severity, so it has to
     * see the whole set at once. */
    s_sw_alert_count = 0;

    if (sw->g_forecast > 0 && s_sw_alert_count < ALERT_MAX) {
        alert_t *a = &s_sw_alerts[s_sw_alert_count++];
        struct tm when = {0};
        char date[24];
        if (sscanf(sw->g_forecast_date, "%d-%d-%d", &when.tm_year, &when.tm_mon,
                   &when.tm_mday) == 3) {
            when.tm_year -= 1900;
            when.tm_mon -= 1;
            units_format_date(date, sizeof(date), &when);
        } else {
            strlcpy(date, sw->g_forecast_date, sizeof(date));
        }
        snprintf(a->title, sizeof(a->title), T(S_W_STORM_FC), sw->g_forecast);
        snprintf(a->detail, sizeof(a->detail), T(S_W_HF_DEGRADED), date);
        a->level = (sw->g_forecast >= 3) ? INFO_RED : INFO_YELLOW;
    }

    if (sw->r_scale > 0 && s_sw_alert_count < ALERT_MAX) {
        alert_t *a = &s_sw_alerts[s_sw_alert_count++];
        snprintf(a->title, sizeof(a->title), T(S_W_BLACKOUT), sw->r_scale);
        strlcpy(a->detail, T(S_W_SUNLIT), sizeof(a->detail));
        a->level = (sw->r_scale >= 3) ? INFO_RED : INFO_YELLOW;
    }

    if (sw->g_scale > 0 && s_sw_alert_count < ALERT_MAX) {
        alert_t *a = &s_sw_alerts[s_sw_alert_count++];
        snprintf(a->title, sizeof(a->title), T(S_W_STORM_NOW), sw->g_scale);
        strlcpy(a->detail, T(S_W_HIGHLAT), sizeof(a->detail));
        a->level = (sw->g_scale >= 3) ? INFO_RED : INFO_YELLOW;
    }

    if (sw->s_scale > 0 && s_sw_alert_count < ALERT_MAX) {
        alert_t *a = &s_sw_alerts[s_sw_alert_count++];
        snprintf(a->title, sizeof(a->title), T(S_W_RADIATION), sw->s_scale);
        strlcpy(a->detail, T(S_W_POLAR), sizeof(a->detail));
        a->level = (sw->s_scale >= 3) ? INFO_RED : INFO_YELLOW;
    }

    rebuild_info();
}

/*
 * ---- the info panel ----
 *
 * Rebuilt from every source at once rather than appended to, because the
 * severity ordering only makes sense globally: a gale outranks a geomagnetic
 * forecast, and both outrank a rare callsign appearing on the cluster.
 *
 * Sources are collected worst-first so the rotation opens on whatever matters
 * most, which is also what a passing glance sees.
 */
static void add_info(const char *title, const char *detail, info_level_t level)
{
    if (s_alert_count >= ALERT_MAX) {
        return;
    }
    alert_t *a = &s_alerts[s_alert_count++];
    strlcpy(a->title, title, sizeof(a->title));
    strlcpy(a->detail, detail, sizeof(a->detail));
    a->level = level;
}

static void rebuild_info(void)
{
    s_alert_count = 0;
    char buf[48];

    /* ---- red: happening now, act on it ---- */
    if (s_have_wx) {
        /* WMO codes 95, 96 and 99 are all thunderstorm. Any of them means a
         * feedline connected to an antenna is a lightning path into the shack,
         * which outranks every other thing this panel can say. */
        int code = s_wx_now.code;
        if (code == 95 || code == 96 || code == 99) {
            add_info(T(S_W_STORM), T(S_W_STORM_D), INFO_RED);
        }
        if (s_wx_now.wind_ms >= WIND_GALE_MS) {
            snprintf(buf, sizeof(buf), T(S_W_GALE), (double)units_speed(s_wx_now.wind_ms),
                     units_speed_suffix());
            add_info(buf, T(S_W_GALE_D), INFO_RED);
        }
    }

    for (int i = 0; i < s_sw_alert_count; i++) {
        if (s_sw_alerts[i].level == INFO_RED) {
            add_info(s_sw_alerts[i].title, s_sw_alerts[i].detail, INFO_RED);
        }
    }

    /* ---- yellow: degrading, or forecast to ---- */
    if (s_have_wx) {
        int code = s_wx_now.code;
        if (s_wx_now.wind_ms >= WIND_STRONG_MS && s_wx_now.wind_ms < WIND_GALE_MS) {
            snprintf(buf, sizeof(buf), T(S_W_WIND), (double)units_speed(s_wx_now.wind_ms),
                     units_speed_suffix());
            add_info(buf, T(S_W_WIND_D), INFO_YELLOW);
        }
        /* 61-65 steady rain, 80-82 showers. Wet weather raises the HF noise
         * floor; it is worth knowing before blaming the band. */
        if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) {
            add_info(T(S_W_RAIN), T(S_W_RAIN_D), INFO_YELLOW);
        }
    }

    for (int i = 0; i < s_sw_alert_count; i++) {
        if (s_sw_alerts[i].level == INFO_YELLOW) {
            add_info(s_sw_alerts[i].title, s_sw_alerts[i].detail, INFO_YELLOW);
        }
    }

    /* ---- green: nothing wrong, and something worth chasing ---- */
    if (s_rare_call[0]) {
        add_info(s_rare_call, T(S_SPOTTED), INFO_GREEN);
    }

    s_alert_shown = 0;
    paint_alert();
}

void ui_home_set_dx(const dx_feed_t *feed)
{
    /*
     * Only most-wanted entities reach the panel. Every spot would be a ticker,
     * and a ticker is the one thing a status panel must not become — it would
     * push a gale warning off the screen within seconds.
     */
    s_rare_call[0] = '\0';
    if (feed && feed->valid) {
        for (int i = 0; i < feed->count; i++) {
            if (feed->spots[i].category == DX_RARE) {
                /* Callsign alone. The country is already implied by the call
                 * to anyone who would chase it, and the line has to read as one
                 * short statement rather than two competing facts. */
                strlcpy(s_rare_call, feed->spots[i].dx, sizeof(s_rare_call));
                break;      /* newest first, so the first match is the freshest */
            }
        }
    }
    if (s_scr) {
        rebuild_info();
    }
}

void ui_home_set_weather(const weather_data_t *wx)
{
    if (!s_scr || !wx) {
        return;
    }
    s_wx_now = wx->current;
    s_have_wx = true;
    rebuild_info();
    /* The reading used to be a bare number with no scale attached, which is
     * ambiguous the moment Fahrenheit is an option. */
    lv_label_set_text_fmt(s_wx_temp, "%d%s", (int)lroundf(units_temp(wx->current.temp_c)),
                          units_temp_suffix());
    lv_label_set_text(s_wx_cond, "");
    lv_label_set_text_fmt(s_wx_detail, "%s %d%s  ·  %d %%  ·  %.1f %s  ·  %d hPa",
                          T(S_FEELS), (int)lroundf(units_temp(wx->current.feels_c)),
                          units_temp_suffix(),
                          (int)lroundf(wx->current.humidity_pct),
                          (double)units_speed(wx->current.wind_ms), units_speed_suffix(),
                          (int)lroundf(wx->current.pressure_hpa));

    if (wx->day_count > 0) {
        set_clock_from_iso(s_sun_rise, wx->days[0].sunrise);
        set_clock_from_iso(s_sun_set, wx->days[0].sunset);
    }

    update_greyline();
}
