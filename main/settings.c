// Settings storage. See settings.h.

#include <string.h>
#include <stddef.h>
#include "settings.h"

static const settings_t DEFAULTS = {
    .rec_hold_stop = 0,
    .tally = TALLY_THICK,
    .clip_timer = 1,
    .flip = 1,
    .start_locked = 0,
    .batt_show = BATT_SHOW_VOLTS,
    .batt_low_mv = 14000,
    .batt_crit_mv = 13200,
    .wb_step = 100,
    .night = 0,
};

static settings_t s_cfg;
static preset_t s_presets[NPRESETS];
preset_t *presets(void) { return s_presets; }

const settings_t *settings(void) { return &s_cfg; }
settings_t *settings_edit(void) { return &s_cfg; }

// Keep stored values sane (a corrupt or older blob must never lock the UI up).
static void sanitise(settings_t *c)
{
    if (c->tally > TALLY_OFF) c->tally = TALLY_THICK;
    if (c->batt_show > BATT_SHOW_PERCENT) c->batt_show = BATT_SHOW_VOLTS;
    c->rec_hold_stop = !!c->rec_hold_stop;
    c->clip_timer = !!c->clip_timer;
    c->flip = !!c->flip;
    c->start_locked = !!c->start_locked;
    c->night = !!c->night;
    if (c->batt_low_mv < 10000 || c->batt_low_mv > 17000) c->batt_low_mv = DEFAULTS.batt_low_mv;
    if (c->batt_crit_mv < 10000 || c->batt_crit_mv >= c->batt_low_mv) c->batt_crit_mv = c->batt_low_mv - 800;
    if (c->wb_step != 50 && c->wb_step != 100 && c->wb_step != 200) c->wb_step = DEFAULTS.wb_step;
}

#ifdef ESP_PLATFORM
// ------------------------------------------------------------------ firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"

#define NS        "bigsix"
#define KEY       "cfg"
#define CFG_VER   1
#define SAVE_DELAY_MS 800

static const char *TAG = "settings";
static TaskHandle_t s_saver;

typedef struct { uint8_t ver; settings_t cfg; } blob_t;

static void write_now(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    blob_t b = { .ver = CFG_VER, .cfg = s_cfg };
    esp_err_t e = nvs_set_blob(h, KEY, &b, sizeof b);
    if (e == ESP_OK) e = nvs_set_blob(h, "presets", s_presets, sizeof s_presets);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved (%s)", esp_err_to_name(e));
}

// Flash writes freeze the caches, which isn't allowed from a task whose stack is in PSRAM (the
// LVGL task's is), so saving happens here. FreeRTOS gives this task an internal-RAM stack.
static void saver_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // wait until changes stop for a moment
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAVE_DELAY_MS))) {}
        write_now();
    }
}

void settings_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    s_cfg = DEFAULTS;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        // Fields are only ever added at the end, so an older, shorter blob fills the start and the
        // new fields keep their defaults.
        uint8_t raw[sizeof(blob_t) + 16];
        size_t n = sizeof raw;
        if (nvs_get_blob(h, KEY, raw, &n) == ESP_OK && n >= 2 && raw[0] == CFG_VER) {
            const size_t off = offsetof(blob_t, cfg);
            const size_t len = n - off < sizeof s_cfg ? n - off : sizeof s_cfg;
            memcpy(&s_cfg, raw + off, len);
        }
        n = sizeof s_presets;
        if (nvs_get_blob(h, "presets", s_presets, &n) != ESP_OK || n != sizeof s_presets)
            memset(s_presets, 0, sizeof s_presets);
        nvs_close(h);
    }
    sanitise(&s_cfg);
    xTaskCreate(saver_task, "settings", 3072, NULL, 3, &s_saver);
}

void settings_changed(void)
{
    sanitise(&s_cfg);
    if (s_saver) xTaskNotifyGive(s_saver);
}

void settings_reset(void)
{
    s_cfg = DEFAULTS;
    settings_changed();
}

void presets_changed(void) { if (s_saver) xTaskNotifyGive(s_saver); }

uint32_t plat_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
uint64_t plat_us(void) { return (uint64_t)esp_timer_get_time(); }
uint32_t plat_free_internal(void) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
uint32_t plat_free_psram(void) { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

static void restart_task(void *arg)
{
    (void)arg;
    write_now();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

void plat_restart(void) { xTaskCreate(restart_task, "restart", 3072, NULL, 5, NULL); }

#else
// ------------------------------------------------------------------ simulator
#include <stdio.h>
#include <time.h>

void settings_init(void) { s_cfg = DEFAULTS; sanitise(&s_cfg); }
void settings_changed(void) { sanitise(&s_cfg); }
void settings_reset(void) { s_cfg = DEFAULTS; }
void presets_changed(void) {}

uint64_t plat_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}
uint32_t plat_ms(void) { return (uint32_t)(plat_us() / 1000); }
uint32_t plat_free_internal(void) { return 0; }
uint32_t plat_free_psram(void) { return 0; }
void plat_restart(void) { printf("[sim] restart requested (the board restarts here)\n"); fflush(stdout); }
#endif
