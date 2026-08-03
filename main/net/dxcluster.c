/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
#include "net/dxcluster.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "lib/dxcc.h"
#include "lib/i18n.h"
#include "lib/station.h"
#include "net/http_get.h"

static const char *TAG = "dx";

#define URL_HAMQTH  "https://www.hamqth.com/dxc_csv.php?limit=96"
#define URL_DXWATCH "https://dxwatch.com/dxsd1/s.php?s=0&r=60&cdx="

/* Field positions in the caret-delimited record. */
enum {
    F_SPOTTER = 0,
    F_FREQ,
    F_DX,
    F_COMMENT,
    F_TIME,
    F_UNUSED,
    F_MODE,
    F_CONTINENT,
    F_BAND,
    F_COUNTRY,
    F_DXCC,
    F_MAX,
};

/**
 * @brief Copy text from the feed into a fixed buffer, safely.
 *
 * Everything here is written by strangers: the comment field is whatever the
 * spotting operator typed, and it reaches the display unmediated. Three things
 * have to be handled rather than assumed away.
 *
 * Control characters are dropped, because a stray newline or escape byte would
 * corrupt the row layout. Backslash escapes are unwrapped, since the feed emits
 * "73\'s" for an apostrophe. And truncation happens only on a UTF-8 character
 * boundary — cutting a multi-byte sequence in half leaves a partial code point
 * that renders as a replacement glyph or, worse, swallows the next character.
 */
static void copy_clean(char *dst, size_t cap, const char *src, size_t len)
{
    size_t o = 0;
    if (cap == 0) {
        return;
    }
    for (size_t i = 0; i < len && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\\' && i + 1 < len) {
            continue;             /* escape marker; emit whatever follows */
        }
        if (c < 0x20 || c == 0x7F) {
            continue;             /* control characters never reach a label */
        }

        /* DXWatch escapes its comments for HTML, so a VHF spot reading
         * "IO84OQ<ES>IN50QR" arrives as "IO84OQ&lt;ES&gt;IN50QR". Undo the five
         * entities that appear in practice; anything else stays literal, which
         * is the safe direction to be wrong in. */
        if (c == '&') {
            static const struct { const char *ent; char ch; } k_ents[] = {
                {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'},
                {"&quot;", '"'}, {"&#39;", '\''},
            };
            bool matched = false;
            for (size_t k = 0; k < sizeof(k_ents) / sizeof(k_ents[0]); k++) {
                size_t el = strlen(k_ents[k].ent);
                if (i + el <= len && strncmp(src + i, k_ents[k].ent, el) == 0) {
                    dst[o++] = k_ents[k].ch;
                    i += el - 1;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }

        /* Determine the length of this UTF-8 sequence from its lead byte and
         * copy it whole, or not at all. */
        size_t seq = 1;
        if (c >= 0xF0) {
            seq = 4;
        } else if (c >= 0xE0) {
            seq = 3;
        } else if (c >= 0xC0) {
            seq = 2;
        } else if (c >= 0x80) {
            continue;             /* stray continuation byte: not decodable */
        }
        if (i + seq > len || o + seq + 1 > cap) {
            break;
        }
        for (size_t k = 0; k < seq; k++) {
            dst[o++] = src[i + k];
        }
        i += seq - 1;
    }

    /* Trim trailing spaces so short comments do not look padded. */
    while (o > 0 && dst[o - 1] == ' ') {
        o--;
    }
    dst[o] = '\0';
}

/** @brief Case-insensitive substring search, since strcasestr is not portable. */
static bool contains_ci(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) {
        return false;
    }
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && tolower((unsigned char)h[i]) == needle[i]) {
            i++;
        }
        if (i == nl) {
            return true;
        }
    }
    return false;
}

bool dx_is_rare(int dxcc)
{
    return dxcc_is_most_wanted(dxcc);
}

const char *dx_category_name(dx_category_t c)
{
    switch (c) {
    case DX_RARE:    return T(S_DX_RARE);
    case DX_DISTANT: return T(S_DX_DISTANT);
    default:         return T(S_DX_ALL);
    }
}

