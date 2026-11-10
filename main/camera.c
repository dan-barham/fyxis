// PYXIS camera link over Bluetooth LE.
//
// Protocol encoding/decoding and the camera state live in camproto.c (shared with
// the Mac simulator). This file is only the Bluetooth LE transport.
//
// Reliability:
//   - Pairs once with the camera's PIN; the bond is stored in NVS and reused.
//   - Any disconnect restarts scanning straight away; reconnection needs no input.
//   - The UI is told the link state so stale values are never shown as live.

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_npl.h"
#include "board.h"
#include "camproto.h"
#include "camera.h"

void ble_store_config_init(void);

static const char *TAG = "camera";

static const ble_uuid128_t UUID_SVC  = BLE_UUID128_INIT(0xd3, 0x93, 0xa8, 0x0c, 0xf3, 0x86, 0x77, 0x8b, 0xe6, 0x11, 0x75, 0x6d, 0x7a, 0x56, 0x1d, 0x29);
static const ble_uuid128_t UUID_OUT  = BLE_UUID128_INIT(0xbb, 0xe1, 0xf8, 0xa2, 0xec, 0xd2, 0x93, 0x84, 0x99, 0x42, 0xee, 0x1a, 0x5f, 0x46, 0xd3, 0x5d);
static const ble_uuid128_t UUID_IN   = BLE_UUID128_INIT(0xd9, 0x37, 0x45, 0x50, 0x76, 0x58, 0x30, 0xbf, 0x6a, 0x41, 0xa0, 0x76, 0x40, 0xe1, 0x64, 0xb8);
static const ble_uuid128_t UUID_TC   = BLE_UUID128_INIT(0xc8, 0x76, 0xe9, 0x87, 0x1d, 0x45, 0xfb, 0x9a, 0xbf, 0x41, 0xf1, 0x86, 0x10, 0x21, 0x8f, 0x6d);
static const ble_uuid128_t UUID_NAME = BLE_UUID128_INIT(0x9c, 0xb8, 0x2e, 0x28, 0x76, 0xcc, 0x63, 0xb0, 0xa0, 0x41, 0xfb, 0xc9, 0x52, 0x0c, 0xac, 0xff);
static const ble_uuid16_t  UUID_CCCD = BLE_UUID16_INIT(0x2902);

#define DEVICE_NAME "Big Six"
#define MAX_CHRS    16

static SemaphoreHandle_t s_lock;

static uint8_t  s_own_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_in_val, s_tc_val;
static uint8_t  s_in_props, s_tc_props, s_out_props;
static uint16_t s_out_val, s_name_val;
static uint16_t s_chr_defs[MAX_CHRS];
static int      s_nchr;

// Link details for the About page (guarded by s_lock, like the camera state).
static cam_diag_t s_diag;
static int s_tc_count, s_rx_count;             // notifications since the last 1 s tick
static struct ble_npl_callout s_stats_timer;

// Connection timing. The central (us) picks the interval: 7.5-15 ms instead of NimBLE's
// default 30-50 ms, so a REC press reaches the camera, and its answer comes back, within a few
// milliseconds, and timecode notifications arrive evenly instead of in bunches. Units: 1.25 ms.
#define CONN_ITVL_MIN   6
#define CONN_ITVL_MAX   12
#define CONN_TIMEOUT    256            // supervision timeout, x10 ms (2.56 s, NimBLE's default)
// Safety net: if a connection with the fast timing fails to come online twice in a row, connect
// with NimBLE's defaults instead (until one succeeds), so the timing can never lock us out.
static int s_fast_fails;
static bool s_went_online, s_pin_asked;     // s_pin_asked: a PIN prompt (not a timing problem)

static void start_scan(void);

// Scanning. Awake: NimBLE's default, listening all the time, so the camera is found the moment it
// advertises. While the screen sleeps: listen 60 ms in every 640 ms (about a tenth of the radio
// time), so finding the camera again takes a second or two longer. Units: 0.625 ms.
#define SCAN_SLOW_ITVL  0x0400
#define SCAN_SLOW_WIN   0x0060
static volatile bool s_scan_slow;
static bool s_synced;                       // host ready; on_sync starts the first scan
static struct ble_npl_event s_scan_ev;      // restarts the scan with the new duty cycle

// ---------- shared-protocol hooks ----------
void camproto_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
void camproto_unlock(void) { xSemaphoreGive(s_lock); }

static void set_link(cam_link_t l) { camproto_set_link(l, false); }

