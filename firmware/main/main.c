#include <assert.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "bigsix.h"
#include "settings.h"
#include "update.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "bigsix";

// The legacy I2C driver's interrupt runs on the core that installs it. Install it on core 1, where
// the LVGL task polls the touch controller: with the interrupt on core 0 and the polling on core 1,
// the first touch reads at start-up occasionally left the interrupt firing until the watchdog reset.
static void i2c_init_task(void *arg)
{
    ESP_ERROR_CHECK(waveshare_i2c_init());
    xTaskNotifyGive((TaskHandle_t)arg);
    vTaskDelete(NULL);
}

// A new firmware from a Wi-Fi update is on probation until it has run for 10 s; if it resets
// before then, the bootloader goes back to the previous one.
static void confirm_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10000));
    update_mark_good();
    vTaskDelete(NULL);
}

void app_main(void)
{
    settings_init();                                   // NVS + saved settings (orientation is one)
    const bool flip = settings()->flip;

    // DOUBLE_DIRECT: LVGL draws straight into a full-screen buffer and only the areas that changed
    // are copied (turned 180° if needed) into the panel's frame buffers. The previous
    // TRIPLE_PARTIAL mode copied every part of the screen that *hadn't* changed from the front
    // buffer to the back one on every frame - about 750 KB of PSRAM traffic, even for one
    // timecode digit. With rotation the adapter needs a third buffer to draw into.
    const esp_lv_adapter_rotation_t rotation = flip ? ESP_LV_ADAPTER_ROTATE_180 : ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT;
    uint8_t frame_buffer_count = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);
    if (rotation != ESP_LV_ADAPTER_ROTATE_0 && frame_buffer_count < 3) frame_buffer_count = 3;

    xTaskCreatePinnedToCore(i2c_init_task, "i2c_init", 3072, xTaskGetCurrentTaskHandle(), 5, NULL, 1);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(frame_buffer_count, &panel_handle, &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());
    if (touch_handle) {                                // touch follows the picture
        esp_lcd_touch_set_mirror_x(touch_handle, flip);
        esp_lcd_touch_set_mirror_y(touch_handle, flip);
    }

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    // Drawing on core 1; Bluetooth, the panel's refill interrupt and the REC button stay on core 0.
    adapter_config.task_core_id = 1;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation
    );
    disp_config.tear_avoid_mode = tear_mode;
    disp_config.profile.use_psram = true;

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    assert(disp != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ESP_LOGI(TAG, "Starting PYXIS Big Six %s (%s)", BIGSIX_VERSION, flip ? "USB at rear" : "USB at front");
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        bigsix_start();
        esp_lv_adapter_unlock();
    }
    xTaskCreate(confirm_task, "confirm", 3072, NULL, 2, NULL);
}
