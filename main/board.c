// Waveshare ESP32-S3-Touch-LCD-4.3: GPIO19/20 go through an FSUSB42 switch to
// either the native USB-C port or the CAN transceiver. The switch is driven by
// EXIO5 on the CH422G I/O expander (I2C, port 0). EXIO5 LOW = USB.
//
// The CH422G output register is write-only, so every write sets all pins.
// We keep the same levels Waveshare's backlight_on() uses (0x1E):
//   EXIO1 touch reset high, EXIO2 backlight on, EXIO3 LCD reset high, EXIO4 SD CS high.
//
// This uses the legacy I2C driver on purpose: it's what Waveshare's board port
// already installed, and the legacy driver serialises access per port, so this
// is safe alongside touch polling.

#include "driver/i2c.h"
#include "esp_log.h"
#include "board.h"

#define CH422G_MODE_ADDR  0x24   // write 0x01: IO0-7 as outputs
#define CH422G_OUT_ADDR   0x38   // output levels for EXIO0-7
#define CH422G_BASE       0x1E
#define CH422G_USB_SEL    (1 << 5)
#define I2C_PORT          0
#define I2C_TIMEOUT       pdMS_TO_TICKS(100)

void board_usb_route(bool alt)
{
    uint8_t mode = 0x01;
    uint8_t out = CH422G_BASE | (alt ? CH422G_USB_SEL : 0);
    esp_err_t e = i2c_master_write_to_device(I2C_PORT, CH422G_MODE_ADDR, &mode, 1, I2C_TIMEOUT);
    if (e == ESP_OK) e = i2c_master_write_to_device(I2C_PORT, CH422G_OUT_ADDR, &out, 1, I2C_TIMEOUT);
    ESP_LOGI("board", "USB_SEL %s (%s)", alt ? "HIGH" : "LOW", e == ESP_OK ? "ok" : esp_err_to_name(e));
}
