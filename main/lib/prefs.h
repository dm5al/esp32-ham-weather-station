/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Small persisted display preferences.
 *
 * Night mode lives in ui/theme.c because the palette owns that decision. This
 * holds the rest: the map projection, the interface language, and the unit
 * system. All three are genuine user choices with no defensible default —
 * azimuthal answers "where do I point the antenna" while equirectangular shows
 * the grey line, and an operator in Ohio and one in Bavaria disagree about what
 * a temperature reading should say.
 *
 * Storage is one NVS namespace shared with theme.c; these are single bytes and
 * a separate namespace per setting would cost a flash page each.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAP_AZIMUTHAL = 0,   /* equidistant, centred on the QTH: true bearings */
    MAP_EQUIRECT,        /* familiar world layout, shows the terminator */
} map_projection_t;

/*
 * Unit systems, named after what an operator would recognise rather than after
 * the standards bodies. The UK really is its own case: metric temperature with
 * imperial distance is not an inconsistency to be corrected, it is what road
 * signs and weather forecasts there actually do.
 */
typedef enum {
    UNITS_EU = 0,   /* 24 h,      C, km,    dd.mm.yyyy, m/s */
    UNITS_UK,       /* 24 h,      C, miles, dd/mm/yyyy, mph */
    UNITS_US,       /* 12 h am/pm, F, miles, mm/dd/yyyy, mph */
    UNITS_COUNT,
} unit_system_t;

typedef enum {
    LANG_EN = 0,
    LANG_DE,
    LANG_RU,
    LANG_FR,
    LANG_IT,
    LANG_ES,
    LANG_COUNT,
} lang_t;

void prefs_init(void);

map_projection_t prefs_projection(void);
esp_err_t prefs_set_projection(map_projection_t p);

unit_system_t prefs_units(void);
esp_err_t prefs_set_units(unit_system_t u);

lang_t prefs_lang(void);
esp_err_t prefs_set_lang(lang_t l);

/**
 * @brief The DX filter, stored as an opaque blob.
 *
 * prefs has no business knowing what a band mask is; it stores the bytes the
 * DX page hands it. Returns false when nothing has been saved yet, so the
 * caller keeps its own defaults rather than being handed a zeroed struct.
 */
bool prefs_get_dx_filter(void *out, size_t len);
esp_err_t prefs_set_dx_filter(const void *in, size_t len);

#ifdef __cplusplus
}
#endif
