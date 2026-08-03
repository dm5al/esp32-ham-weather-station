/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * Interface language.
 *
 * A flat table of string IDs rather than gettext-style lookup by English text:
 * the compiler then catches a missing translation as a missing initialiser, and
 * no hashing or string comparison happens at draw time.
 *
 * Every language is stored UTF-8 and rendered from the same Montserrat faces —
 * tools/gen_fonts.sh builds sizes 12 to 28 with Latin-1 (0xA0-0xFF) and Cyrillic
 * (0x400-0x4FF), so German umlauts, French accents and Russian all draw without
 * a second typeface. The 36 and 48 px faces are ASCII-only and are used for the
 * callsign and clocks, which never need either.
 *
 * Changing language rebuilds every screen, exactly as a palette change does.
 */
#pragma once

#include "lib/prefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* header */
    S_LOCAL_LBL, S_UPDATED, S_NEXT, S_NEVER, S_NOW, S_IN_MIN,
    S_NOT_COMMISSIONED, S_WAITING_TIME,

    /* home */
    S_LOCAL_WEATHER, S_SUNRISE, S_SUNSET, S_GREYLINE, S_FEELS,
    S_SOLAR, S_RADIO_BLACKOUT, S_SOLAR_RADIATION, S_GEOMAGNETIC,
    S_KP_FORECAST, S_WARNINGS, S_NO_WARNINGS, S_BAND_CONDITIONS,
    S_NONE, S_ACTIVE,
    S_W_STORM_FC, S_W_HF_DEGRADED, S_W_BLACKOUT, S_W_SUNLIT,
    S_W_STORM_NOW, S_W_HIGHLAT, S_W_RADIATION, S_W_POLAR,

    /* map */
    S_PROPAGATION, S_WSPR_MEASURED, S_GRIDS, S_FURTHEST,
    S_MUF_NA, S_NEAREST_IONOSONDE, S_SNR, S_AZIMUTHAL, S_GREYLINE_BTN,

    /* dx cluster */
    S_DX_SOURCE, S_DX_FREQ, S_DX_BAND, S_DX_COUNTRY, S_DX_SPOTTER,
    S_DX_COMMENT, S_DX_EMPTY, S_DX_LOADING,
    S_DX_RARE, S_DX_DISTANT, S_DX_ALL, S_INFO,
    S_W_GALE, S_W_GALE_D, S_W_WIND, S_W_WIND_D, S_W_STORM, S_W_STORM_D,
    S_W_RAIN, S_W_RAIN_D, S_I_RARE_D, S_ALL_QUIET, S_ON_OPEN,
    S_CHANGE, S_OK, S_CANCEL, S_BACK, S_DX_SOURCES,
    S_ABOUT, S_FACTORY_RESET, S_CONFIRM_RESET, S_SPOTTED,

    /* settings */
    S_SETTINGS, S_STATION, S_EDIT, S_NIGHT_MODE, S_OFF, S_AUTO, S_ON,
    S_CHOOSE_NETWORK, S_OFFLINE, S_LANGUAGE, S_UNITS, S_PRIVACY, S_AUTO_SWITCH,
    S_NOT_SET, S_TAP_EDIT, S_AUTHOR, S_LICENCE, S_SOURCES, S_MEASURING,
    S_CONTACT, S_FIRMWARE_VER, S_COMPONENTS, S_SERVICES_USED, S_PRIVACY_WEATHER,
    S_PRIVACY_TITLE,

    /* commissioning */
    S_STEP_FORMAT, S_STEP_STATION, S_CHOOSE_WIFI, S_RESCAN, S_SCANNING,
    S_CONNECTING, S_PASSWORD_FOR, S_NET_PASSWORD, S_CONNECT, S_TAP_CONNECT,
    S_NO_NETWORKS, S_CONNECT_FAIL, S_STATION_TITLE, S_CALLSIGN, S_LOCATOR,
    S_FINISH, S_NEXT_STEP, S_PRIVACY_SHORT, S_LOC_HINT, S_LOC_BAD,
    S_LANG_UNITS_TITLE,

    S_COUNT,
} str_id_t;

/** @brief The string in the current language. Never NULL. */
const char *T(str_id_t id);

/** @brief Language name in its own language, for the picker. */
const char *lang_name(lang_t l);

#ifdef __cplusplus
}
#endif
