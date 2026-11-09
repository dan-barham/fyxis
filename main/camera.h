// PYXIS camera link over Bluetooth LE (Blackmagic Camera Control protocol).
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LINK_SEARCHING,   // scanning for a camera
    LINK_CONNECTING,  // connected, securing / discovering
    LINK_PAIRING,     // waiting for the PIN shown on the camera
    LINK_ONLINE,      // receiving camera settings
} cam_link_t;

typedef struct {
    cam_link_t link;
    bool connected;          // link == LINK_ONLINE
    bool have_data;          // at least one full set of values received (may be stale if !connected)
    bool need_pin;           // camera is asking for its 6-digit PIN

    bool recording;
    int  iso;
    int  shutter_angle_x100; // 18000 = 180.00°
    int  shutter_speed;      // 48 = 1/48
    int  project_fps;        // whole number; see project_mrate
    bool project_mrate;      // true = x1000/1001 (23.98, 29.97, 59.94)
    bool offspeed;
    int  offspeed_fps;
    bool offspeed_mrate;
    int  wb_kelvin;
    int  tint;
    int  res_w, res_h;
    bool tc_valid;           // timecode from the camera's Timecode characteristic
    uint8_t tc_h, tc_m, tc_s, tc_f;
    int  media_s;            // remaining record time on the media, seconds (-1 = unknown)
    int  slot_n;             // card slots reported (9.2); the PYXIS has two CFexpress slots
    int  slot_s[4];          // remaining time per slot, seconds (as last reported; may be stale)
    bool slot_present[2];    // card in the slot (worked out from the transport flags and 9.2)
    int  slot_active;        // slot being recorded to: 0 or 1 (transport flags), -1 = unknown
    int  slot_type[2];       // transport storage-medium fields, logged for checking
    int  transport_flags;    // raw, logged for checking
    int  batt_mv;            // voltage at the camera's DC input, mV (0 = not reported yet)
    int  batt_pct;           // 9.0 second field, if sent (-1 = not sent); logged, not relied on
    int  batt_flags;         // 9.0 third field, if sent
    // Lens (only if the camera reports it: an electronic lens, and only some bodies send it).
    int  lens_fstop_x10;     // 0.2 aperture as an f-number x 10 (28 = f/2.8); 0 = not reported
    int  lens_focal_mm;      // 0.7 focal length; 0 = not reported
    char lens_type[40];      // 12.10, e.g. "Canon EF 24-70mm f/2.8L"
    char lens_iris[12];      // 12.11, e.g. "f2.8" (text, as the camera writes it into metadata)
    char lens_focal[12];     // 12.12, e.g. "35mm"
    char lens_dist[12];      // 12.13, e.g. "1.2m"
    uint32_t rec_req_seq;    // bumps each time a record start / stop is sent
    bool rec_req_start;      // what the last one asked for
    uint32_t seq;            // bumps on every published update
} cam_state_t;

// Link details for the About page.
typedef struct {
    int  conn_itvl_us;       // connection interval, microseconds (0 = not connected)
    int  latency;            // peripheral latency, events
    int  mtu;                // ATT MTU (0 = not exchanged)
    int  data_len;           // link-layer payload, bytes (27 = not extended)
    int  rssi;               // dBm (0 = unknown)
    int  tc_per_s;           // timecode notifications over the last second
    int  rx_per_s;           // settings notifications over the last second
    bool bonded;             // a pairing is stored
    char name[32];           // the camera's Bluetooth name ("" = not read yet)
} cam_diag_t;

// CAM_FPS: recording (sensor) rate, one frame per step. The project rate is left alone;
// any rate other than the project rate turns off-speed on.
// CAM_SHUTTER steps through preset angles; CAM_SHUTTER_SPEED through 1/x speeds (sent as the
// equivalent angle for the current frame rate, which is what the PYXIS uses in angle mode).
typedef enum { CAM_ISO, CAM_SHUTTER, CAM_SHUTTER_SPEED, CAM_WB, CAM_TINT, CAM_FPS } cam_param_t;

void camera_start(void);                 // also initialises NVS (bond storage)
void camera_get(cam_state_t *out);
void camera_step(cam_param_t p, int dir);  // ignored unless online and not recording
void camera_record(bool start);          // start / stop recording (ignored unless online)
void camera_set_shutter_angle(int angle_x100);  // exact angle, e.g. 17280 = 172.8°
void camera_set_shutter_speed(int denom);       // exact 1/x speed, e.g. 50 = 1/50
void camera_set_fps(int fps);                   // recording rate; the project rate = off-speed off
void camera_set_iso(int iso);                   // exact EI
void camera_set_wb(int kelvin, int tint);       // exact white balance and tint
void camera_enter_pin(uint32_t pin);     // answer the pairing request
void camera_forget(void);                // drop the stored pairing and search again
void camera_get_diag(cam_diag_t *out);
