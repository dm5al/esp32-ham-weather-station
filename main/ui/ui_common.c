#include <string.h>

#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int x, int y,
                   const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_label_right(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int right,
                         int y, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, colour, 0, y, text);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(l, right);
    lv_obj_set_pos(l, 0, y);
    return l;
}

lv_obj_t *ui_label_centre(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour, int x,
                          int y, int w, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, colour, x, y, text);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t *ui_box(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t colour, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, colour, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    return ui_box(parent, w, h, x, y, theme()->card, 12);
}

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb)
{
    const theme_t *t = theme();
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, t->card, 0);
    lv_obj_set_style_bg_color(b, t->card_hi, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    }
    return b;
}

void ui_hline(lv_obj_t *parent, int x, int y, int w, lv_color_t colour)
{
    ui_box(parent, w, 1, x, y, colour, 0);
}

void ui_vline(lv_obj_t *parent, int x, int y, int h, lv_color_t colour)
{
    ui_box(parent, 1, h, x, y, colour, 0);
}

const char *ui_weekday_short(int tm_wday)
{
    static const char *k[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return (tm_wday >= 0 && tm_wday < 7) ? k[tm_wday] : "";
}

lv_obj_t *ui_bottom_bar(lv_obj_t *scr)
{
    /* Just the divider now. Back went away with the header's home button: two
     * controls for one destination, one of which moved between pages. */
    ui_hline(scr, 0, 424, 800, theme()->card_hi);
    return scr;
}
