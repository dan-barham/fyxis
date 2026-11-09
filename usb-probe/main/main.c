// PYXIS USB probe: can the board talk to the camera over the rear USB-C cable?
//
// Two tests, chosen on screen (the choice is saved and the board restarts into it):
//  HOST   - the board is the USB host. If the camera appears as a USB device, list what it offers,
//           and if it has a PTP (still image) interface, open a session and read live properties.
//  DEVICE - the board is a USB device. Report whether the camera acts as a USB host on that port
//           (bus resets and start-of-frame packets from it).
//
// Wiring for the test: flash from the Mac, then UNPLUG THE MAC and connect only the board's USB port
// to the camera, which powers the board. Never connect both: the board's USB-port 5 V is its main 5 V
// rail, and the UART port feeds that rail, so the Mac's 5 V would reach the camera's USB-C port.

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_lv_adapter.h"
#include "waveshare_rgb_lcd_port.h"
#include "usb/usb_host.h"
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"

LV_FONT_DECLARE(font_label_20);
LV_FONT_DECLARE(font_mid_36);

static const char *TAG = "usbprobe";
enum { MODE_HOST = 0, MODE_DEVICE = 1 };
static int s_mode;

// ------------------------------------------------------------------ on-screen log
#define LOG_LINES 15
static lv_obj_t *s_log;
static char s_lines[LOG_LINES][96];
static int s_nlines;

static void say(const char *fmt, ...)
{
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    printf("[probe] %s\n", b);
    if (s_nlines == LOG_LINES) {
        memmove(s_lines[0], s_lines[1], sizeof s_lines[0] * (LOG_LINES - 1));
        s_nlines--;
    }
    strcpy(s_lines[s_nlines++], b);
    if (!s_log) return;
    static char all[LOG_LINES * 97];
    all[0] = 0;
    for (int i = 0; i < s_nlines; i++) { strcat(all, s_lines[i]); strcat(all, "\n"); }
    if (esp_lv_adapter_lock(1000) == ESP_OK) {
        lv_label_set_text(s_log, all);
        esp_lv_adapter_unlock();
    }
}

// Saving to flash freezes the caches, which isn't allowed from a task whose stack is in PSRAM,
// and the LVGL task's stack is (stack_in_psram). So the save runs in its own task, whose stack
// FreeRTOS allocates from internal RAM.
static void save_mode_task(void *arg)
{
    const int m = (int)(intptr_t)arg;
    nvs_handle_t h;
    esp_err_t e = nvs_open("probe", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_i32(h, "mode", m);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) {
        say("Couldn't save the test choice: %s", esp_err_to_name(e));
        vTaskDelete(NULL);
    }
    printf("[probe] switching to %s test, restarting\n", m == MODE_HOST ? "HOST" : "DEVICE");
    esp_restart();
}

static void mode_cb(lv_event_t *e)
{
    const int m = (int)(intptr_t)lv_event_get_user_data(e);
    xTaskCreate(save_mode_task, "savemode", 4096, (void *)(intptr_t)m, 5, NULL);
}

static lv_obj_t *button(lv_obj_t *parent, const char *txt, bool active, int mode)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 370, 64);
    lv_obj_set_style_radius(b, 32, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, active ? lv_color_hex(0x0A84FF) : lv_color_hex(0x282828), 0);
    lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)mode);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &font_label_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &font_mid_36, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(t, s_mode == MODE_HOST ? "USB test: board = host" : "USB test: board = device");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 20, 14);

    lv_obj_t *b1 = button(scr, s_mode == MODE_HOST ? "HOST TEST (running)" : "SWITCH TO HOST TEST", s_mode == MODE_HOST, MODE_HOST);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 20, -14);
    lv_obj_t *b2 = button(scr, s_mode == MODE_DEVICE ? "DEVICE TEST (running)" : "SWITCH TO DEVICE TEST", s_mode == MODE_DEVICE, MODE_DEVICE);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, -20, -14);

    s_log = lv_label_create(scr);
    lv_obj_set_width(s_log, 760);
    lv_obj_set_style_text_font(s_log, &font_label_20, 0);
    lv_obj_set_style_text_color(s_log, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_text_line_space(s_log, 1, 0);
    lv_label_set_text(s_log, "");
    lv_obj_align(s_log, LV_ALIGN_TOP_LEFT, 20, 66);
}

