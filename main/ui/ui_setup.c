/*
 * Commissioning assistant.
 *
 * Three steps. Language and formats come first, before the network and before
 * identity, because every screen after this one is rendered in whatever is
 * chosen here — asking at the end would mean the operator read the whole
 * assistant in a language they may not have wanted.
 *
 * Wi-Fi and station both need text entry, so both carry an on-screen keyboard.
 *
 * The privacy promise is stated on the station step, next to the field where
 * the callsign is typed. That is the moment the user wonders where it goes, and
 * a README they will never read is not an answer.
 */
#include <string.h>

#include "esp_log.h"
#include "lib/geo.h"
#include "lib/i18n.h"
#include "lib/prefs.h"
#include "lib/station.h"
#include "lib/units.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.setup";

typedef enum { STEP_FORMAT = 0, STEP_WIFI, STEP_STATION, STEP_COUNT } setup_step_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_body;          /* everything below the step rail */
static lv_obj_t *s_rail[STEP_COUNT];
static lv_obj_t *s_rail_lbl[STEP_COUNT];
static setup_step_t s_step = STEP_FORMAT;

/*
 * The step rail belongs to first-time commissioning, where the point is to show
 * how far through you are. Reached from Settings there is no sequence — you came
 * to change one thing — so the rail is hidden and the body takes the whole
 * screen, with OK and Cancel instead of Finish.
 */
static bool s_assistant = true;

/* Format step */
static lv_obj_t *s_lang_btn[LANG_COUNT];
static lv_obj_t *s_lang_lbl[LANG_COUNT];
static lv_obj_t *s_unit_btn[UNITS_COUNT];
static lv_obj_t *s_unit_lbl[UNITS_COUNT];

/* Wi-Fi step */
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_kb;
static lv_obj_t *s_pass_ta;
static wifi_mgr_ap_t s_aps[WIFI_MGR_MAX_APS];
static size_t s_ap_count;
static char s_pending_ssid[WIFI_MGR_SSID_MAX + 1];

/* Station step */
static lv_obj_t *s_call_ta;
static lv_obj_t *s_qth_ta;
static lv_obj_t *s_loc_ta;
static lv_obj_t *s_loc_hint;
static lv_obj_t *s_finish_btn;
static lv_obj_t *s_rail_rule;

static void build_step(void);
static void refresh_locator_hint(void);
static void on_cancel(lv_event_t *e);

/*
 * Entry points, one per reason to open this page. Both rebuild the body, since
 * the screen is cached and would otherwise reopen wherever it was left.
 */
static void set_rail_visible(bool on);

void ui_setup_goto_wifi(void)
{
    s_step = STEP_WIFI;
    s_assistant = false;
    set_rail_visible(false);
    if (s_body) {
        build_step();
        /* Arriving here means "change the network", and the list is stale the
         * moment it is shown. Scan first rather than making the operator ask. */
        const ui_cmd_t cmd = {.type = UI_CMD_WIFI_SCAN};
        ui_post_cmd(&cmd);
    }
}

void ui_setup_goto_station(void)
{
    s_step = STEP_STATION;
    s_assistant = false;
    set_rail_visible(false);
    if (s_body) {
        build_step();
    }
}

/* ---- step rail ----------------------------------------------------------- */

