/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Propagation map for one band.
 *
 * Two projections, toggled from the buttons under the legend:
 *
 *   Azimuthal equidistant, centred on the QTH. Screen direction IS the beam
 *   heading and radius is proportional to true distance, so the map answers
 *   "where do I point the antenna".
 *
 *   Equirectangular, which cannot show bearings but can show the grey line —
 *   the terminator is meaningless on an azimuthal plot and is exactly what you
 *   want when chasing low-band openings.
 *
 * Everything drawn is measured: the dots are grids that actually heard signals
 * from this region in the last three hours. An empty sector means nobody there
 * is listening, not that the band is shut.
 *
 * The canvas is deliberately taller than 2:1. Equirectangular is only correct
 * at exactly two by one, so it is letterboxed inside the canvas; azimuthal is a
 * circle and can use the full height, which buys it a noticeably larger disc
 * from space that would otherwise be background.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lib/i18n.h"
#include "lib/landmask.h"
#include "lib/prefs.h"
#include "lib/solar.h"
#include "lib/station.h"
#include "lib/units.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/theme.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.map";

#define MAP_X   12
/* Top edge is level with the MUF container beside it, bottom runs to the screen
 * edge — there is nothing below the map to leave room for. */
#define MAP_Y   128
#define MAP_W   572
#define MAP_H   348
#define EQ_H    (MAP_W / 2)            /* equirectangular band, exactly 2:1 */
#define EQ_TOP  ((MAP_H - EQ_H) / 2)

#define PANEL_X 596
#define PANEL_W 192
/*
 * The panel column starts below the map, not level with it. The ionosonde line
 * runs along the top right and its descenders reached the MUF card, so the two
 * read as one crowded block; sixteen pixels separates them.
 */
#define PANEL_Y (MAP_Y + 16)

#define ANTIPODE_KM 20015.0f

/* Spots are 3x3 rather than round: at this size a circle is drawn from the same
 * nine pixels but antialiased, which reads as a smudge rather than a point. */
#define SPOT_HALF 1
#define QTH_HALF  3

/*
 * Bearing spokes stop short at both ends: clear of the centre so they do not
 * bury the QTH marker, and clear of the rim so the degree numbers sit in a gap
 * rather than with a line drawn through them.
 */
#define SPOKE_INNER 14.0f
#define SPOKE_GAP   28.0f
#define BRG_LABELS  12

static lv_obj_t *s_scr;
static lv_obj_t *s_canvas;
static lv_draw_buf_t *s_draw_buf;
static lv_obj_t *s_title;
static lv_obj_t *s_iono;
static lv_obj_t *s_count;
static lv_obj_t *s_far;
static lv_obj_t *s_muf;
static lv_obj_t *s_proj_btn[2];
static lv_obj_t *s_brg_lbl[BRG_LABELS];

static int s_band_index;
static prop_spot_t s_spots[PROP_MAX_SPOTS];
static int s_spot_count;
static prop_muf_t s_muf_data;

/* ---- projection ---------------------------------------------------------- */

typedef struct {
    float cx, cy, r;      /* azimuthal */
    bool azimuthal;
} proj_t;

static proj_t s_proj;

static void paint_projection_buttons(void);

/*
 * All coordinates are CANVAS-LOCAL: 0..MAP_W by 0..MAP_H. The canvas is placed
 * on the screen at (MAP_X, MAP_Y), and adding that offset here as well pushed
 * the whole map down and right, clipping most of it.
 */
static void project(geo_point_t p, float *x, float *y)
{
    if (s_proj.azimuthal) {
        const station_t *st = station_get();
        float brg = geo_bearing(st->pos, p) * (float)M_PI / 180.0f;
        float km = geo_distance_km(st->pos, p);
        if (km > ANTIPODE_KM) {
            km = ANTIPODE_KM;
        }
        float rr = s_proj.r * km / ANTIPODE_KM;
        *x = s_proj.cx + rr * sinf(brg);
        *y = s_proj.cy - rr * cosf(brg);
    } else {
        *x = (p.lon + 180.0f) / 360.0f * MAP_W;
        *y = EQ_TOP + (90.0f - p.lat) / 180.0f * EQ_H;
    }
}

