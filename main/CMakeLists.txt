idf_component_register(
    SRCS
        "main.c"
        "bsp/ch422g.c"
        "bsp/board.c"
        "bsp/lcd_timing.c"
        "bsp/lcd_console.c"
        "lib/geo.c"
        "lib/solar.c"
        "lib/units.c"
        "lib/i18n.c"
        "lib/dxcc.c"
        "lib/station.c"
        "lib/prefs.c"
        "lib/landmask.c"
        "lib/selftest.c"
        "net/wifi_manager.c"
        "net/http_get.c"
        "net/spacewx.c"
        "net/propagation.c"
        "net/weather.c"
        "net/dxcluster.c"
        "ui/theme.c"
        "ui/ui.c"
        "ui/ui_common.c"
        "ui/ui_header.c"
        "ui/ui_home.c"
        "ui/ui_setup.c"
        "ui/ui_settings.c"
        "ui/ui_dx.c"
        "ui/ui_map.c"
        "ui/fonts/lv_font_ui_12.c"
        "ui/fonts/lv_font_ui_14.c"
        "ui/fonts/lv_font_ui_16.c"
        "ui/fonts/lv_font_ui_18.c"
        "ui/fonts/lv_font_ui_20.c"
        "ui/fonts/lv_font_ui_24.c"
        "ui/fonts/lv_font_ui_28.c"
        "ui/fonts/lv_font_ui_36.c"
        "ui/fonts/lv_font_ui_48.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_lcd esp_driver_i2c esp_driver_gpio nvs_flash esp_wifi esp_netif
        esp_event esp-tls esp_http_client json mbedtls lwip console esp_driver_uart
)

# Board selection. Defaults to the Waveshare so a plain "idf.py build" still
# produces the original firmware; tools/build_all.py passes -DBOARD for each.
if(NOT DEFINED BOARD)
    set(BOARD "WAVESHARE_7")
endif()
target_compile_definitions(${COMPONENT_LIB} PUBLIC BOARD_${BOARD}=1)
message(STATUS "Building for board: ${BOARD}")

# lv_font_conv output includes <lvgl/lvgl.h> unless told otherwise.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