/*
 * Mode, inferred from the comment and the frequency.
 *
 * The feed has a mode column but it is empty on most records — of the sixteen
 * spots in a typical fetch only a couple carry anything. Spotters do however
 * name the mode in the comment almost every time ("FT8 -13dB", "TNX CW"), and
 * where they do not, the band plan segment is a reliable fallback.
 */
static void infer_mode(dx_spot_t *s, const char *published)
{
    static const char *const k_modes[] = {"FT8", "FT4", "CW", "SSB", "RTTY", "PSK",
                                          "JT65", "MSK", "SSTV", "AM", "FM"};
    /*
     * The published column is trusted only when it names a mode we recognise.
     * HamQTH puts a bare "E" in it on many records; whatever that flag means it
     * is not a mode, and passing it through put a column of "E" on screen where
     * CW or SSB belonged.
     */
    if (published && published[0]) {
        for (size_t i = 0; i < sizeof(k_modes) / sizeof(k_modes[0]); i++) {
            if (strcmp(published, k_modes[i]) == 0) {
                strlcpy(s->mode, k_modes[i], sizeof(s->mode));
                return;
            }
        }
    }
    for (size_t i = 0; i < sizeof(k_modes) / sizeof(k_modes[0]); i++) {
        if (contains_ci(s->comment, k_modes[i])) {
            strlcpy(s->mode, k_modes[i], sizeof(s->mode));
            return;
        }
    }
    /*
     * Fall back to the band plan.
     *
     * Only the exact FT8 watering holes used to be recognised, so most spots
     * showed nothing at all. The IARU segment boundaries are a regulated
     * convention that essentially everyone follows, which makes them a sound
     * basis for a guess: below the CW/data boundary it is CW, between that and
     * the phone boundary it is a digital mode, above it is voice.
     *
     * This is inference, not fact — a rag-chew a few kHz outside its segment
     * will be labelled wrongly. It is still far better than a blank column, and
     * the exact FT8 frequencies below are checked first because those are as
     * near certain as this gets.
     */
    const int khz = (int)s->freq_khz;
    if (khz == 14074 || khz == 7074 || khz == 10136 || khz == 18100 || khz == 21074 ||
        khz == 24915 || khz == 28074 || khz == 3573 || khz == 50313 || khz == 144174) {
        strlcpy(s->mode, "FT8", sizeof(s->mode));
        return;
    }

    /* low edge, end of CW, end of digital — all kHz, IARU Region 1. */
    static const struct { int lo, cw, digi, hi; } k_plan[] = {
        {  1810,   1838,   1843,   2000},   /* 160m */
        {  3500,   3570,   3600,   3800},   /* 80m  */
        {  7000,   7040,   7050,   7200},   /* 40m  */
        { 10100,  10130,  10150,  10150},   /* 30m, no phone */
        { 14000,  14070,  14099,  14350},   /* 20m  */
        { 18068,  18095,  18109,  18168},   /* 17m  */
        { 21000,  21070,  21110,  21450},   /* 15m  */
        { 24890,  24915,  24929,  24990},   /* 12m  */
        { 28000,  28070,  28190,  29700},   /* 10m  */
        { 50000,  50100,  50500,  52000},   /* 6m   */
        {144000, 144150, 144400, 148000},   /* 2m   */
    };
    for (size_t i = 0; i < sizeof(k_plan) / sizeof(k_plan[0]); i++) {
        if (khz < k_plan[i].lo || khz > k_plan[i].hi) {
            continue;
        }
        const char *m = (khz <= k_plan[i].cw)   ? "CW"
                        : (khz <= k_plan[i].digi) ? "digi"
                                                  : "SSB";
        strlcpy(s->mode, m, sizeof(s->mode));
        return;
    }
    s->mode[0] = '\0';
}

/* ---- filtering ----------------------------------------------------------- */

static const char *const k_band_names[DXB_COUNT] = {
    "160m", "80m", "40m", "30m", "20m", "17m",
    "15m", "12m", "10m", "6m", "2m", "other",
};

static const char *const k_mode_names[DXM_COUNT] = {
    "CW", "SSB", "FT8", "FT4", "digi", "other",
};