static void diag_conn_update(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) != 0) return;
    camproto_lock();
    s_diag.conn_itvl_us = d.conn_itvl * 1250;
    s_diag.latency = d.conn_latency;
    camproto_unlock();
    ESP_LOGI(TAG, "connection interval %.2f ms, latency %d, timeout %d ms",
             d.conn_itvl * 1.25, d.conn_latency, d.supervision_timeout * 10);
}

// Once a second: signal strength and notification rates.
static void stats_tick(struct ble_npl_event *ev)
{
    (void)ev;
    int8_t rssi = 0;
    const bool conn = s_conn != BLE_HS_CONN_HANDLE_NONE;
    if (conn) ble_gap_conn_rssi(s_conn, &rssi);
    int bonds = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &bonds);
    camproto_lock();
    s_diag.rssi = conn ? rssi : 0;
    s_diag.tc_per_s = s_tc_count;
    s_diag.rx_per_s = s_rx_count;
    s_diag.bonded = bonds > 0;
    s_tc_count = s_rx_count = 0;
    camproto_unlock();
    ble_npl_callout_reset(&s_stats_timer, ble_npl_time_ms_to_ticks32(1000));
}

// The camera's name (GAP Device Name), for the settings screen.
static int on_name(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status == 0 && attr && attr->om) {
        char n[sizeof s_diag.name] = {0};
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        if (len >= sizeof n) len = sizeof n - 1;
        os_mbuf_copydata(attr->om, 0, len, n);
        camproto_lock();
        memcpy(s_diag.name, n, sizeof n);
        camproto_unlock();
        ESP_LOGI(TAG, "camera name: %s", n);
    }
    return 0;
}

// ---------- outgoing queue ----------
// The link allows one acknowledged write at a time, so writes queue here and go out one after
// another (a preset sends four). REC goes to the front of the queue.
#define TXQ 16
static struct { uint8_t len; uint8_t b[32]; } s_txq[TXQ];
static int s_tx_head, s_tx_n;
static bool s_tx_busy;
static SemaphoreHandle_t s_txlock;

static void tx_kick(void);

static int on_written(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status) ESP_LOGW(TAG, "write answered with error %d", err->status);
    xSemaphoreTake(s_txlock, portMAX_DELAY);
    s_tx_busy = false;
    xSemaphoreGive(s_txlock);
    tx_kick();
    return 0;
}

static void tx_kick(void)
{
    for (;;) {
        uint8_t b[32];
        int len;
        xSemaphoreTake(s_txlock, portMAX_DELAY);
        if (s_tx_busy || !s_tx_n || s_conn == BLE_HS_CONN_HANDLE_NONE || !s_out_val) {
            xSemaphoreGive(s_txlock);
            return;
        }
        len = s_txq[s_tx_head].len;
        memcpy(b, s_txq[s_tx_head].b, len);
        s_tx_head = (s_tx_head + 1) % TXQ;
        s_tx_n--;
        const bool ack = s_out_props & BLE_GATT_CHR_PROP_WRITE;
        s_tx_busy = ack;
        xSemaphoreGive(s_txlock);
        int rc = ack ? ble_gattc_write_flat(s_conn, s_out_val, b, len, on_written, NULL)
                     : ble_gattc_write_no_rsp_flat(s_conn, s_out_val, b, len);
        if (rc) {
            ESP_LOGW(TAG, "write failed: %d", rc);
            xSemaphoreTake(s_txlock, portMAX_DELAY);
            s_tx_busy = false;
            xSemaphoreGive(s_txlock);
        }
        if (ack && !rc) return;                    // the next one goes when this is answered
    }
}

static void tx_clear(void)
{
    xSemaphoreTake(s_txlock, portMAX_DELAY);
    s_tx_head = s_tx_n = 0;
    s_tx_busy = false;
    xSemaphoreGive(s_txlock);
}

static void send_packet_at(const uint8_t *b, int len, bool front)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_out_val || len <= 0 || len > 32) return;
    xSemaphoreTake(s_txlock, portMAX_DELAY);
    if (s_tx_n == TXQ) { s_tx_head = (s_tx_head + 1) % TXQ; s_tx_n--; }   // full: drop the oldest
    int slot;
    if (front) { s_tx_head = (s_tx_head + TXQ - 1) % TXQ; slot = s_tx_head; }
    else slot = (s_tx_head + s_tx_n) % TXQ;
    s_txq[slot].len = len;
    memcpy(s_txq[slot].b, b, len);
    s_tx_n++;
    xSemaphoreGive(s_txlock);
    tx_kick();
}

