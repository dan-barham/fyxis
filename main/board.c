// Waveshare ESP32-S3-Touch-LCD-4.3: GPIO19/20 go through an FSUSB42 switch to
// either the native USB-C port or the CAN transceiver. The switch is driven by
// EXIO5 on the CH422G I/O expander (I2C, port 0). EXIO5 LOW = USB.
//
// The CH422G output register is write-only, so every write sets all pins. We keep the
// levels Waveshare's backlight_on() uses (0x1E) and change only our two pins:
//   EXIO1 touch reset high, EXIO2 backlight on, EXIO3 LCD reset high, EXIO4 SD CS high,
//   EXIO5 USB / CAN switch (board_usb_route), EXIO2 backlight (plat_display_power).
//
// This uses the legacy I2C driver on purpose: it's what Waveshare's board port
// already installed, and the legacy driver serialises access per port, so this
// is safe alongside touch polling.

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "board.h"
#include "settings.h"
#include "waveshare_rgb_lcd_port.h"

#define CH422G_MODE_ADDR  0x24   // write 0x01: IO0-7 as outputs
#define CH422G_OUT_ADDR   0x38   // output levels for EXIO0-7
#define CH422G_BASE       0x1E
#define CH422G_BACKLIGHT  (1 << 2)
#define CH422G_USB_SEL    (1 << 5)
#define I2C_PORT          0
#define I2C_TIMEOUT       pdMS_TO_TICKS(100)

// Pixel clock while the display sleeps. The backlight is off and the picture is black, so the
// panel only needs refreshing slowly: 4 MHz is about 10 frames a second instead of 39, and cuts
// the CPU's work refilling the panel's bounce buffers from PSRAM by the same factor.
#define SLEEP_PCLK_HZ     (4 * 1000 * 1000)

static const char *TAG = "board";
// Last levels written. Both callers run on the LVGL task (camera_start() is called from
// bigsix_start()), so this needs no lock.
static uint8_t s_out = CH422G_BASE;
static esp_lcd_panel_handle_t s_panel;

static esp_err_t ch422g_update(uint8_t clear, uint8_t set)
{
    s_out = (s_out & ~clear) | set;
    const uint8_t out = s_out;
    uint8_t mode = 0x01;
    esp_err_t e = i2c_master_write_to_device(I2C_PORT, CH422G_MODE_ADDR, &mode, 1, I2C_TIMEOUT);
    if (e == ESP_OK) e = i2c_master_write_to_device(I2C_PORT, CH422G_OUT_ADDR, &out, 1, I2C_TIMEOUT);
    return e;
}

void board_usb_route(bool alt)
{
    esp_err_t e = ch422g_update(alt ? 0 : CH422G_USB_SEL, alt ? CH422G_USB_SEL : 0);
    ESP_LOGI(TAG, "USB_SEL %s (%s)", alt ? "HIGH" : "LOW", e == ESP_OK ? "ok" : esp_err_to_name(e));
}

void board_set_panel(esp_lcd_panel_handle_t panel) { s_panel = panel; }

void plat_display_power(bool on)
{
    // Waking: full pixel clock first (it takes effect at the next frame), then the backlight.
    if (on && s_panel) esp_lcd_rgb_panel_set_pclk(s_panel, EXAMPLE_LCD_PIXEL_CLOCK_HZ);
    esp_err_t e = ch422g_update(on ? 0 : CH422G_BACKLIGHT, on ? CH422G_BACKLIGHT : 0);
    if (!on && s_panel) esp_lcd_rgb_panel_set_pclk(s_panel, SLEEP_PCLK_HZ);
    ESP_LOGI(TAG, "display %s (%s)", on ? "awake" : "asleep", e == ESP_OK ? "ok" : esp_err_to_name(e));
}
