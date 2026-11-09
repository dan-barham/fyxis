idf_component_register(
    SRCS "main.c" "../components/waveshare_rgb_lcd_port.c" "font_label_20.c" "font_mid_36.c"
    INCLUDE_DIRS "." "../components"
    REQUIRES esp_lvgl_adapter esp_lcd esp_lcd_touch esp_lcd_touch_gt911 lvgl
             driver esp_driver_gpio esp_driver_usb_serial_jtag usb hal nvs_flash log esp_timer)