const char *dx_band_name(dx_band_t b)
{
    return (b < DXB_COUNT) ? k_band_names[b] : "";
}

const char *dx_mode_name(dx_mode_t m)
{
    return (m < DXM_COUNT) ? k_mode_names[m] : "";
}

/**
 * @brief Bucket a spot by frequency rather than by the feed's band string.
 *
 * The published band ("20M", "23CM") is free-form enough that matching on it
 * means keeping a table of every spelling every source uses. The frequency is a
 * number, and the band edges are fixed by regulation.
 */
static dx_band_t band_of(float khz)
{
    if (khz >= 1800 && khz <= 2000)     return DXB_160;
    if (khz >= 3500 && khz <= 4000)     return DXB_80;
    if (khz >= 7000 && khz <= 7300)     return DXB_40;
    if (khz >= 10100 && khz <= 10150)   return DXB_30;
    if (khz >= 14000 && khz <= 14350)   return DXB_20;
    if (khz >= 18068 && khz <= 18168)   return DXB_17;
    if (khz >= 21000 && khz <= 21450)   return DXB_15;
    if (khz >= 24890 && khz <= 24990)   return DXB_12;
    if (khz >= 28000 && khz <= 29700)   return DXB_10;
    if (khz >= 50000 && khz <= 54000)   return DXB_6;
    if (khz >= 144000 && khz <= 148000) return DXB_2;
    return DXB_OTHER;
}

static dx_mode_t mode_of(const char *mode)
{
    if (!mode || !mode[0]) {
        return DXM_OTHER;
    }
    if (strcmp(mode, "CW") == 0)  return DXM_CW;
    if (strcmp(mode, "FT8") == 0) return DXM_FT8;
    if (strcmp(mode, "FT4") == 0) return DXM_FT4;
    if (strcmp(mode, "SSB") == 0 || strcmp(mode, "USB") == 0 ||
        strcmp(mode, "LSB") == 0 || strcmp(mode, "AM") == 0 ||
        strcmp(mode, "FM") == 0) {
        return DXM_SSB;
    }
    return DXM_DIGI;   /* RTTY, PSK, JT65, MSK, SSTV */
}

const char *dx_source_name(dx_source_t s)
{
    return (s == DXS_DXWATCH) ? "DXWatch" : "HamQTH";
}

bool dx_spot_matches(const dx_spot_t *s, const dx_filter_t *f)
{
    if (!s) {
        return false;
    }
    if (!f) {
        return true;
    }
    /* An empty mask means "no restriction" rather than "nothing": clearing
     * every checkbox should show everything, not blank the table. */
    if (f->bands && !(f->bands & (1u << band_of(s->freq_khz)))) {
        return false;
    }
    if (f->modes && !(f->modes & (1u << mode_of(s->mode)))) {
        return false;
    }
    if (f->cats && !(f->cats & (1u << (int)s->category))) {
        return false;
    }
    return true;
}

/** @brief Split one record on '^'. Returns the number of fields located. */
static int split_fields(char *line, const char **out, int max)
{
    int n = 0;
    const char *p = line;
    out[n++] = p;
    for (char *c = line; *c && n < max; c++) {
        if (*c == '^') {
            *c = '\0';
            out[n++] = c + 1;
        }
    }
    return n;
}