/* ---- canvas drawing ------------------------------------------------------ */

/*
 * Rendered per pixel from a 1-bit land mask rather than by filling polygons.
 * LVGL has no concave polygon fill, so the previous approach fan-triangulated
 * each coastline ring and visibly distorted anything not roughly convex.
 *
 * Day and night are computed per pixel too, so twilight is a smooth gradient
 * rather than stepped bands.
 */

#define SHADE_LEVELS 16

/* Full night is reached at astronomical twilight, 18 degrees below the horizon. */
#define NIGHT_FLOOR_SIN 0.309017f
#define NIGHT_MAX_MIX   150            /* 0-255, toward the night colour */

static uint16_t s_land_lut[SHADE_LEVELS];
static uint16_t s_sea_lut[SHADE_LEVELS];

/*
 * Angular distance depends only on the radius from centre, so sin and cos of it
 * are a one-dimensional table rather than two transcendentals per pixel. 512
 * steps across a half-turn is 0.35 degrees per entry, which is finer than the
 * 1024-wide land mask can resolve anyway.
 */
/*
 * Indexed by the SQUARED radius, not the radius.
 *
 * Everything the azimuthal path needs from the distance is a function of r
 * alone, and indexing on r squared removes the square root that would otherwise
 * be needed to compute r in the first place. The Xtensa LX7 FPU has no divide
 * or square-root instruction — the compiler emits a Newton-Raphson sequence or
 * a libgcc call for each — and those, not the inverse trigonometry, turned out
 * to be what kept the azimuthal repaint an order of magnitude slower than the
 * equirectangular one at the same pixel count.
 *
 * The stored k is sin(d)/r, which is what the projection actually multiplies
 * by; folding the division into the table removes two more per pixel. It has a
 * finite limit of pi/R at the centre, so nothing blows up there.
 *
 * Uniform steps in r squared are coarse near the middle: the first entry covers
 * about five pixels of radius. That disc is underneath the QTH marker, which is
 * drawn seven pixels across on top of it.
 */
#define DIST_LUT_N 1024
static float s_cos_d[DIST_LUT_N];   /* cos of the angular distance */
static float s_k[DIST_LUT_N];       /* sin(d) / r                  */

/*
 * Inverse trigonometry, tabled.
 *
 * asinf() and atan2f() are software routines in newlib costing a few hundred
 * cycles each. At two per pixel across 150k pixels they were the entire cost of
 * a repaint — measured at 542 ms, long enough to starve the LCD's bounce-buffer
 * refill and desync the scan.
 *
 * Neither result is wanted as an angle. Both exist only to index the land mask,
 * so the tables store the mask row and column directly and the intermediate
 * degrees never appear. The resolution needed is one mask cell, 0.35 degrees;
 * these are finer than that everywhere except within a few degrees of the
 * poles, where the mask is solid ice or ocean either way.
 */
#define ASIN_LUT_N 1024
#define ATAN_LUT_N 512

static uint16_t s_row_lut[ASIN_LUT_N];   /* sin(lat)  -> land mask row    */
static float s_atan_lut[ATAN_LUT_N];     /* t in 0..1 -> atan(t) radians  */
static float s_eq_cos[MAP_W];            /* equirect: cos(lon - sun_lon)  */
static uint16_t s_eq_col[MAP_W];         /* equirect: x -> mask column    */

