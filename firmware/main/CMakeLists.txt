idf_component_register(
    SRCS
        "main.c"
        "../components/waveshare_rgb_lcd_port.c"
        "camera.c" "camproto.c" "bigsix.c" "board.c" "recbutton.c" "settings.c" "update.c"
        "font_label_14.c" "font_label_20.c" "font_tc_26.c" "font_icons_22.c" "font_icons_28.c"
        "font_icons_48.c" "font_mid_36.c" "font_value_64.c" "font_big_132.c"
    INCLUDE_DIRS
        "."
        "../components"
    REQUIRES
        esp_lvgl_adapter esp_lcd esp_lcd_touch esp_lcd_touch_gt911 lvgl
        driver esp_driver_gpio esp_timer nvs_flash bt log esp_rom esp_wifi esp_netif esp_event esp_http_server app_update)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