// ------------------------------------------------------------------ DEVICE test
static void device_test_task(void *arg)
{
    (void)arg;
    say("Board is a USB device on its USB port (ESP32 built-in USB serial).");
    say("Connect the board's USB port to the camera's rear USB-C.");
    bool was_sof = false;
    int resets = 0;
    int64_t last_note = 0;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
    for (;;) {
        const uint32_t raw = usb_serial_jtag_ll_get_intraw_mask();
        if (raw & USB_SERIAL_JTAG_INTR_BUS_RESET) {
            usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
            resets++;
            say("USB bus reset from a host (#%d): something is acting as USB host.", resets);
        }
        const bool sof = usb_serial_jtag_is_connected();
        if (sof != was_sof) {
            say(sof ? "Host is running the bus (frames arriving): connected to a USB host."
                    : "Frames stopped: no active USB host.");
            was_sof = sof;
        }
        const int64_t now = esp_timer_get_time();
        if (now - last_note > 15000000) {
            last_note = now;
            say("status: host frames %s, bus resets so far %d", sof ? "YES" : "no", resets);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ------------------------------------------------------------------ HOST test
static usb_host_client_handle_t s_client;
static QueueHandle_t s_new_dev;
static SemaphoreHandle_t s_xfer_done;
static usb_device_handle_t s_dev;

static void host_lib_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        uint8_t a = msg->new_dev.address;
        xQueueSend(s_new_dev, &a, 0);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        say("Device unplugged.");
    }
}

static void client_task(void *arg)
{
    (void)arg;
    for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
}

static void xfer_cb(usb_transfer_t *t) { (void)t; xSemaphoreGive(s_xfer_done); }

static esp_err_t xfer(usb_transfer_t *t, uint8_t ep, int n, int timeout_ms)
{
    t->device_handle = s_dev;
    t->bEndpointAddress = ep;
    t->num_bytes = n;
    t->callback = xfer_cb;
    t->timeout_ms = timeout_ms;
    ESP_RETURN_ON_ERROR(usb_host_transfer_submit(t), TAG, "submit");
    if (xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        usb_host_endpoint_halt(s_dev, ep);
        usb_host_endpoint_flush(s_dev, ep);
        xSemaphoreTake(s_xfer_done, pdMS_TO_TICKS(500));
        usb_host_endpoint_clear(s_dev, ep);
        return ESP_ERR_TIMEOUT;
    }
    return t->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
}

static void str_desc(const usb_str_desc_t *d, char *out, size_t n)
{
    out[0] = 0;
    if (!d) return;
    size_t j = 0;
    for (int i = 0; i < (d->bLength - 2) / 2 && j + 1 < n; i++) {
        const uint16_t c = d->wData[i];
        out[j++] = c < 128 ? (char)c : '?';
    }
    out[j] = 0;
}

// PTP over bulk: command -> optional data in -> response.
static usb_transfer_t *s_out, *s_in;
static uint8_t s_ep_out, s_ep_in;
static int s_mps_in = 64;
static uint32_t s_tid;
static uint8_t s_data[8192];
static int s_data_len;

static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int ptp(uint16_t code, int nparams, uint32_t p1, uint32_t p2)
{
    uint8_t *c = s_out->data_buffer;
    const int len = 12 + 4 * nparams;
    const uint32_t tid = ++s_tid;
    c[0] = len; c[1] = 0; c[2] = 0; c[3] = 0;
    c[4] = 1; c[5] = 0;                                 // command block
    c[6] = code & 0xFF; c[7] = code >> 8;
    memcpy(c + 8, &tid, 4);
    memcpy(c + 12, &p1, 4);
    memcpy(c + 16, &p2, 4);
    if (xfer(s_out, s_ep_out, len, 2000) != ESP_OK) { say("PTP 0x%04X: command not accepted", code); return -1; }
    s_data_len = 0;
    for (int tries = 0; tries < 8; tries++) {
        const int n = (int)sizeof s_data / s_mps_in * s_mps_in;
        const int want = n > 4096 ? 4096 : n;
        if (xfer(s_in, s_ep_in, want, 3000) != ESP_OK) { say("PTP 0x%04X: no reply", code); return -1; }
        const uint8_t *r = s_in->data_buffer;
        const int got = s_in->actual_num_bytes;
        if (got < 12) continue;
        const uint16_t type = rd16(r + 4);
        if (type == 2) {                                // data block (first packet set)
            const int total = (int)rd32(r) - 12;
            int take = got - 12;
            if (take > (int)sizeof s_data) take = sizeof s_data;
            memcpy(s_data, r + 12, take);
            s_data_len = take;
            while (s_data_len < total && s_data_len < (int)sizeof s_data) {      // rest of the data
                if (xfer(s_in, s_ep_in, want, 3000) != ESP_OK) break;
                int m = s_in->actual_num_bytes;
                if (s_data_len + m > (int)sizeof s_data) m = sizeof s_data - s_data_len;
                memcpy(s_data + s_data_len, s_in->data_buffer, m);
                s_data_len += m;
            }
        } else if (type == 3) {                         // response block
            return rd16(r + 6);
        }
    }
    return -1;
}

static int ptp_str(const uint8_t *p, int avail, char *out, size_t n)   // returns bytes used
{
    out[0] = 0;
    if (avail < 1) return 0;
    const int chars = p[0];
    size_t j = 0;
    for (int i = 0; i < chars && 1 + 2 * i + 1 < avail; i++) {
        const uint16_t c = rd16(p + 1 + 2 * i);
        if (!c) break;
        if (j + 1 < n) out[j++] = c < 128 ? (char)c : '?';
    }
    out[j] = 0;
    return 1 + 2 * chars;
}

static int skip_array(const uint8_t *p, int avail, int elem)
{
    if (avail < 4) return avail;
    return 4 + (int)rd32(p) * elem;
}

static void ptp_probe(void)
{
    usb_host_transfer_alloc(512, 0, &s_out);
    usb_host_transfer_alloc(4096, 0, &s_in);
    int rc = ptp(0x1002, 1, 1, 0);                     // OpenSession(1)
    say("PTP OpenSession -> 0x%04X%s", rc & 0xFFFF, rc == 0x2001 ? " (OK)" : "");
    rc = ptp(0x1001, 0, 0, 0);                         // GetDeviceInfo
    if (rc == 0x2001 && s_data_len > 8) {
        const uint8_t *p = s_data;
        int o = 2 + 4 + 2;                             // std version, vendor ext id, vendor ext version
        char tmp[64];
        o += ptp_str(p + o, s_data_len - o, tmp, sizeof tmp);
        o += 2;                                        // functional mode
        const int nops = s_data_len - o >= 4 ? (int)rd32(p + o) : 0;
        for (int i = 0; i < 5; i++) o += skip_array(p + o, s_data_len - o, 2);  // ops, events, props, capture, image
        char man[48], model[48], ver[48];
        o += ptp_str(p + o, s_data_len - o, man, sizeof man);
        o += ptp_str(p + o, s_data_len - o, model, sizeof model);
        o += ptp_str(p + o, s_data_len - o, ver, sizeof ver);
        say("PTP camera: %s %s, firmware %s, %d operations", man, model, ver, nops);
    } else {
        say("PTP GetDeviceInfo -> 0x%04X", rc & 0xFFFF);
    }
    // Live values the Mac test read with gphoto2: D006 (shutter angle x100), D00A (recording).
    const uint16_t props[] = { 0xD006, 0xD00A };
    for (int i = 0; i < 2; i++) {
        rc = ptp(0x1015, 1, props[i], 0);              // GetDevicePropValue
        if (rc == 0x2001 && s_data_len > 0) {
            uint32_t v = 0;
            memcpy(&v, s_data, s_data_len < 4 ? s_data_len : 4);
            say("PTP property 0x%04X = %lu  (live camera data over USB)", props[i], (unsigned long)v);
        } else {
            say("PTP property 0x%04X -> 0x%04X", props[i], rc & 0xFFFF);
        }
    }
    say("Change shutter on the camera and tap HOST TEST to read again.");
}

static void host_test_task(void *arg)
{
    (void)arg;
    say("Board is the USB host on its USB port, waiting for a device...");
    say("Note: the board's USB-C port identifies itself as a device (5.1k on CC),");
    say("so the camera will probably act as host and this test may find nothing.");
    for (;;) {
        uint8_t addr;
        if (xQueueReceive(s_new_dev, &addr, pdMS_TO_TICKS(15000)) != pdTRUE) {
            say("still no USB device seen (camera not acting as a device)");
            continue;
        }
        if (usb_host_device_open(s_client, addr, &s_dev) != ESP_OK) { say("device %d: open failed", addr); continue; }
        usb_device_info_t info;
        usb_host_device_info(s_dev, &info);
        char man[48], prod[48];
        str_desc(info.str_desc_manufacturer, man, sizeof man);
        str_desc(info.str_desc_product, prod, sizeof prod);
        const usb_device_desc_t *dd;
        usb_host_get_device_descriptor(s_dev, &dd);
        say("DEVICE FOUND: VID %04X PID %04X class %d  \"%s\" \"%s\"", dd->idVendor, dd->idProduct,
            dd->bDeviceClass, man, prod);
        const usb_config_desc_t *cd;
        usb_host_get_active_config_descriptor(s_dev, &cd);
        const uint8_t *p = (const uint8_t *)cd;
        int ptp_if = -1;
        s_ep_in = s_ep_out = 0;
        for (int o = 0; o + 2 <= cd->wTotalLength && p[o]; o += p[o]) {
            if (p[o + 1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                say("  interface %d: class %d subclass %d protocol %d", p[o + 2], p[o + 5], p[o + 6], p[o + 7]);
                if (p[o + 5] == 6 && ptp_if < 0) ptp_if = p[o + 2];
            } else if (p[o + 1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && ptp_if >= 0) {
                const uint8_t ep = p[o + 2], attr = p[o + 3] & 3;
                if (attr == 2 && (ep & 0x80) && !s_ep_in) { s_ep_in = ep; s_mps_in = p[o + 4] | p[o + 5] << 8; }
                if (attr == 2 && !(ep & 0x80) && !s_ep_out) s_ep_out = ep;
            }
        }
        if (ptp_if >= 0 && s_ep_in && s_ep_out) {
            if (usb_host_interface_claim(s_client, s_dev, ptp_if, 0) == ESP_OK) {
                say("PTP interface %d claimed, talking to the camera...", ptp_if);
                ptp_probe();
            } else {
                say("PTP interface %d: claim failed", ptp_if);
            }
        } else {
            say("No PTP (still image) interface on this device.");
        }
    }
}

static void start_host_test(void)
{
    s_new_dev = xQueueCreate(4, 1);
    s_xfer_done = xSemaphoreCreateBinary();
    const usb_host_config_t hc = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    esp_err_t e = usb_host_install(&hc);
    if (e != ESP_OK) { say("usb_host_install failed: %s", esp_err_to_name(e)); return; }
    xTaskCreate(host_lib_task, "usbhost", 4096, NULL, 6, NULL);
    const usb_host_client_config_t cc = {
        .is_synchronous = false, .max_num_event_msg = 5,
        .async = { .client_event_callback = client_cb, .callback_arg = NULL },
    };
    usb_host_client_register(&cc, &s_client);
    xTaskCreate(client_task, "usbclient", 4096, NULL, 5, NULL);
    xTaskCreate(host_test_task, "probe", 6144, NULL, 4, NULL);
}

// ------------------------------------------------------------------ main
void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    nvs_handle_t h;
    int32_t m = MODE_DEVICE;                           // start with the passive test
    if (nvs_open("probe", NVS_READONLY, &h) == ESP_OK) { nvs_get_i32(h, "mode", &m); nvs_close(h); }
    s_mode = (int)m;

    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
    const uint8_t fb_count = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(fb_count, &panel, &touch));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());   // also sets USB_SEL low: GPIO19/20 -> USB port
    esp_lv_adapter_config_t ac = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ac.task_stack_size = 12 * 1024;
    ac.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&ac));
    esp_lv_adapter_display_config_t dc = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(panel, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    dc.profile.use_psram = true;
    lv_display_t *disp = esp_lv_adapter_register_display(&dc);
    if (touch) {
        esp_lv_adapter_touch_config_t tc = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
        esp_lv_adapter_register_touch(&tc);
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    if (esp_lv_adapter_lock(-1) == ESP_OK) { build_ui(); esp_lv_adapter_unlock(); }

    say("PYXIS USB probe. Power from the CAMERA only: board's USB port -> camera.");
    say("Do not connect the Mac at the same time.");
    if (s_mode == MODE_HOST) start_host_test();
    else xTaskCreate(device_test_task, "probe", 4096, NULL, 4, NULL);
}