static void build_shade_luts(void)
{
    const theme_t *t = theme();
    for (int i = 0; i < SHADE_LEVELS; i++) {
        lv_opa_t mix = (lv_opa_t)(NIGHT_MAX_MIX * i / (SHADE_LEVELS - 1));
        s_land_lut[i] = lv_color_to_u16(lv_color_mix(t->night_shade, t->land, mix));
        s_sea_lut[i] = lv_color_to_u16(lv_color_mix(t->night_shade, t->sea, mix));
    }
    /* Entry i corresponds to r = R * sqrt(i / (N-1)), so the index is r squared
     * scaled onto the table. Built per repaint because it depends on R. */
    /* Entry i corresponds to r = R * sqrt(i / (N-1)), so the index is r squared
     * scaled onto the table. Distance is linear in radius — that linearity is
     * what "equidistant" means — so d = pi * r / R. Rebuilt per repaint since
     * both depend on R. */
    for (int i = 0; i < DIST_LUT_N; i++) {
        const float root = sqrtf((float)i / (DIST_LUT_N - 1));
        const float r = s_proj.r * root;
        const float d = (float)M_PI * root;
        s_cos_d[i] = cosf(d);
        s_k[i] = (r > 0.0001f) ? sinf(d) / r : (float)M_PI / s_proj.r;
    }

    static bool inverse_built;
    if (inverse_built) {
        return;      /* independent of theme, sun and QTH: build once */
    }
    inverse_built = true;

    for (int i = 0; i < ASIN_LUT_N; i++) {
        float z = -1.0f + 2.0f * i / (ASIN_LUT_N - 1);
        float lat = asinf(z) * 180.0f / (float)M_PI;
        int row = (int)((90.0f - lat) / 180.0f * LANDMASK_H);
        if (row < 0) {
            row = 0;
        } else if (row >= LANDMASK_H) {
            row = LANDMASK_H - 1;
        }
        s_row_lut[i] = (uint16_t)row;
    }
    for (int i = 0; i < ATAN_LUT_N; i++) {
        s_atan_lut[i] = atanf((float)i / (ATAN_LUT_N - 1));
    }
}

/**
 * @brief Land mask column for a direction in the equatorial plane.
 *
 * Standard octant reduction: atan is tabled only over 0..45 degrees, and the
 * remaining seven octants are reached by swapping the arguments and reflecting.
 */
static inline int dir_to_col(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y);
    float a;
    if (ax < 1e-9f && ay < 1e-9f) {
        a = 0.0f;
    } else {
        bool steep = ay > ax;
        float t = steep ? ax / ay : ay / ax;
        a = s_atan_lut[(int)(t * (ATAN_LUT_N - 1))];
        if (steep) {
            a = (float)M_PI_2 - a;
        }
        if (x < 0.0f) {
            a = (float)M_PI - a;
        }
        if (y < 0.0f) {
            a = -a;
        }
    }
    int col = (int)((a + (float)M_PI) * (LANDMASK_W / (2.0f * (float)M_PI)));
    return col & (LANDMASK_W - 1);   /* mask width is a power of two */
}

/** @brief Sample the land mask by row and column, skipping the lat/lon round trip. */
static inline bool land_at_rc(int row, int col)
{
    return (LANDMASK[row * (LANDMASK_W / 8) + (col >> 3)] >> (7 - (col & 7))) & 1;
}

/** @brief Shade index: 0 is full day, SHADE_LEVELS-1 is full night. */
static inline int shade_index(float sin_elev)
{
    if (sin_elev >= 0.0f) {
        return 0;
    }
    float d = -sin_elev / NIGHT_FLOOR_SIN;
    if (d > 1.0f) {
        d = 1.0f;
    }
    return (int)(d * (SHADE_LEVELS - 1) + 0.5f);
}

/**
 * @brief Paint land, sea and terminator straight into the canvas buffer.
 *
 * Writing RGB565 directly is far faster than lv_canvas_set_px across 188k
 * pixels, and this runs on every band change and projection switch.
 *
 * The azimuthal path used to evaluate roughly nine transcendentals per pixel,
 * which made a full repaint long enough to starve the LCD's bounce-buffer
 * refill and desync the scan — the picture visibly trembled. Two identities
 * remove most of that work:
 *
 *   sin and cos of the bearing are just dx/r and -dy/r, since the bearing is
 *   defined as atan2(dx, -dy) — computing the angle only to take its sine
 *   again is wasted;
 *
 *   the position can be built as a vector in the QTH's local frame, so the
 *   solar elevation falls out of a dot product with the subsolar vector rather
 *   than needing another cosine.
 *
 * What remains per pixel is one square root, one arcsine and one arctangent,
 * both of the latter needed for the land mask lookup itself.
 */