static bool parse_line(char *line, dx_spot_t *out)
{
    const char *f[F_MAX];
    int n = split_fields(line, f, F_MAX);
    /* Anything short of the country column is a truncated or unexpected
     * record; skipping it costs one spot and avoids showing nonsense. */
    if (n <= F_COUNTRY) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    copy_clean(out->dx, sizeof(out->dx), f[F_DX], strlen(f[F_DX]));
    copy_clean(out->spotter, sizeof(out->spotter), f[F_SPOTTER], strlen(f[F_SPOTTER]));
    copy_clean(out->country, sizeof(out->country), f[F_COUNTRY], strlen(f[F_COUNTRY]));
    copy_clean(out->band, sizeof(out->band), f[F_BAND], strlen(f[F_BAND]));
    copy_clean(out->comment, sizeof(out->comment), f[F_COMMENT], strlen(f[F_COMMENT]));
    copy_clean(out->continent, sizeof(out->continent), f[F_CONTINENT],
               strlen(f[F_CONTINENT]));

    if (out->dx[0] == '\0') {
        return false;
    }

    out->freq_khz = strtof(f[F_FREQ], NULL);
    out->dxcc = (n > F_DXCC) ? atoi(f[F_DXCC]) : 0;
    infer_mode(out, f[F_MODE]);

    /* Time arrives as "0859 2026-08-02"; only the clock part is shown. */
    out->hour = -1;
    if (strlen(f[F_TIME]) >= 4) {
        char hh[3] = {f[F_TIME][0], f[F_TIME][1], 0};
        char mm[3] = {f[F_TIME][2], f[F_TIME][3], 0};
        int h = atoi(hh);
        int m = atoi(mm);
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
            out->hour = h;
            out->minute = m;
        }
    }
    return true;
}

/** @brief Finish a spot: classify it, note its origin, keep it if usable. */
static bool accept(dx_feed_t *f, dx_spot_t *s, dx_source_t src)
{
    if (s->dx[0] == '\0' || f->count >= DX_MAX_SPOTS) {
        return false;
    }
    s->source = (uint8_t)src;

    /*
     * Everything derived hangs off the DXCC entity code.
     *
     * HamQTH publishes it, along with a country name and continent. DXWatch
     * publishes nothing that identifies the entity at all — its numeric fields
     * are ages in seconds, not country codes — so for those the entity comes
     * from the callsign, exactly as every other piece of ham software does it.
     */
    if (s->dxcc == 0) {
        s->dxcc = (uint16_t)dxcc_from_callsign(s->dx);
    }
    if (s->dxcc > 0) {
        /* Our table wins over whatever the feed called it, even when the feed
         * supplied a name. One entity then reads the same whichever service
         * reported it, and the names are the shortened ones chosen to fit the
         * column — "UAE", not "United Arab Emirates" clipped mid-word. */
        const char *n = dxcc_name(s->dxcc);
        if (n) {
            strlcpy(s->country, n, sizeof(s->country));
        }
        if (s->continent[0] == '\0') {
            const char *c = dxcc_continent(s->dxcc);
            if (c) {
                strlcpy(s->continent, c, sizeof(s->continent));
            }
        }
    }

    if (dx_is_rare(s->dxcc)) {
        s->category = DX_RARE;
    } else if (s->continent[0] && strcmp(s->continent, station_continent()) != 0) {
        s->category = DX_DISTANT;
    } else {
        s->category = DX_COMMON;
    }

    /* The same spot reaches both services within seconds of each other. Match
     * on callsign and frequency rather than on time, which differs by however
     * long the two took to ingest it. */
    for (int i = 0; i < f->count; i++) {
        if (strcmp(f->spots[i].dx, s->dx) == 0 &&
            (int)(f->spots[i].freq_khz + 0.5f) == (int)(s->freq_khz + 0.5f)) {
            return false;
        }
    }
    f->spots[f->count++] = *s;
    return true;
}

static int fetch_hamqth(dx_feed_t *f)
{
    char *body = NULL;
    if (http_get_body(URL_HAMQTH, &body, NULL) != ESP_OK || !body) {
        ESP_LOGW(TAG, "HamQTH fetch failed");
        return 0;
    }
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(body, "\r\n", &save);
         line && f->count < DX_MAX_SPOTS;
         line = strtok_r(NULL, "\r\n", &save)) {
        dx_spot_t s;
        if (parse_line(line, &s) && accept(f, &s, DXS_HAMQTH)) {
            n++;
        }
    }
    free(body);
    return n;
}

/*
 * DXWatch returns an object keyed by spot id, each value an array:
 *
 *   {"s":{"61168069":["EA5KK",14074,"EA5JMO","FT8 DIPLOMA","1355z 02 Aug",193,22,1]}}
 *    spotter, kHz, dx, comment, "HHMMz DD Mon", dxcc_dx, dxcc_spotter, flag
 *
 * Note what is NOT here: an entity. Fields 5 and 6 look like country codes and
 * are not — they are the age of the spot and of the spotter's in seconds, which
 * is only obvious once you notice they descend monotonically down a newest-first
 * list. Reading one as a DXCC code put EA1BVG in Liberia.
 *
 * The entity therefore comes from the callsign, via the same prefix table every
 * other piece of ham software uses. accept() does that for any spot that
 * arrives without one.
 */
