// Physical REC button on one GPIO. See recbutton.h for the wiring and behaviour.

#include <stdbool.h>
#include "recbutton.h"
#include "camera.h"
#include "settings.h"

#define LOCKOUT_MS   400    // a second press this soon after one that acted is ignored
#define HOLD_MS      500    // "Hold to stop"

static volatile uint32_t s_hint_seq;
static volatile int s_hold_pct;
static uint32_t s_last_act_ms;
static bool s_acted_once;
static bool s_holding;             // down, waiting for the hold to complete
static uint32_t s_down_ms;

uint32_t recbutton_hint_seq(void) { return s_hint_seq; }
int recbutton_hold_progress(void) { return s_hold_pct; }

static void act(bool start)
{
    s_last_act_ms = plat_ms();
    s_acted_once = true;
    camera_record(start);
}

void recbutton_down(void)
{
    const uint32_t now = plat_ms();
    if (s_acted_once && now - s_last_act_ms < LOCKOUT_MS) return;
    cam_state_t st;
    camera_get(&st);
    if (!st.connected) return;
    if (st.recording && settings()->rec_hold_stop) {   // stop only after a hold
        s_holding = true;
        s_down_ms = now;
        s_hold_pct = 1;
        return;
    }
    act(!st.recording);
}

void recbutton_up(void)
{
    if (s_holding) {                                    // let go before the hold completed
        s_holding = false;
        s_hold_pct = 0;
        s_hint_seq++;
    }
}

// While held: completes the hold. Called every few ms by the firmware task / sim loop.
static void hold_poll(void)
{
    if (!s_holding) return;
    const uint32_t held = plat_ms() - s_down_ms;
    if (held >= HOLD_MS) {
        s_holding = false;
        s_hold_pct = 0;
        cam_state_t st;
        camera_get(&st);
        if (st.connected && st.recording) act(false);
    } else {
        s_hold_pct = 1 + (int)(held * 99 / HOLD_MS);
    }
}

#ifdef ESP_PLATFORM
// ------------------------------------------------------------------ firmware
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define PIN          GPIO_NUM_6          // sensor header, "AD"
#define CONFIRM_US   1500                // still low after this: a real press, not a spike
#define RELEASE_MS   30                  // high this long: released (bounce over)
#define POLL_MS      5

static const char *TAG = "recbutton";
static TaskHandle_t s_task;

static void IRAM_ATTR edge_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void button_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, 0);                      // drop edges left over from bounce
        if (gpio_get_level(PIN) != 0) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait for a press
        // A short check filters a spike on the wire without adding noticeable delay.
        esp_rom_delay_us(CONFIRM_US);
        if (gpio_get_level(PIN) != 0) continue;
        recbutton_down();
        // Wait for a clean release; bounce on the way down or up is ignored here.
        int high_ms = 0;
        while (high_ms < RELEASE_MS) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            hold_poll();
            high_ms = gpio_get_level(PIN) ? high_ms + POLL_MS : 0;
        }
        recbutton_up();
    }
}

void recbutton_start(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io);
    // High priority: the press goes out before the display or anything else gets the CPU.
    xTaskCreatePinnedToCore(button_task, "recbutton", 3072, NULL, 12, &s_task, 0);
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGE(TAG, "ISR service: %s", esp_err_to_name(e));
    gpio_isr_handler_add(PIN, edge_isr, NULL);
    ESP_LOGI(TAG, "REC button on GPIO%d (interrupt)", PIN);
}

#else
// ------------------------------------------------------------------ simulator
void recbutton_start(void) {}
void recbutton_sim_poll(void) { hold_poll(); }
#endif