static void send_packet(const uint8_t *b, int len) { send_packet_at(b, len, false); }

// ---------- GATT discovery chain ----------
// Subscriptions, in order: camera settings (indications), then timecode (notifications).
typedef struct { uint16_t val, end, cccd; uint8_t props; } sub_t;
static sub_t s_subs[2];
static int s_nsubs, s_sub_i;

static void subscribe_next(uint16_t conn);

static int on_cccd_written(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status != 0) {
        if (s_sub_i == 0) {                        // settings are essential
            ESP_LOGE(TAG, "subscribe failed: %d", err->status);
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        ESP_LOGW(TAG, "timecode subscribe failed: %d (carrying on without it)", err->status);
    }
    s_sub_i++;
    subscribe_next(conn);
    return 0;
}

static void write_cccd(uint16_t conn, sub_t *sb)
{
    if (!sb->cccd) sb->cccd = sb->val + 1;        // usual layout if no descriptor was listed
    uint16_t v = (sb->props & BLE_GATT_CHR_PROP_NOTIFY) ? 0x0001 : 0x0002;
    ble_gattc_write_flat(conn, sb->cccd, &v, sizeof v, on_cccd_written, NULL);
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val, const struct ble_gatt_dsc *dsc, void *arg)
{
    sub_t *sb = &s_subs[s_sub_i];
    if (err->status == 0) {
        if (ble_uuid_cmp(&dsc->uuid.u, &UUID_CCCD.u) == 0) sb->cccd = dsc->handle;
        return 0;
    }
    write_cccd(conn, sb);                          // done (or failed): subscribe
    return 0;
}

static void subscribe_next(uint16_t conn)
{
    if (s_sub_i >= s_nsubs) {
        if (s_name_val) ble_gattc_write_flat(conn, s_name_val, DEVICE_NAME, strlen(DEVICE_NAME), NULL, NULL);
        ESP_LOGI(TAG, "online");
        s_went_online = true;
        s_fast_fails = 0;
        set_link(LINK_ONLINE);
        static const ble_uuid16_t name_uuid = BLE_UUID16_INIT(0x2A00);
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, &name_uuid.u, on_name, NULL);
        return;
    }
    sub_t *sb = &s_subs[s_sub_i];
    if (sb->end > sb->val) ble_gattc_disc_all_dscs(conn, sb->val, sb->end, on_dsc, NULL);
    else write_cccd(conn, sb);
}

// A characteristic's descriptors run up to the next characteristic's declaration.
static uint16_t chr_end(uint16_t val)
{
    uint16_t end = s_svc_end;
    for (int i = 0; i < s_nchr; i++)
        if (s_chr_defs[i] > val && s_chr_defs[i] - 1 < end) end = s_chr_defs[i] - 1;
    return end;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0) {
        if (s_nchr < MAX_CHRS) s_chr_defs[s_nchr++] = chr->def_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_IN.u) == 0)   { s_in_val = chr->val_handle; s_in_props = chr->properties; }
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_TC.u) == 0)   { s_tc_val = chr->val_handle; s_tc_props = chr->properties; }
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_OUT.u) == 0)  { s_out_val = chr->val_handle; s_out_props = chr->properties; }
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_NAME.u) == 0) { s_name_val = chr->val_handle; }
        return 0;
    }
    if (err->status != BLE_HS_EDONE || !s_in_val || !s_out_val) {
        ESP_LOGE(TAG, "camera service incomplete");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    s_nsubs = 0;
    s_subs[s_nsubs++] = (sub_t){ s_in_val, chr_end(s_in_val), 0, s_in_props };
    if (s_tc_val) s_subs[s_nsubs++] = (sub_t){ s_tc_val, chr_end(s_tc_val), 0, s_tc_props };
    s_sub_i = 0;
    subscribe_next(conn);
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE && s_svc_start) {
        s_nchr = 0;
        ble_gattc_disc_all_chrs(conn, s_svc_start, s_svc_end, on_chr, NULL);
    } else {
        ESP_LOGE(TAG, "camera service not found");
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

static void start_discovery(uint16_t conn)
{
    s_svc_start = s_svc_end = s_in_val = s_tc_val = s_out_val = s_name_val = 0;
    ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC.u, on_svc, NULL);
}

