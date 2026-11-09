// Board-specific hooks for the Waveshare ESP32-S3-Touch-LCD-4.3.
#pragma once
#include <stdbool.h>

// GPIO19/20 go through a switch to either the native USB-C port (alt = false)
// or the CAN transceiver (alt = true). With the Bluetooth link the camera cable
// only powers the board, so the firmware selects CAN: the port's data lines are
// then disconnected and the camera just sees a device drawing power.
void board_usb_route(bool alt);
