// Firmware update over Wi-Fi. See update.h.

#include <stdio.h>
#include <string.h>
#include "update.h"
#include "settings.h"

#ifdef ESP_PLATFORM
// ------------------------------------------------------------------ firmware
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_mac.h"

static const char *TAG = "update";
static SemaphoreHandle_t s_mu;
static upd_status_t s_st;
static httpd_handle_t s_http;
static esp_netif_t *s_netif;
static bool s_wifi_inited;

static void set_state(upd_state_t st, int pct, const char *msg)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_st.state = st;
    s_st.pct = pct;
    if (msg) snprintf(s_st.msg, sizeof s_st.msg, "%s", msg);
    xSemaphoreGive(s_mu);
}

static const char PAGE[] =
"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>FYXIS update</title><style>"
"body{font:16px -apple-system,system-ui,sans-serif;background:#000;color:#fff;max-width:480px;margin:40px auto;padding:0 16px}"
"h1{font-size:24px}p{color:#8a8a8a}input{margin:16px 0;color:#fff}"
"button{background:#0a84ff;color:#fff;border:0;border-radius:24px;padding:12px 28px;font-size:16px}"
"#bar{height:8px;background:#282828;border-radius:4px;margin-top:20px}#fill{height:8px;width:0;background:#0a84ff;border-radius:4px}"
"</style></head><body><h1>FYXIS firmware update</h1>"
"<p>Choose <b>pyxis_bigsix.bin</b> from firmware/build, then Upload. The board restarts when it's done.</p>"
"<input type=file id=f accept='.bin'><br><button onclick=go()>Upload</button>"
"<div id=bar><div id=fill></div></div><p id=s></p><script>"
"function go(){var f=document.getElementById('f').files[0];if(!f)return;var x=new XMLHttpRequest();"
"x.upload.onprogress=function(e){document.getElementById('fill').style.width=(100*e.loaded/e.total)+'%'};"
"x.onload=function(){document.getElementById('s').textContent=x.responseText};"
"x.onerror=function(){document.getElementById('s').textContent='Upload failed'};"
"x.open('POST','/update');x.send(f);document.getElementById('s').textContent='Uploading...'}"
"</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof PAGE - 1);
}

static esp_err_t fail(httpd_req_t *req, const char *why)
{
    ESP_LOGE(TAG, "%s", why);
    set_state(UPD_FAILED, 0, why);
    httpd_resp_set_status(req, "500 Update failed");
    httpd_resp_sendstr(req, why);
    return ESP_FAIL;
}

static void restart_soon(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    plat_restart();
    vTaskDelete(NULL);
}

// The image arrives as the raw request body. Runs in the HTTP server's task (internal-RAM stack,
// so the flash writes are allowed).
static esp_err_t update_post(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return fail(req, "No update slot (partition table without OTA slots)");
    const int total = req->content_len;
    if (total <= 0 || total > (int)part->size) return fail(req, "File is empty or too big");

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) return fail(req, "Couldn't start writing");
    static char buf[4096];
    int got = 0;
    set_state(UPD_RECEIVING, 0, "");
    while (got < total) {
        int n = httpd_req_recv(req, buf, sizeof buf);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { esp_ota_abort(ota); return fail(req, "Upload interrupted"); }
        if (got == 0 && (uint8_t)buf[0] != 0xE9) { esp_ota_abort(ota); return fail(req, "Not an ESP32 firmware file"); }
        if (esp_ota_write(ota, buf, n) != ESP_OK) { esp_ota_abort(ota); return fail(req, "Write failed"); }
        got += n;
        set_state(UPD_RECEIVING, (int)((int64_t)got * 100 / total), NULL);
    }
    esp_err_t e = esp_ota_end(ota);
    if (e != ESP_OK) return fail(req, e == ESP_ERR_OTA_VALIDATE_FAILED ? "File failed its check (corrupt or wrong chip)" : "Couldn't finish");
    if (esp_ota_set_boot_partition(part) != ESP_OK) return fail(req, "Couldn't select the new firmware");
    set_state(UPD_DONE, 100, "");
    httpd_resp_sendstr(req, "Done. The board is restarting with the new firmware.");
    ESP_LOGI(TAG, "update written to %s (%d bytes), restarting", part->label, got);
    xTaskCreate(restart_soon, "restart_soon", 3072, NULL, 5, NULL);
    return ESP_OK;
}