// Bigger ATT packets first (CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU), so the camera's burst of
// settings after subscribing arrives in fewer packets. Discovery runs whatever the answer.
static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    if (err->status == 0) {
        ESP_LOGI(TAG, "ATT MTU %u", mtu);
        camproto_lock();
        s_diag.mtu = mtu;
        camproto_unlock();
    } else ESP_LOGW(TAG, "MTU exchange failed: %d (carrying on at 23)", err->status);
    start_discovery(conn);
    return 0;
}

// ---------- GAP ----------
static bool adv_has_service(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) return false;
    for (int i = 0; i < f.num_uuids128; i++)
        if (ble_uuid_cmp(&f.uuids128[i].u, &UUID_SVC.u) == 0) return true;
    return false;
}

static void forget_peer(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
}

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        if (adv_has_service(&ev->disc)) {
            ble_gap_disc_cancel();
            const struct ble_gap_conn_params cp = {
                .scan_itvl = 0x0010, .scan_window = 0x0010,
                .itvl_min = CONN_ITVL_MIN, .itvl_max = CONN_ITVL_MAX,
                .latency = 0, .supervision_timeout = CONN_TIMEOUT,
                .min_ce_len = 0, .max_ce_len = 0,
            };
            const bool fast = s_fast_fails < 2;
            if (!fast) ESP_LOGW(TAG, "fast timing failed twice; connecting with default timing");
            s_went_online = s_pin_asked = false;
            if (ble_gap_connect(s_own_addr_type, &ev->disc.addr, 10000, fast ? &cp : NULL, gap_cb, NULL) != 0) start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_conn == BLE_HS_CONN_HANDLE_NONE) start_scan();
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) { s_fast_fails++; start_scan(); return 0; }
        s_conn = ev->connect.conn_handle;
        camproto_reset();
        camproto_lock();
        memset(s_diag.name, 0, sizeof s_diag.name);
        s_diag.mtu = 0;
        s_diag.data_len = 27;
        camproto_unlock();
        diag_conn_update(s_conn);
        // Longer link-layer packets: a 247-byte ATT packet goes in one radio packet, not ten.
        if (ble_gap_set_data_len(s_conn, 251, 2120) == 0) {
            camproto_lock();
            s_diag.data_len = 251;
            camproto_unlock();
        }
        set_link(LINK_CONNECTING);
        ESP_LOGI(TAG, "connected, securing link");
        ble_gap_security_initiate(s_conn);   // re-encrypts with a stored bond, or starts PIN pairing
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (ev->passkey.params.action == BLE_SM_IOACT_INPUT) {
            s_pin_asked = true;
            camproto_set_link(LINK_PAIRING, true);
        } else if (ev->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io io = { .action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 1 };
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) {
            set_link(LINK_CONNECTING);
            if (ble_gattc_exchange_mtu(ev->enc_change.conn_handle, on_mtu, NULL) != 0)
                start_discovery(ev->enc_change.conn_handle);
        } else {
            // Wrong PIN, or the camera forgot us: drop our copy so the next attempt pairs fresh.
            ESP_LOGW(TAG, "securing failed (%d), clearing pairing", ev->enc_change.status);
            forget_peer(ev->enc_change.conn_handle);
            ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ev->conn_update.status == 0) diag_conn_update(ev->conn_update.conn_handle);
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
    case BLE_GAP_EVENT_CONN_UPDATE_REQ: {
        // The camera asks for different timing. Accept, but at the fastest interval it allows
        // and with no peripheral latency (latency would delay our REC writes by whole intervals).
        const struct ble_gap_upd_params *peer = ev->conn_update_req.peer_params;
        struct ble_gap_upd_params *self = ev->conn_update_req.self_params;
        ESP_LOGI(TAG, "camera asks for interval %.2f-%.2f ms, latency %d",
                 peer->itvl_min * 1.25, peer->itvl_max * 1.25, peer->latency);
        self->itvl_min = self->itvl_max = peer->itvl_min;
        self->latency = 0;
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        forget_peer(ev->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (ev->notify_rx.attr_handle == s_in_val || ev->notify_rx.attr_handle == s_tc_val) {
            uint8_t buf[256];
            uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
            if (n > sizeof buf) n = sizeof buf;
            if (os_mbuf_copydata(ev->notify_rx.om, 0, n, buf) != 0) return 0;
            if (ev->notify_rx.attr_handle == s_in_val) { s_rx_count++; camproto_decode(buf, n); }
            else { s_tc_count++; camproto_decode_timecode(buf, n); }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnected (reason 0x%x)", ev->disconnect.reason);
        if (!s_went_online && !s_pin_asked) s_fast_fails++;   // dropped before it got going
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        tx_clear();
        camproto_lock();
        s_diag.conn_itvl_us = 0;
        s_diag.rssi = 0;
        camproto_unlock();
        set_link(LINK_SEARCHING);
        start_scan();
        return 0;

    default:
        return 0;
    }
}

static void start_scan(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_disc_active() || ble_gap_conn_active()) return;
    struct ble_gap_disc_params p = {0};
    p.filter_duplicates = 1;
    p.passive = 0;                 // active: the service UUID may be in the scan response
    if (s_scan_slow) { p.itvl = SCAN_SLOW_ITVL; p.window = SCAN_SLOW_WIN; }   // 0 = NimBLE's default
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &p, gap_cb, NULL);
    if (rc) ESP_LOGE(TAG, "scan start failed: %d", rc);
}