static int fetch_dxwatch(dx_feed_t *f)
{
    char *body = NULL;
    if (http_get_body(URL_DXWATCH, &body, NULL) != ESP_OK || !body) {
        ESP_LOGW(TAG, "DXWatch fetch failed");
        return 0;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGW(TAG, "DXWatch JSON unparseable");
        return 0;
    }

    int n = 0;
    const cJSON *set = cJSON_GetObjectItemCaseSensitive(root, "s");
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, set)
    {
        if (!cJSON_IsArray(entry) || f->count >= DX_MAX_SPOTS) {
            continue;
        }
        const cJSON *spotter = cJSON_GetArrayItem(entry, 0);
        const cJSON *freq = cJSON_GetArrayItem(entry, 1);
        const cJSON *dx = cJSON_GetArrayItem(entry, 2);
        const cJSON *cmt = cJSON_GetArrayItem(entry, 3);
        const cJSON *when = cJSON_GetArrayItem(entry, 4);
        if (!cJSON_IsString(dx) || !cJSON_IsNumber(freq)) {
            continue;
        }

        dx_spot_t s = {0};
        copy_clean(s.dx, sizeof(s.dx), dx->valuestring, strlen(dx->valuestring));
        if (cJSON_IsString(spotter)) {
            copy_clean(s.spotter, sizeof(s.spotter), spotter->valuestring,
                       strlen(spotter->valuestring));
        }
        if (cJSON_IsString(cmt)) {
            copy_clean(s.comment, sizeof(s.comment), cmt->valuestring,
                       strlen(cmt->valuestring));
        }
        s.freq_khz = (float)freq->valuedouble;
        strlcpy(s.band, dx_band_name(band_of(s.freq_khz)), sizeof(s.band));

        s.hour = -1;
        if (cJSON_IsString(when) && strlen(when->valuestring) >= 4) {
            int hh = 0, mm = 0;
            if (sscanf(when->valuestring, "%2d%2d", &hh, &mm) == 2 &&
                hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
                s.hour = hh;
                s.minute = mm;
            }
        }
        infer_mode(&s, NULL);
        if (accept(f, &s, DXS_DXWATCH)) {
            n++;
        }
    }
    cJSON_Delete(root);
    return n;
}

esp_err_t dx_fetch(dx_feed_t *out, uint8_t sources)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sources == 0) {
        sources = 1u << DXS_HAMQTH;   /* never leave the page with no source */
    }

    /*
     * Static, not a local. dx_feed_t is 36 spots of 124 bytes — about 4.5 KB —
     * and this runs on the 8 KB app task alongside a TLS handshake that wants
     * several KB of its own. As a local it overflowed the stack the moment the
     * buffer grew from 16 spots to 36; the task died rather than the request.
     *
     * Safe as static because only the app task ever calls this, one fetch at a
     * time. The console command has its own copy for the same reason.
     */
    static EXT_RAM_BSS_ATTR dx_feed_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    int from_hamqth = 0, from_dxwatch = 0;

    if (sources & (1u << DXS_HAMQTH)) {
        from_hamqth = fetch_hamqth(&fresh);
    }
    if (sources & (1u << DXS_DXWATCH)) {
        from_dxwatch = fetch_dxwatch(&fresh);
    }

    if (fresh.count == 0) {
        ESP_LOGW(TAG, "no usable records from any enabled source");
        return ESP_FAIL;
    }

    fresh.updated = time(NULL);
    fresh.valid = true;
    *out = fresh;

    ESP_LOGI(TAG, "%d spots (HamQTH %d, DXWatch %d), newest %s on %.1f kHz (%s)",
             fresh.count, from_hamqth, from_dxwatch, fresh.spots[0].dx,
             (double)fresh.spots[0].freq_khz, fresh.spots[0].band);
    return ESP_OK;
}