static void paint_globe(time_t now)
{
    const theme_t *t = theme();
    lv_draw_buf_t *buf = s_draw_buf;
    if (!buf) {
        return;
    }
    build_shade_luts();

    const station_t *st = station_get();
    geo_point_t sub = solar_subsolar(now);
    const bool lit = (now > 1577836800);

    const float rad = (float)M_PI / 180.0f;

    const float sun_lat = sub.lat * rad;
    const float sun_lon = sub.lon * rad;
    const float sin_sun = sinf(sun_lat);
    const float cos_sun = cosf(sun_lat);

    /* Subsolar direction as a unit vector, so elevation is a dot product. */
    const float sx = cos_sun * cosf(sun_lon);
    const float sy = cos_sun * sinf(sun_lon);
    const float sz = sin_sun;

    /* Orthonormal frame at the QTH: up, north, east. */
    const float qlat = st->pos.lat * rad;
    const float qlon = st->pos.lon * rad;
    const float sin_q = sinf(qlat), cos_q = cosf(qlat);
    const float sin_l = sinf(qlon), cos_l = cosf(qlon);
    const float ux = cos_q * cos_l, uy = cos_q * sin_l, uz = sin_q;
    const float nx = -sin_q * cos_l, ny = -sin_q * sin_l, nz = cos_q;
    const float ex = -sin_l, ey = cos_l;   /* east has no z component */

    const uint16_t bg = lv_color_to_u16(t->bg);
    const uint32_t stride = buf->header.stride;
    const float r2 = s_proj.r * s_proj.r;
    const float q_scale = (DIST_LUT_N - 1) / r2;

    /* Equirectangular longitude is a function of the column alone, so the mask
     * column and the solar cosine are hoisted out of the inner loop entirely —
     * what is left per pixel is two multiplies and a table lookup. */
    if (!s_proj.azimuthal) {
        for (int x = 0; x < MAP_W; x++) {
            const float lon = (x + 0.5f) / MAP_W * 360.0f - 180.0f;
            s_eq_cos[x] = cosf(lon * rad - sun_lon);
            int col = (int)((lon + 180.0f) / 360.0f * LANDMASK_W);
            s_eq_col[x] = (uint16_t)(col & (LANDMASK_W - 1));
        }
    }

    const int64_t t0 = esp_timer_get_time();

    for (int y = 0; y < MAP_H; y++) {
        uint16_t *row = (uint16_t *)(buf->data + (uint32_t)y * stride);

        if (!s_proj.azimuthal) {
            /* Outside the 2:1 band there is no world to draw. */
            if (y < EQ_TOP || y >= EQ_TOP + EQ_H) {
                for (int x = 0; x < MAP_W; x++) {
                    row[x] = bg;
                }
                continue;
            }
            const float lat = 90.0f - (y - EQ_TOP + 0.5f) / EQ_H * 180.0f;
            const float sin_lat = sinf(lat * rad);
            const float cos_lat = cosf(lat * rad);
            int mrow = (int)((90.0f - lat) / 180.0f * LANDMASK_H);
            if (mrow < 0) {
                mrow = 0;
            } else if (mrow >= LANDMASK_H) {
                mrow = LANDMASK_H - 1;
            }
            /* Longitude depends only on the column, so both the mask column and
             * the solar term are per-row constants computed once below. */
            for (int x = 0; x < MAP_W; x++) {
                int shade = 0;
                if (lit) {
                    /* cos(zenith) is sin(elevation): no extra trig needed. */
                    shade = shade_index(sin_lat * sin_sun + cos_lat * cos_sun * s_eq_cos[x]);
                }
                row[x] = land_at_rc(mrow, s_eq_col[x]) ? s_land_lut[shade] : s_sea_lut[shade];
            }
            continue;
        }

        const float dy = (y + 0.5f) - s_proj.cy;
        const float dy2 = dy * dy;
        for (int x = 0; x < MAP_W; x++) {
            const float dx = (x + 0.5f) - s_proj.cx;
            const float q = dx * dx + dy2;
            if (q > r2) {
                row[x] = bg;
                continue;
            }

            /* One table lookup replaces a square root, two sines and two
             * divisions: k already carries the 1/r that turned dx and dy into
             * the sine and cosine of the bearing. */
            int di = (int)(q * q_scale);
            if (di >= DIST_LUT_N) {
                di = DIST_LUT_N - 1;
            }
            const float cd = s_cos_d[di];
            const float k = s_k[di];

            const float a = -dy * k;   /* sin(d) * cos(bearing) */
            const float b = dx * k;    /* sin(d) * sin(bearing) */
            const float px = cd * ux + a * nx + b * ex;
            const float py = cd * uy + a * ny + b * ey;
            const float pz = cd * uz + a * nz;   /* east contributes no z */

            int shade = 0;
            if (lit) {
                shade = shade_index(px * sx + py * sy + pz * sz);
            }

            /* Straight from the vector to mask coordinates: the latitude and
             * longitude in degrees were only ever an intermediate step. */
            float z = pz;
            if (z > 1.0f) {
                z = 1.0f;
            } else if (z < -1.0f) {
                z = -1.0f;
            }
            const int mrow = s_row_lut[(int)((z + 1.0f) * 0.5f * (ASIN_LUT_N - 1))];
            row[x] = land_at_rc(mrow, dir_to_col(py, px)) ? s_land_lut[shade]
                                                          : s_sea_lut[shade];
        }
    }

    /* Worth logging: this is the one operation long enough to starve the LCD
     * bounce-buffer refill, and it only runs on a band change or a projection
     * switch, so the line is rare rather than noise. */
    ESP_LOGI(TAG, "%s repaint %ux%u in %lld ms", s_proj.azimuthal ? "azimuthal" : "equirect",
             (unsigned)MAP_W, (unsigned)MAP_H, (esp_timer_get_time() - t0) / 1000);
}