// Runs on the NimBLE host task: a scan in progress is restarted with the new duty cycle.
static void scan_mode_ev(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_active()) return;
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    start_scan();
    ESP_LOGI(TAG, "scanning %s", s_scan_slow ? "slowly (screen asleep)" : "continuously");
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_synced = true;
    set_link(LINK_SEARCHING);
    start_scan();
    ble_npl_callout_init(&s_stats_timer, nimble_port_get_dflt_eventq(), stats_tick, NULL);
    ble_npl_callout_reset(&s_stats_timer, ble_npl_time_ms_to_ticks32(1000));
}

static void on_reset(int reason) { ESP_LOGE(TAG, "BLE host reset: %d", reason); }

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---------- public API ----------
void camera_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_txlock = xSemaphoreCreateMutex();

    // Keep the USB port's data lines disconnected: the camera cable only powers the board.
    board_usb_route(true);

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth failed to start");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;   // we type the PIN the camera shows
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_npl_event_init(&s_scan_ev, scan_mode_ev, NULL);
    nimble_port_freertos_init(host_task);
}

void camera_set_low_power(bool on)
{
    if (s_scan_slow == on) return;
    s_scan_slow = on;
    if (s_synced) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_scan_ev);
}

void camera_get(cam_state_t *out) { camproto_get(out); }

void camera_get_diag(cam_diag_t *out)
{
    camproto_lock();
    *out = s_diag;
    camproto_unlock();
}

void camera_enter_pin(uint32_t pin)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct ble_sm_io io = { .action = BLE_SM_IOACT_INPUT, .passkey = pin };
    ble_sm_inject_io(s_conn, &io);
    camproto_set_link(LINK_CONNECTING, false);
}

// Called from the UI (LVGL) task. Erasing the stored pairing writes to flash, which freezes the
// caches, and that isn't allowed from a task whose stack is in PSRAM (the LVGL task's is). So the
// erase runs in its own task, whose stack FreeRTOS allocates from internal RAM.
static void forget_task(void *arg)
{
    (void)arg;
    ble_store_clear();
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    ESP_LOGI(TAG, "pairing forgotten");
    vTaskDelete(NULL);
}

void camera_forget(void)
{
    xTaskCreate(forget_task, "forget", 4096, NULL, 5, NULL);
}

void camera_step(cam_param_t p, int dir)
{
    uint8_t b[32];
    send_packet(b, camproto_build_step(p, dir, b));
}

void camera_record(bool start)
{
    uint8_t b[32];
    send_packet_at(b, camproto_build_record(start, b), true);   // ahead of anything queued
}

void camera_playback(int mode, int speed)
{
    uint8_t b[32];
    send_packet_at(b, camproto_build_transport(mode, speed, 0, b), true);
}

void camera_playback_clip(int steps)
{
    uint8_t b[32];
    send_packet(b, camproto_build_clip_step(steps > 0, b));
}

void camera_set_iso(int iso)
{
    uint8_t b[32];
    send_packet(b, camproto_build_iso(iso, b));
}

void camera_set_wb(int kelvin, int tint)
{
    uint8_t b[32];
    send_packet(b, camproto_build_wb(kelvin, tint, b));
}

void camera_set_shutter_angle(int angle_x100)
{
    uint8_t b[32];
    send_packet(b, camproto_build_shutter_angle(angle_x100, b));
}

void camera_set_shutter_speed(int denom)
{
    uint8_t b[32];
    send_packet(b, camproto_build_shutter_speed(denom, b));
}

void camera_set_fps(int fps)
{
    uint8_t b[32];
    send_packet(b, camproto_build_fps(fps, b));
}