static void set_rail_visible(bool on)
{
    for (int i = 0; i < STEP_COUNT; i++) {
        if (!s_rail[i]) {
            return;
        }
        if (on) {
            lv_obj_clear_flag(s_rail[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_rail_lbl[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rail[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_rail_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_rail_rule) {
        if (on) {
            lv_obj_clear_flag(s_rail_rule, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rail_rule, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_body) {
        lv_obj_set_pos(s_body, 0, on ? 60 : 12);
        lv_obj_set_size(s_body, 800, on ? 420 : 468);
    }
}

static void paint_rail(void)
{
    const theme_t *t = theme();
    /* Re-read the names every time: changing language on step one has to
     * relabel the rail as well as the body. */
    const char *names[STEP_COUNT] = {T(S_STEP_FORMAT), "Wi-Fi", T(S_STEP_STATION)};

    for (int i = 0; i < STEP_COUNT; i++) {
        bool active = (i <= (int)s_step);
        lv_obj_set_style_bg_color(s_rail[i], active ? t->accent : t->card, 0);
        lv_label_set_text(s_rail_lbl[i], names[i]);
        lv_obj_set_style_text_color(s_rail_lbl[i], i == (int)s_step ? t->text : t->muted, 0);
    }
}

/* ---- format step --------------------------------------------------------- */

static void paint_format_choices(void)
{
    const theme_t *t = theme();
    for (int i = 0; i < LANG_COUNT; i++) {
        bool on = (i == (int)prefs_lang());
        lv_obj_set_style_bg_color(s_lang_btn[i], on ? t->accent : t->card_hi, 0);
        lv_obj_set_style_text_color(s_lang_lbl[i], on ? t->bg : t->text, 0);
    }
    for (int i = 0; i < UNITS_COUNT; i++) {
        bool on = (i == (int)prefs_units());
        lv_obj_set_style_bg_color(s_unit_btn[i], on ? t->accent : t->card_hi, 0);
        lv_obj_set_style_text_color(s_unit_lbl[i], on ? t->bg : t->text, 0);
    }
}

static void on_pick_language(lv_event_t *e)
{
    prefs_set_lang((lang_t)(intptr_t)lv_event_get_user_data(e));
    /* Only this screen exists during commissioning, and build_step() replaces
     * the body wholesale, so there is nothing to defer here. */
    build_step();
}

static void on_pick_units(lv_event_t *e)
{
    prefs_set_units((unit_system_t)(intptr_t)lv_event_get_user_data(e));
    build_step();
}

static void on_format_next(lv_event_t *e)
{
    (void)e;
    s_step = STEP_WIFI;
    build_step();
}

static void build_format_step(void)
{
    const theme_t *t = theme();

    ui_label(s_body, &lv_font_ui_24, t->text, 20, 0, T(S_LANG_UNITS_TITLE));

    ui_label(s_body, &lv_font_ui_12, t->muted, 20, 44, T(S_LANGUAGE));
    for (int i = 0; i < LANG_COUNT; i++) {
        lv_obj_t *b = ui_button(s_body, 20 + i * 128, 64, 122, 48, NULL);
        lv_obj_set_style_bg_color(b, t->card_hi, 0);
        lv_obj_add_event_cb(b, on_pick_language, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_lang_btn[i] = b;
        s_lang_lbl[i] = ui_label_centre(b, &lv_font_ui_16, t->text, 0, 14, 122,
                                        lang_name((lang_t)i));
    }

    ui_label(s_body, &lv_font_ui_12, t->muted, 20, 128, T(S_UNITS));
    /* Spelling out what each choice means beats making the operator guess
     * whether "UK" implies Celsius or Fahrenheit — it implies both, one each. */
    static const char *k_desc[UNITS_COUNT] = {
        "24 h  ·  °C  ·  km",
        "24 h  ·  °C  ·  mi",
        "12 h  ·  °F  ·  mi",
    };
    for (int i = 0; i < UNITS_COUNT; i++) {
        lv_obj_t *b = ui_button(s_body, 20 + i * 130, 148, 124, 58, NULL);
        lv_obj_set_style_bg_color(b, t->card_hi, 0);
        lv_obj_add_event_cb(b, on_pick_units, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_unit_btn[i] = b;
        s_unit_lbl[i] = ui_label_centre(b, &lv_font_ui_18, t->text, 0, 8, 124,
                                        units_name((unit_system_t)i));
        ui_label_centre(b, &lv_font_ui_12, t->muted, 0, 34, 124, k_desc[i]);
    }

    lv_obj_t *next = ui_button(s_body, 600, 240, 180, 48, on_format_next);
    lv_obj_set_style_bg_color(next, t->accent, 0);
    lv_obj_t *l = ui_label(next, &lv_font_ui_18, t->bg, 0, 0, T(S_NEXT_STEP));
    lv_obj_center(l);

    paint_format_choices();
}

/* ---- Wi-Fi step ---------------------------------------------------------- */

static void close_keyboard(void)
{
    if (s_kb) {
        lv_obj_delete(s_kb);
        s_kb = NULL;
    }
}

/** @brief Abandon the password sheet and go back to the network list. */
static void on_pass_cancel(lv_event_t *e)
{
    (void)e;
    build_step();
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) {
        return;
    }
    ui_cmd_t cmd = {.type = UI_CMD_WIFI_CONNECT};
    strlcpy(cmd.a, s_pending_ssid, sizeof(cmd.a));
    strlcpy(cmd.b, lv_textarea_get_text(s_pass_ta), sizeof(cmd.b));
    ui_post_cmd(&cmd);
    ui_setup_set_status(T(S_CONNECTING), false);
    build_step();   /* drop the password sheet */
}

static void on_kb_ready(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        on_connect(e);
    }
}

/*
 * Kill key auto-repeat.
 *
 * Set once here and again when the focus moves, and NEVER from inside a key
 * event. Re-applying it on every VALUE_CHANGED is what produced the doubled
 * sixth character, and the log of a single tap says so plainly:
 *
 *   30207  locator now "JO30w"  (5 chars)
 *   30210  locator now "JO30ww" (6 chars)
 *   30213  locator now "JO30ww" (6 chars)
 *
 * Three events in six milliseconds. Auto-repeat needs a 400 ms hold, so that
 * was never the cause; this is re-entrancy. The handler ran inside the
 * keyboard's own dispatch and called set_button_ctrl_all(), rewriting the
 * control bits of all thirty-odd buttons of a map the mode switch had just
 * rebuilt, and the matrix emitted the press again.
 *
 * It reproduced only on the fifth character because that is the first press
 * after switching back from the number map — and never on an all-letter
 * locator, which needs no mode switch at all. Both observations that looked
 * like autocomplete were pointing at the mode change the whole time.
 *
 * The original concern was real: a mode switch does clear these flags. But a
 * deliberate tap never approaches 400 ms, so losing them between mode changes
 * costs nothing, while restoring them mid-keypress cost a character.
 *
 * A button matrix re-sends LV_EVENT_VALUE_CHANGED on LONG_PRESSED_REPEAT, so
 * holding a key past the 400 ms threshold inserts the character twice, which a
 * deliberate tap on a touch panel crosses easily.
 *
 * Setting the flag once is not enough — which is why this looked fixed and was
 * not. lv_keyboard_set_mode() rebuilds the button map from scratch and the
 * per-button control flags go with it, so the first tap on ABC, 123 or shift
 * silently restores repeat.
 *
 * That is exactly why only the locator misbehaved. "JO30wp" forces two mode
 * changes: digits for the square, then lower case for the subsquare. The
 * callsign and QTH never leave a single map, so they never lost the flag —
 * matching the report that every other field typed correctly.
 *
 * Re-applying on every VALUE_CHANGED covers mode changes whatever triggers
 * them, including LVGL's own internal ones.
 */
static void suppress_repeat(lv_obj_t *kb)
{
    lv_buttonmatrix_set_button_ctrl_all(kb, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
}

static void style_keyboard(lv_obj_t *kb)
{
    const theme_t *t = theme();

    suppress_repeat(kb);

    lv_obj_set_style_bg_color(kb, t->bg, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 6, 0);
    lv_obj_set_style_text_font(kb, &lv_font_ui_20, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, t->card, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, t->text, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, t->accent, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
}

/*
 * What a station field may contain, in either case.
 *
 * Restricting these to capitals was worse than the problem it solved: a town
 * name is not written in block capitals, and a locator typed as JO30WP came
 * back as JO30wp with nothing to explain the change. Both cases are accepted
 * and neither is converted. Everything that reads these values already handles
 * case for itself.
 *
 * The Wi-Fi password has no restriction at all — passwords are case sensitive
 * and arbitrary, and none of this may touch them.
 */
#define STATION_CHARS     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/- ."

static lv_obj_t *make_textarea(lv_obj_t *parent, int x, int y, int w, const char *placeholder,
                               int max_len, bool password)
{
    const theme_t *t = theme();
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, 48);
    lv_obj_set_pos(ta, x, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_password_mode(ta, password);
    if (!password) {
        lv_textarea_set_accepted_chars(ta, STATION_CHARS);
    }
    lv_obj_set_style_text_font(ta, &lv_font_ui_20, 0);
    lv_obj_set_style_bg_color(ta, t->bg, 0);
    lv_obj_set_style_text_color(ta, t->text, 0);
    lv_obj_set_style_border_color(ta, t->card_hi, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, 9, 0);

    /*
     * Show which field the keyboard is typing into. Without this the only clue
     * is the caret, which is a single hairline on a 7-inch panel viewed at
     * arm's length — invisible in practice, and the reason it was possible to
     * type a callsign into the locator without noticing.
     *
     * Styled by state rather than by event handler so LVGL keeps the two in
     * step on its own: FOCUSED is set whenever the keyboard is attached.
     */
    lv_obj_set_style_border_color(ta, t->accent, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta, t->card, LV_STATE_FOCUSED);
    return ta;
}

static void on_ap_clicked(lv_event_t *e)
{
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_ap_count) {
        return;
    }
    strlcpy(s_pending_ssid, s_aps[idx].ssid, sizeof(s_pending_ssid));

    if (!s_aps[idx].secure) {
        ui_cmd_t cmd = {.type = UI_CMD_WIFI_CONNECT};
        strlcpy(cmd.a, s_pending_ssid, sizeof(cmd.a));
        ui_post_cmd(&cmd);
        ui_setup_set_status(T(S_CONNECTING), false);
        return;
    }

    /* Password sheet over the list, with the keyboard along the bottom. */
    const theme_t *t = theme();
    lv_obj_clean(s_body);
    s_list = NULL;

    ui_label(s_body, &lv_font_ui_20, t->text, 20, 8, T(S_PASSWORD_FOR));
    ui_label(s_body, &lv_font_ui_20, t->accent, 210, 8, s_pending_ssid);
    s_pass_ta = make_textarea(s_body, 20, 40, 640, T(S_NET_PASSWORD), WIFI_MGR_PASS_MAX, true);

    lv_obj_t *back = ui_button(s_body, 560, 40, 106, 48, on_pass_cancel);
    lv_obj_set_style_bg_color(back, t->card_hi, 0);
    lv_obj_t *bl = ui_label(back, &lv_font_ui_18, t->text, 0, 0, T(S_CANCEL));
    lv_obj_center(bl);

    lv_obj_t *btn = ui_button(s_body, 674, 40, 106, 48, on_connect);
    lv_obj_set_style_bg_color(btn, t->accent, 0);
    lv_obj_t *l = ui_label(btn, &lv_font_ui_18, t->bg, 0, 0, T(S_CONNECT));
    lv_obj_center(l);

    s_status = ui_label(s_body, &lv_font_ui_14, t->muted, 20, 96, "");

    s_kb = lv_keyboard_create(s_body);
    lv_obj_set_size(s_kb, 800, 240);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_kb, s_pass_ta);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_READY, NULL);
    style_keyboard(s_kb);
}

static void on_rescan(lv_event_t *e)
{
    (void)e;
    ui_setup_set_status(T(S_SCANNING), false);
    const ui_cmd_t cmd = {.type = UI_CMD_WIFI_SCAN};
    ui_post_cmd(&cmd);
}

static void build_wifi_step(void)
{
    const theme_t *t = theme();

    ui_label(s_body, &lv_font_ui_24, t->text, 20, 4, T(S_CHOOSE_WIFI));
    if (!s_assistant) {
        /* Reached from Settings, this is a dead end without it: the header's
         * home button goes to the main screen, not back where you came from. */
        lv_obj_t *back = ui_button(s_body, 430, 0, 170, 44, on_cancel);
        lv_obj_set_style_bg_color(back, t->card_hi, 0);
        /* "Back", not "Cancel": nothing is being abandoned here, the list is
         * just being left. Cancel belongs on the password sheet, where there is
         * a half-finished action to abandon. */
        lv_obj_t *bl = ui_label(back, &lv_font_ui_16, t->text, 0, 0, T(S_BACK));
        lv_obj_center(bl);
    }
    lv_obj_t *btn = ui_button(s_body, 610, 0, 170, 44, on_rescan);
    lv_obj_set_style_bg_color(btn, t->card_hi, 0);
    lv_obj_t *l = ui_label(btn, &lv_font_ui_16, t->text, 0, 0, T(S_RESCAN));
    lv_obj_center(l);

    s_list = lv_list_create(s_body);
    lv_obj_set_size(s_list, 760, 250);
    lv_obj_set_pos(s_list, 20, 48);
    lv_obj_set_style_bg_color(s_list, t->card, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_radius(s_list, 12, 0);
    lv_obj_set_style_pad_all(s_list, 8, 0);

    s_status = ui_label(s_body, &lv_font_ui_16, t->muted, 20, 306, T(S_SCANNING));
    ui_setup_set_aps(s_aps, s_ap_count);
}

/* ---- station step -------------------------------------------------------- */

static void refresh_locator_hint(void)
{
    if (!s_loc_ta || !s_loc_hint) {
        return;
    }
    const theme_t *t = theme();
    const char *loc = lv_textarea_get_text(s_loc_ta);
    geo_point_t p;

    if (loc[0] == '\0') {
        lv_label_set_text(s_loc_hint, T(S_LOC_HINT));
        lv_obj_set_style_text_color(s_loc_hint, t->muted, 0);
        lv_obj_add_state(s_finish_btn, LV_STATE_DISABLED);
    } else if (geo_from_locator(loc, &p)) {
        lv_label_set_text_fmt(s_loc_hint, "%.3f  %.3f", p.lat, p.lon);
        lv_obj_set_style_text_color(s_loc_hint, t->good, 0);
        lv_obj_clear_state(s_finish_btn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_loc_hint, T(S_LOC_BAD));
        lv_obj_set_style_text_color(s_loc_hint, t->poor, 0);
        lv_obj_add_state(s_finish_btn, LV_STATE_DISABLED);
    }
}

/**
 * @brief Move the caret to the end when a field is tapped.
 *
 * Tapping mid-string otherwise drops the cursor where the finger landed, so
 * the next key inserts into the middle of the value. On a prefilled locator
 * that is indistinguishable from a spurious character appearing.
 */
static void on_field_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);

    if (!s_kb) {
        return;
    }
    lv_keyboard_set_textarea(s_kb, ta);
    /* A callsign is upper case; a town name is not. */
    lv_keyboard_set_mode(s_kb, (ta == s_qth_ta) ? LV_KEYBOARD_MODE_TEXT_LOWER
                                                : LV_KEYBOARD_MODE_TEXT_UPPER);
    suppress_repeat(s_kb);
}


/*
 * Reject a second insertion that arrives on the heels of the first.
 *
 * A single tap has produced two characters through three different builds, and
 * three explanations for it have now been wrong. The measurements are however
 * consistent and decisive: the duplicate lands 3 ms after the original, and it
 * only ever happens on the first press after the keyboard changes its button
 * map. No human types two characters in 3 ms, and no touch panel reports a
 * deliberate tap twice that fast.
 *
 * So rather than chase the mechanism a fourth time, this refuses the outcome.
 * Any growth of exactly one character within 60 ms of the previous change is a
 * double dispatch and is undone. The threshold is far below anything a person
 * can produce — the fastest sustained typing is around 100 ms per character,
 * and that is on a full-travel keyboard, not a 7-inch panel.
 *
 * The suppression flag matters: lv_textarea_delete_char() raises its own
 * VALUE_CHANGED, and re-entering here is exactly the mistake that caused the
 * problem in the first place.
 */
#define DEDUP_WINDOW_MS 60

static void on_field_changed(lv_event_t *e)
{
    static uint32_t s_last_ms;
    static char s_last_text[24];
    static bool s_undoing;

    lv_obj_t *ta = lv_event_get_target(e);
    if (s_undoing) {
        return;
    }

    const char *txt = lv_textarea_get_text(ta);
    size_t len = strlen(txt);
    size_t prev = strlen(s_last_text);
    uint32_t since = lv_tick_elaps(s_last_ms);

    if (len == prev + 1 && since < DEDUP_WINDOW_MS && strncmp(txt, s_last_text, prev) == 0) {
        ESP_LOGW(TAG, "dropped duplicate '%c' %ums after the last change",
                 txt[len - 1], (unsigned)since);
        s_undoing = true;
        lv_textarea_delete_char(ta);
        s_undoing = false;
        refresh_locator_hint();
        return;
    }

    strlcpy(s_last_text, txt, sizeof(s_last_text));
    s_last_ms = lv_tick_get();

    if (ta == s_loc_ta && s_loc_ta) {
        ESP_LOGI(TAG, "locator now \"%s\" (%u chars)", txt, (unsigned)len);
    }
    refresh_locator_hint();
}

static void on_cancel(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void on_finish(lv_event_t *e)
{
    (void)e;
    ui_cmd_t cmd = {.type = UI_CMD_SET_STATION};
    strlcpy(cmd.a, lv_textarea_get_text(s_call_ta), sizeof(cmd.a));
    strlcpy(cmd.b, lv_textarea_get_text(s_qth_ta), sizeof(cmd.b));
    strlcpy(cmd.c, lv_textarea_get_text(s_loc_ta), sizeof(cmd.c));
    ESP_LOGI(TAG, "commissioning %s / %s", cmd.a, cmd.c);
    ui_post_cmd(&cmd);
}

static void build_station_step(void)
{
    const theme_t *t = theme();

    ui_label(s_body, &lv_font_ui_24, t->text, 20, 0, T(S_STATION_TITLE));

    /* The promise belongs here, where the callsign is actually typed. */
    ui_box(s_body, 560, 34, 20, 32, t->card, 9);
    ui_box(s_body, 4, 34, 20, 32, t->good, 2);
    ui_label(s_body, &lv_font_ui_12, t->good, 36, 42, T(S_PRIVACY_SHORT));

    /* QTH is a Q code and stays QTH in every language on the list. */
    ui_label(s_body, &lv_font_ui_12, t->muted, 20, 84, T(S_CALLSIGN));
    ui_label(s_body, &lv_font_ui_12, t->muted, 20, 140, "QTH");
    ui_label(s_body, &lv_font_ui_12, t->muted, 20, 196, T(S_LOCATOR));

    s_call_ta = make_textarea(s_body, 130, 74, 300, "DL1ABC", STATION_CALL_MAX - 1, false);
    s_qth_ta = make_textarea(s_body, 130, 130, 400, "", STATION_QTH_MAX - 1, false);
    s_loc_ta = make_textarea(s_body, 130, 186, 200, "JO30xp", STATION_LOC_MAX - 1, false);
    s_loc_hint = ui_label(s_body, &lv_font_ui_14, t->muted, 344, 200, "");

    for (lv_obj_t **ta = (lv_obj_t *[]){s_call_ta, s_qth_ta, s_loc_ta, NULL}; *ta; ta++) {
        lv_obj_add_event_cb(*ta, on_field_focus, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(*ta, on_field_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* Centred below the fields, matching the filter sheet and every other pair
     * of controls on the device; Cancel on the left of the two. */
    if (!s_assistant) {
        lv_obj_t *cancel = ui_button(s_body, 244, 246, 150, 46, on_cancel);
        lv_obj_set_style_bg_color(cancel, t->card_hi, 0);
        lv_obj_t *cl = ui_label(cancel, &lv_font_ui_18, t->text, 0, 0, T(S_CANCEL));
        lv_obj_center(cl);
    }
    s_finish_btn = ui_button(s_body, 406, 246, 150, 46, on_finish);
    lv_obj_set_style_bg_color(s_finish_btn, t->accent, 0);
    lv_obj_t *l = ui_label(s_finish_btn, &lv_font_ui_18, t->bg, 0, 0,
                           s_assistant ? T(S_FINISH) : T(S_OK));
    lv_obj_center(l);

    /* Prefill when re-running commissioning from Settings. */
    const station_t *st = station_get();
    if (st->configured) {
        lv_textarea_set_text(s_call_ta, st->call);
        lv_textarea_set_text(s_qth_ta, st->qth);
        lv_textarea_set_text(s_loc_ta, st->locator);
    }

    s_kb = lv_keyboard_create(s_body);
    lv_obj_set_size(s_kb, 800, 140);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(s_kb, s_call_ta);
    style_keyboard(s_kb);

    refresh_locator_hint();
}

/* ---- screen -------------------------------------------------------------- */

static void build_step(void)
{
    close_keyboard();
    s_pass_ta = NULL;
    s_list = NULL;
    s_status = NULL;
    lv_obj_clean(s_body);

    switch (s_step) {
    case STEP_FORMAT:  build_format_step(); break;
    case STEP_WIFI:    build_wifi_step(); break;
    default:           build_station_step(); break;
    }
    paint_rail();
}

static void on_screen_deleted(lv_event_t *e)
{
    (void)e;
    s_scr = NULL;
    s_body = NULL;
    s_rail_rule = NULL;
    s_kb = NULL;
    s_status = NULL;
}

lv_obj_t *ui_setup_create(void)
{
    const theme_t *t = theme();

    s_scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < STEP_COUNT; i++) {
        int x = 24 + i * 200;
        s_rail[i] = ui_box(s_scr, 26, 26, x, 18, t->card, LV_RADIUS_CIRCLE);
        lv_obj_t *num = ui_label_centre(s_scr, &lv_font_ui_14, t->bg, x, 22, 26, "");
        lv_label_set_text_fmt(num, "%d", i + 1);
        s_rail_lbl[i] = ui_label(s_scr, &lv_font_ui_16, t->muted, x + 36, 22, "");
    }
    s_rail_rule = ui_box(s_scr, 800, 1, 0, 58, t->card_hi, 0);

    s_body = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, 800, 420);
    lv_obj_set_pos(s_body, 0, 60);
    set_rail_visible(s_assistant);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    /* Re-entering from Settings skips straight to the station details: the
     * network is already joined and the language already chosen. */
    /* Which step to show is the caller's decision — ui_setup_goto_* sets it
     * before the screen is loaded. Falling back to "configured means show the
     * station step" is what sent Change network to the callsign form. */
    if (!station_get()->configured) {
        s_step = STEP_FORMAT;
    }
    build_step();
    return s_scr;
}

void ui_setup_set_aps(const wifi_mgr_ap_t *aps, size_t count)
{
    if (aps && aps != s_aps) {
        if (count > WIFI_MGR_MAX_APS) {
            count = WIFI_MGR_MAX_APS;
        }
        memcpy(s_aps, aps, count * sizeof(wifi_mgr_ap_t));
        s_ap_count = count;
    }
    if (!s_list) {
        return;
    }

    const theme_t *t = theme();
    lv_obj_clean(s_list);
    for (size_t i = 0; i < s_ap_count; i++) {
        lv_obj_t *btn = lv_list_add_button(s_list, NULL, s_aps[i].ssid);
        lv_obj_set_style_bg_color(btn, t->card, 0);
        lv_obj_set_style_bg_color(btn, t->card_hi, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, t->text, 0);
        lv_obj_set_style_text_font(btn, &lv_font_ui_20, 0);
        lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *meta = ui_label(btn, &lv_font_ui_14, t->muted, 0, 0, "");
        lv_label_set_text_fmt(meta, "%s%d dBm", s_aps[i].secure ? "locked  " : "",
                              s_aps[i].rssi);
        lv_obj_align(meta, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    if (s_status) {
        ui_setup_set_status(s_ap_count ? T(S_TAP_CONNECT) : T(S_NO_NETWORKS),
                            s_ap_count == 0);
    }
}

void ui_setup_set_status(const char *text, bool error)
{
    if (!s_status) {
        return;
    }
    lv_label_set_text(s_status, text ? text : "");
    lv_obj_set_style_text_color(s_status, error ? theme()->poor : theme()->muted, 0);
}

void ui_setup_wifi_connected(void)
{
    if (s_step != STEP_WIFI) {
        return;
    }
    /*
     * Where to go next depends on why we are here. During commissioning the
     * network is one step of three and the station follows. Reached from
     * Settings, joining a network was the whole errand — so it ends there,
     * rather than dropping the operator back on the list they just finished
     * with and leaving them to find their own way out.
     */
    if (s_assistant) {
        ESP_LOGI(TAG, "network joined, advancing to station step");
        s_step = STEP_STATION;
        build_step();
    } else {
        ESP_LOGI(TAG, "network joined, returning to settings");
        ui_show_settings();
    }
}