static lv_color_t snr_colour(int8_t snr)
{
    const theme_t *t = theme();
    if (snr >= -10) {
        return t->good;
    }
    if (snr >= -18) {
        return t->fair;
    }
    if (snr >= -24) {
        return t->orange;
    }
    return t->poor;
}

/**
 * @brief Put the degree numbers just inside the rim, or hide them.
 *
 * Only the azimuthal view has bearings; on the grey line the same numbers would
 * be meaningless, so they go away entirely rather than sit there wrong.
 */
static void place_bearing_labels(void)
{
    for (int i = 0; i < BRG_LABELS; i++) {
        if (!s_brg_lbl[i]) {
            return;
        }
        if (!s_proj.azimuthal) {
            lv_obj_add_flag(s_brg_lbl[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int deg = i * 30;
        const float a = deg * (float)M_PI / 180.0f;
        const float rr = s_proj.r - SPOKE_GAP / 2.0f;
        /* Canvas-local plus the canvas origin: these are screen widgets. */
        const float x = MAP_X + s_proj.cx + rr * sinf(a);
        const float y = MAP_Y + s_proj.cy - rr * cosf(a);

        lv_label_set_text_fmt(s_brg_lbl[i], "%d", deg);
        lv_obj_set_pos(s_brg_lbl[i], (int)(x - 15.0f), (int)(y - 7.0f));
        lv_obj_clear_flag(s_brg_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void redraw(void)
{
    if (!s_canvas) {
        return;
    }
    const theme_t *t = theme();
    const station_t *st = station_get();
    time_t now = time(NULL);

    s_proj.azimuthal = (prefs_projection() == MAP_AZIMUTHAL);
    s_proj.cx = MAP_W / 2.0f;
    s_proj.cy = MAP_H / 2.0f;
    s_proj.r = MAP_H / 2.0f - 2.0f;

    paint_globe(now);

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    if (s_proj.azimuthal) {
        for (int km = 5000; km <= 15000; km += 5000) {
            lv_draw_arc_dsc_t ring;
            lv_draw_arc_dsc_init(&ring);
            ring.color = t->text;
            ring.opa = LV_OPA_20;
            ring.width = 1;
            ring.radius = (int32_t)(s_proj.r * km / ANTIPODE_KM);
            ring.center.x = (int32_t)s_proj.cx;
            ring.center.y = (int32_t)s_proj.cy;
            ring.start_angle = 0;
            ring.end_angle = 360;
            lv_draw_arc(&layer, &ring);
        }

        /*
         * Bearing spokes every 30 degrees.
         *
         * This is what makes the projection worth having. The centre is your
         * QTH, straight up is true north, and the angle clockwise to any point
         * is its great-circle initial bearing — the number you turn the rotator
         * to. Without graduations the map only says a path exists; with them it
         * says where to point.
         *
         * Cardinals are drawn brighter: they are what the eye uses to orient
         * before reading anything finer. The spokes start clear of the centre
         * so twelve converging lines do not bury the QTH marker.
         */
        lv_draw_line_dsc_t spoke;
        for (int deg = 0; deg < 360; deg += 30) {
            const bool cardinal = (deg % 90) == 0;
            const float a = deg * (float)M_PI / 180.0f;
            /* Screen y grows downward, so north is -cos and east is +sin. */
            lv_draw_line_dsc_init(&spoke);
            spoke.color = t->text;
            spoke.opa = cardinal ? LV_OPA_40 : LV_OPA_20;
            spoke.width = 1;
            spoke.p1.x = s_proj.cx + SPOKE_INNER * sinf(a);
            spoke.p1.y = s_proj.cy - SPOKE_INNER * cosf(a);
            spoke.p2.x = s_proj.cx + (s_proj.r - SPOKE_GAP) * sinf(a);
            spoke.p2.y = s_proj.cy - (s_proj.r - SPOKE_GAP) * cosf(a);
            lv_draw_line(&layer, &spoke);
        }
    }

    lv_draw_rect_dsc_t dot;
    for (int i = 0; i < s_spot_count; i++) {
        float x, y;
        project(s_spots[i].pos, &x, &y);
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = snr_colour(s_spots[i].snr);
        dot.bg_opa = LV_OPA_COVER;
        dot.radius = 0;
        lv_area_t a = {(int32_t)x - SPOT_HALF, (int32_t)y - SPOT_HALF,
                       (int32_t)x + SPOT_HALF, (int32_t)y + SPOT_HALF};
        lv_draw_rect(&layer, &dot, &a);
    }

    /* The QTH last, so a spot can never bury it, and larger with a contrasting
     * border so it reads as "here" rather than as a strong signal. */
    if (st->configured) {
        float x, y;
        project(st->pos, &x, &y);
        lv_draw_rect_dsc_init(&dot);
        dot.bg_color = t->text;
        dot.bg_opa = LV_OPA_COVER;
        dot.border_color = t->bg;
        dot.border_width = 2;
        dot.border_opa = LV_OPA_COVER;
        dot.radius = 0;
        lv_area_t a = {(int32_t)x - QTH_HALF, (int32_t)y - QTH_HALF,
                       (int32_t)x + QTH_HALF, (int32_t)y + QTH_HALF};
        lv_draw_rect(&layer, &dot, &a);
    }

    lv_canvas_finish_layer(s_canvas, &layer);
    place_bearing_labels();
    /* Keep the button pair agreeing with the projection actually drawn.
     * They used to be repainted only by their own click handler, so any
     * other route to a projection change left the wrong one highlighted
     * over the right map. */
    paint_projection_buttons();
}

/* ---- projection buttons -------------------------------------------------- */

static void paint_projection_buttons(void)
{
    const theme_t *t = theme();
    map_projection_t p = prefs_projection();
    for (int i = 0; i < 2; i++) {
        bool on = (i == (int)p);
        lv_obj_set_style_bg_color(s_proj_btn[i], on ? t->accent : t->card_hi, 0);
    }
}

static void on_pick_projection(lv_event_t *e)
{
    prefs_set_projection((map_projection_t)(intptr_t)lv_event_get_user_data(e));
    paint_projection_buttons();
    redraw();
}

/**
 * @brief A globe: circle, meridian and equator.
 *
 * Drawn rather than taken from a symbol font, because LVGL's built-in set has
 * nothing that reads as either projection, and the two icons have to be
 * distinguishable at a glance from across the room.
 */
static void draw_globe_icon(lv_obj_t *parent, int cx, int cy)
{
    const theme_t *t = theme();
    lv_obj_t *o = ui_box(parent, 30, 30, cx - 15, cy - 15, t->text, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, t->text, 0);
    lv_obj_set_style_border_width(o, 2, 0);

    /* Meridian: a narrow ellipse inside the disc. */
    lv_obj_t *m = ui_box(parent, 14, 30, cx - 7, cy - 15, t->text, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(m, t->text, 0);
    lv_obj_set_style_border_width(m, 2, 0);

    ui_box(parent, 30, 2, cx - 15, cy - 1, t->text, 0);   /* equator */
}

/**
 * @brief A map with one half in shadow — the grey line itself, not a generic
 *        map glyph, so the icon says which projection it selects.
 */
static void draw_greyline_icon(lv_obj_t *parent, int cx, int cy)
{
    const theme_t *t = theme();
    lv_obj_t *o = ui_box(parent, 34, 24, cx - 17, cy - 12, t->text, 3);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, t->text, 0);
    lv_obj_set_style_border_width(o, 2, 0);

    lv_obj_t *night = ui_box(parent, 15, 20, cx + 0, cy - 10, t->text, 0);
    lv_obj_set_style_bg_opa(night, LV_OPA_40, 0);
}

/* ---- screen -------------------------------------------------------------- */

/*
 * The canvas buffer is not owned by the screen, so deleting the screen leaks it
 * — 389 KB of PSRAM every time a language or unit change rebuilds the pages.
 * Four changes and the next allocation fails.
 */
static void on_screen_deleted(lv_event_t *e)
{
    (void)e;
    if (s_draw_buf) {
        lv_draw_buf_destroy(s_draw_buf);
        s_draw_buf = NULL;
    }
    s_scr = NULL;
    s_canvas = NULL;
}

lv_obj_t *ui_map_create(void)
{
    const theme_t *t = theme();

    s_scr = lv_obj_create(NULL);
    lv_obj_add_event_cb(s_scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_style_bg_color(s_scr, t->bg, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    ui_header_build(s_scr, UI_PAGE_MAP);

    s_title = ui_label(s_scr, &lv_font_ui_24, t->text, 20, 96, "--");
    /* Provenance and the ionosonde stack on the right, together occupying about
     * the same height as the title beside them. */
    ui_label_right(s_scr, &lv_font_ui_12, t->muted, 788, 96, T(S_WSPR_MEASURED));
    s_iono = ui_label_right(s_scr, &lv_font_ui_12, t->muted, 788, 114, "");

    /*
     * RGB565 canvas in PSRAM: 572x348x2 = 389 KB, far too large for internal
     * RAM but trivial for the 8 MB PSRAM. Allocated once and reused for every
     * band and projection.
     */
    s_draw_buf = lv_draw_buf_create(MAP_W, MAP_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!s_draw_buf) {
        ESP_LOGE(TAG, "canvas allocation failed (%d bytes)", MAP_W * MAP_H * 2);
        ui_label(s_scr, &lv_font_ui_16, t->poor, MAP_X, MAP_Y, "map buffer unavailable");
    } else {
        s_canvas = lv_canvas_create(s_scr);
        lv_canvas_set_draw_buf(s_canvas, s_draw_buf);
        lv_obj_set_pos(s_canvas, MAP_X, MAP_Y);
        lv_canvas_fill_bg(s_canvas, t->bg, LV_OPA_COVER);
    }

    /*
     * Bearing graduations, as labels over the canvas rather than pixels in it.
     * They are static text that never changes with the data, so drawing them
     * into the buffer would mean repainting them on every band change.
     */
    for (int i = 0; i < BRG_LABELS; i++) {
        s_brg_lbl[i] = ui_label_centre(s_scr, &lv_font_ui_12, t->muted, 0, 0, 30, "");
    }

    /* ---- MUF, its own container directly under the header ---- */
    ui_card(s_scr, PANEL_X, PANEL_Y, PANEL_W, 56);
    s_muf = ui_label(s_scr, &lv_font_ui_20, t->accent, PANEL_X + 14, PANEL_Y + 16, "MUF --");

    /* ---- reach ---- */
    ui_card(s_scr, PANEL_X, PANEL_Y + 64, PANEL_W, 56);
    s_count = ui_label(s_scr, &lv_font_ui_20, t->text, PANEL_X + 14, PANEL_Y + 72, "--");
    s_far = ui_label(s_scr, &lv_font_ui_12, t->good, PANEL_X + 14, PANEL_Y + 98, "");

    /* ---- signal-to-noise legend ---- */
    ui_label(s_scr, &lv_font_ui_12, t->muted, PANEL_X, PANEL_Y + 138, T(S_SNR));
    static const char *leg[4] = {">= -10 dB", "-10 to -18", "-18 to -24", "below -24"};
    for (int i = 0; i < 4; i++) {
        const lv_color_t c[4] = {t->good, t->fair, t->orange, t->poor};
        int y = PANEL_Y + 158 + i * 20;
        /* Square swatches to match the map: a legend showing circles for square
         * marks makes the reader hunt for a shape that is not there. */
        ui_box(s_scr, 10, 10, PANEL_X + 2, y + 2, c[i], 0);
        ui_label(s_scr, &lv_font_ui_12, t->text, PANEL_X + 20, y, leg[i]);
    }

    /* ---- projection choice, below the legend ---- */
    for (int i = 0; i < 2; i++) {
        int x = PANEL_X + i * 100;
        int y = PANEL_Y + 250;
        /* NULL here: ui_button would register a second handler with no user
         * data, and the button would fire twice per press. */
        s_proj_btn[i] = ui_button(s_scr, x, y, 88, 56, NULL);
        lv_obj_add_event_cb(s_proj_btn[i], on_pick_projection, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        if (i == 0) {
            draw_globe_icon(s_proj_btn[i], 44, 28);
        } else {
            draw_greyline_icon(s_proj_btn[i], 44, 28);
        }
    }
    paint_projection_buttons();

    ui_map_set_spots(NULL, s_spot_count, &s_muf_data);
    return s_scr;
}

void ui_map_set_band(int band_index)
{
    const prop_band_t *b = prop_band(band_index);
    if (!b || !s_scr) {
        return;
    }
    bool changed = (band_index != s_band_index);
    s_band_index = band_index;
    lv_label_set_text_fmt(s_title, T(S_PROPAGATION), b->name);

    /*
     * Drop the old band's data at the same moment as the old band's name.
     *
     * Fetching the new band takes a few seconds, and this used to relabel the
     * title and nothing else — so the map kept painting the previous band's
     * receptions and its MUF under the new heading. That is not stale data, it
     * is wrong data presented as current: 20 m openings read as 40 m by anyone
     * who looked during those seconds, which is exactly the mistake a
     * propagation display exists to prevent.
     *
     * An empty map with "measuring" under it says the true thing, and the wait
     * is short.
     */
    if (changed) {
        s_spot_count = 0;
        s_muf_data = (prop_muf_t){0};
        lv_label_set_text(s_count, T(S_MEASURING));
        lv_label_set_text(s_far, "");
        lv_label_set_text(s_muf, "");
        lv_label_set_text(s_iono, "");
        redraw();
    }
}

void ui_map_set_spots(const prop_spot_t *spots, int count, const prop_muf_t *muf)
{
    if (!s_scr) {
        return;
    }
    if (count > PROP_MAX_SPOTS) {
        count = PROP_MAX_SPOTS;
    }
    if (spots && count > 0) {
        memcpy(s_spots, spots, count * sizeof(prop_spot_t));
        s_spot_count = count;
    } else if (spots) {
        s_spot_count = 0;
    }

    const station_t *st = station_get();
    float far_km = 0;
    for (int i = 0; i < s_spot_count; i++) {
        float km = st->configured ? geo_distance_km(st->pos, s_spots[i].pos) : 0;
        if (km > far_km) {
            far_km = km;
        }
    }

    lv_label_set_text_fmt(s_count, T(S_GRIDS), s_spot_count);
    if (far_km > 0) {
        lv_label_set_text_fmt(s_far, "%.0f %s %s", (double)units_distance(far_km),
                              units_distance_suffix(), T(S_FURTHEST));
    } else {
        lv_label_set_text(s_far, "");
    }

    if (muf) {
        s_muf_data = *muf;
    }
    if (s_muf_data.valid) {
        lv_label_set_text_fmt(s_muf, "MUF %.1f MHz", (double)s_muf_data.muf);
        /* Naming the station matters: a MUF is a measurement from one place,
         * not a property of the sky everywhere. */
        lv_label_set_text_fmt(s_iono, "%s %s  ·  %.0f %s", T(S_NEAREST_IONOSONDE),
                              s_muf_data.station,
                              (double)units_distance(s_muf_data.distance_km),
                              units_distance_suffix());
    } else {
        lv_label_set_text(s_muf, T(S_MUF_NA));
        lv_label_set_text(s_iono, "");
    }

    redraw();
}