// Wi-Fi set-up writes its calibration to flash, which isn't allowed from the LVGL task (its stack
// is in PSRAM), so starting happens in a task of its own.
static void start_task(void *arg)
{
    (void)arg;
    esp_err_t e = ESP_OK;
    if (!s_wifi_inited) {
        esp_netif_init();
        esp_event_loop_create_default();
        s_netif = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        e = esp_wifi_init(&cfg);
        if (e == ESP_OK) { esp_wifi_set_storage(WIFI_STORAGE_RAM); s_wifi_inited = true; }
    }
    if (e == ESP_OK) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        wifi_config_t ap = { 0 };
        xSemaphoreTake(s_mu, portMAX_DELAY);
        snprintf(s_st.ssid, sizeof s_st.ssid, "FYXIS-%02X%02X", mac[4], mac[5]);
        snprintf(s_st.pass, sizeof s_st.pass, "%08lu", (unsigned long)(esp_random() % 100000000UL));
        snprintf(s_st.url, sizeof s_st.url, "http://192.168.4.1");
        memcpy(ap.ap.ssid, s_st.ssid, strlen(s_st.ssid));          // both fit: 10 and 8 characters
        memcpy(ap.ap.password, s_st.pass, strlen(s_st.pass));
        xSemaphoreGive(s_mu);
        ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
        ap.ap.channel = 6;
        ap.ap.max_connection = 2;
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        e = esp_wifi_set_mode(WIFI_MODE_AP);
        if (e == ESP_OK) e = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (e == ESP_OK) e = esp_wifi_start();
    }
    if (e == ESP_OK && !s_http) {
        httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
        hc.stack_size = 8192;
        hc.recv_wait_timeout = 15;
        e = httpd_start(&s_http, &hc);
        if (e == ESP_OK) {
            const httpd_uri_t get = { .uri = "/", .method = HTTP_GET, .handler = page_get };
            const httpd_uri_t post = { .uri = "/update", .method = HTTP_POST, .handler = update_post };
            httpd_register_uri_handler(s_http, &get);
            httpd_register_uri_handler(s_http, &post);
        }
    }
    if (e == ESP_OK) { ESP_LOGI(TAG, "waiting for an upload on %s", s_st.ssid); set_state(UPD_READY, 0, ""); }
    else { ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(e)); set_state(UPD_FAILED, 0, "Wi-Fi didn't start"); }
    vTaskDelete(NULL);
}

static void stop_task(void *arg)
{
    (void)arg;
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
    if (s_wifi_inited) esp_wifi_stop();
    set_state(UPD_OFF, 0, "");
    vTaskDelete(NULL);
}

static void ensure_init(void)
{
    if (s_mu) return;
    s_mu = xSemaphoreCreateMutex();
    const esp_partition_t *run = esp_ota_get_running_partition();
    snprintf(s_st.slot, sizeof s_st.slot, "%s", run ? run->label : "?");
}

void update_start(void)
{
    ensure_init();
    if (s_st.state == UPD_STARTING || s_st.state == UPD_READY || s_st.state == UPD_RECEIVING) return;
    set_state(UPD_STARTING, 0, "");
    xTaskCreate(start_task, "upd_start", 6144, NULL, 5, NULL);
}

void update_stop(void)
{
    ensure_init();
    if (s_st.state == UPD_RECEIVING || s_st.state == UPD_DONE) return;   // don't cut an update off
    xTaskCreate(stop_task, "upd_stop", 4096, NULL, 5, NULL);
}

void update_get(upd_status_t *out)
{
    ensure_init();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_mu);
}

void update_mark_good(void)
{
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed");
    }
}

#else
// ------------------------------------------------------------------ simulator
static upd_status_t s_st = { .slot = "ota_0" };

void update_start(void)
{
    s_st.state = UPD_READY;
    snprintf(s_st.ssid, sizeof s_st.ssid, "FYXIS-22A6");
    snprintf(s_st.pass, sizeof s_st.pass, "48213957");
    snprintf(s_st.url, sizeof s_st.url, "http://192.168.4.1");
    printf("[sim] Wi-Fi update started (the board opens its own network here)\n");
    fflush(stdout);
}
void update_stop(void) { s_st.state = UPD_OFF; }
void update_get(upd_status_t *out) { *out = s_st; }
void update_mark_good(void) {}
#endif
