// Board-specific hooks for the Waveshare ESP32-S3-Touch-LCD-4.3.
#pragma once
#include <stdbool.h>
#include "esp_lcd_types.h"

// GPIO19/20 go through a switch to either the native USB-C port (alt = false)
// or the CAN transceiver (alt = true). With the Bluetooth link the camera cable
// only powers the board, so the firmware selects CAN: the port's data lines are
// then disconnected and the camera just sees a device drawing power.
void board_usb_route(bool alt);

// The RGB panel, for plat_display_power() (settings.h), which slows its pixel clock while the
// display sleeps. Call once from main.c after the panel is created.
void board_set_panel(esp_lcd_panel_handle_t panel);
