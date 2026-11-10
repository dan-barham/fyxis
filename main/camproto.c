// Blackmagic Camera Control protocol (same packet format over SDI and Bluetooth LE):
//   [dest][length][command=0][reserved] [category][parameter][type][operation][data...] pad to 4
//
// Parameters used (confirmed against a PYXIS 12K over Bluetooth):
//   1.2  white balance       int16[2]  {kelvin, tint}
//   1.9  recording format    int16[5]  {project fps, sensor fps, width, height, flags}
//        flags: bit0 project M-rate, bit1 sensor M-rate, bit2 off-speed
//   1.11 shutter angle       int32     degrees x 100 (the PYXIS sends this, not 1.12, in angle mode)
//   1.12 shutter speed       int32     1/x
//   1.14 ISO                 int32
//   0.2  lens aperture       fixed16  aperture value AV, f-number = sqrt(2^AV)
//   0.7  lens focal length   int16    mm
//   12.10-12.13 lens type / iris / focal length / distance: strings (clip metadata)
//   9.2  remaining record    int16[..] seconds per media slot (seen dropping 1429 -> 1142 -> 714
//        as the frame rate rose 23.98 -> 29.97 -> 48; Blackmagic's REST API documents seconds)
//   10.1 transport mode      int8[5]   {mode, speed, flags, slot 1 medium, slot 2 medium}
//        mode: 0 preview, 1 play, 2 record. Recording is started/stopped by writing it back
//        with the mode changed (and, to start, the playback speed and loop flags cleared, so REC
//        rolls a clip straight from playback).
//        speed: playback, 0 paused, 1 normal, >1 faster, <0 backwards. flags: bit0 loop, bit1 play all.
// Timecode characteristic: 32-bit BCD HH:MM:SS:FF (09:12:53:10 = 0x09125310).

#include <string.h>
#include "camproto.h"
#include "settings.h"
#include <math.h>

static cam_state_t s_st = { .media_s = -1, .batt_pct = -1, .slot_active = -1 };
static int32_t s_transport[5];
static bool s_have_transport;

// Card slots. The PYXIS reports an empty slot as 0 time left (same as a full card), and on eject it
// clears the slot's transport flag but doesn't send a new time. So: a slot holds a card if its flag
// is set, or if it reports time left and hasn't been ejected since.
static int  s_slot_bits = -1;          // transport flags bits 5/6 as last seen (-1 = not yet)
static bool s_slot_ejected[2];
static int  s_slot_last[2] = { -1, -1 };

static void slots_update(void)
{
    for (int i = 0; i < 2; i++) {
        const bool flag = s_slot_bits >= 0 && (s_slot_bits >> i) & 1;
        const bool has_time = i < s_st.slot_n && s_st.slot_s[i] > 0;
        s_st.slot_present[i] = flag || (has_time && !s_slot_ejected[i]);
    }
}
enum { GOT_ISO = 1, GOT_ANG = 2, GOT_SPD = 4, GOT_WB = 8, GOT_FMT = 16 };
static unsigned s_got;

static const int ISO_LIST[] = { 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200 };
// Preset shutter angles (x100), finer than before.
static const int ANGLES[] = { 1120, 1500, 2250, 3000, 3750, 4500, 6000, 7200, 7500, 9000, 10800, 12000,
                              14400, 15000, 17280, 18000, 21600, 27000, 32400, 36000 };
// Preset shutter speeds (1/x), third-stop steps.
static const int SPEEDS[] = { 24, 25, 30, 40, 48, 50, 60, 80, 96, 100, 120, 125, 160, 200, 250, 320, 400,
                              500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000 };

static double rec_fps(const cam_state_t *st)
{
    return st->offspeed ? st->offspeed_fps * (st->offspeed_mrate ? 1000.0 / 1001.0 : 1.0)
                        : st->project_fps  * (st->project_mrate  ? 1000.0 / 1001.0 : 1.0);
}

static int angle_for_speed(const cam_state_t *st, int denom)
{
    const double f = rec_fps(st);
    if (denom <= 0 || f <= 0) return 0;
    return (int)(36000.0 * f / denom + 0.5);
}

// The last 1/x speed requested and the angle it was sent as. The angle only has 0.01°
// resolution, so converting back can land a little off (1/1728 -> 1/1726); when the camera's
// angle still matches, report the speed that was asked for.
static int s_req_speed, s_req_angle;
static double s_req_fps;

int camproto_speed_for_angle(const cam_state_t *st, int a)
{
    const double f = rec_fps(st);
    if (a <= 0 || f <= 0) return 0;
    if (s_req_speed && a == s_req_angle && f == s_req_fps) return s_req_speed;
    return (int)(f * 36000.0 / a + 0.5);
}

static void remember_speed(const cam_state_t *st, int denom, int angle)
{
    s_req_speed = denom;
    s_req_angle = angle;
    s_req_fps = rec_fps(st);
}

// 1 int8, 2 int16, 3 int32, 5 string (bytes), 128 fixed16 (5.11 fixed point)
static int type_size(int type) { return type == 1 || type == 5 ? 1 : type == 2 || type == 128 ? 2 : type == 3 ? 4 : 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int32_t rd(const uint8_t *p, int type)
{
    switch (type) {
    case 1:  return (int8_t)p[0];
    case 2:
    case 128: return (int16_t)(p[0] | (p[1] << 8));
    case 3:  return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
    default: return 0;
    }
}

void camproto_reset(void)
{
    camproto_lock();
    s_got = 0;
    camproto_unlock();
}

void camproto_set_link(cam_link_t link, bool need_pin)
{
    camproto_lock();
    s_st.link = link;
    s_st.connected = (link == LINK_ONLINE);
    s_st.need_pin = need_pin;
    s_st.seq++;
    camproto_unlock();
}

void camproto_get(cam_state_t *out)
{
    camproto_lock();
    *out = s_st;
    camproto_unlock();
}

static void copy_str(char *dst, size_t cap, const uint8_t *d, int len)
{
    int n = len < (int)cap - 1 ? len : (int)cap - 1;
    while (n > 0 && d[n - 1] == 0) n--;                // strings may be padded with NULs
    memcpy(dst, d, n);
    dst[n] = 0;
}

static void handle_param(int cat, int par, int type, const uint8_t *d, int len)
{
    const int sz = type_size(type);
    if (!sz) return;
    if (type == 5) {                                   // text: lens metadata
        camproto_lock();
        if (cat == 12 && par == 10) copy_str(s_st.lens_type, sizeof s_st.lens_type, d, len);
        else if (cat == 12 && par == 11) copy_str(s_st.lens_iris, sizeof s_st.lens_iris, d, len);
        else if (cat == 12 && par == 12) copy_str(s_st.lens_focal, sizeof s_st.lens_focal, d, len);
        else if (cat == 12 && par == 13) copy_str(s_st.lens_dist, sizeof s_st.lens_dist, d, len);
        s_st.seq++;
        camproto_unlock();
        return;
    }
    const int n = len / sz;
    int32_t v[5] = {0};
    for (int i = 0; i < n && i < 5; i++) v[i] = rd(d + i * sz, type);

    camproto_lock();
    if (cat == 1 && par == 14 && n >= 1)      { s_st.iso = v[0]; s_got |= GOT_ISO; }
    else if (cat == 1 && par == 11 && n >= 1) { s_st.shutter_angle_x100 = v[0]; s_got |= GOT_ANG; }
    else if (cat == 1 && par == 12 && n >= 1) { s_st.shutter_speed = v[0]; s_got |= GOT_SPD; }
    else if (cat == 1 && par == 2 && n >= 2)  { s_st.wb_kelvin = v[0]; s_st.tint = v[1]; s_got |= GOT_WB; }
    else if (cat == 1 && par == 9 && n >= 5) {
        s_st.project_fps    = v[0];
        s_st.offspeed_fps   = v[1];
        s_st.res_w          = v[2];
        s_st.res_h          = v[3];
        s_st.project_mrate  = v[4] & 1;
        s_st.offspeed_mrate = (v[4] >> 1) & 1;
        s_st.offspeed       = (v[4] >> 2) & 1;
        s_got |= GOT_FMT;
    }
    else if (cat == 10 && par == 1 && n >= 1) {
        s_st.recording = (v[0] == 2);
        s_st.transport_mode = v[0];
        s_st.transport_speed = n >= 2 ? v[1] : 0;
        for (int i = 0; i < 5; i++) s_transport[i] = i < n ? v[i] : 0;
        s_have_transport = (n >= 5);
        if (n >= 3) {                                  // flags: bit 5 = slot 1 active, bit 6 = slot 2
            s_st.slot_active = (v[2] & 0x20) ? 0 : (v[2] & 0x40) ? 1 : -1;
            s_st.transport_flags = v[2];
            const int bits = (v[2] >> 5) & 3;
            if (s_slot_bits >= 0) {
                for (int i = 0; i < 2; i++) {
                    const bool was = (s_slot_bits >> i) & 1, now = (bits >> i) & 1;
                    const bool other_rose = !((s_slot_bits >> (1 - i)) & 1) && ((bits >> (1 - i)) & 1);
                    if (was && !now && !other_rose) s_slot_ejected[i] = true;   // flag gone, not a switch
                    if (now) s_slot_ejected[i] = false;
                }
            }
            s_slot_bits = bits;
            slots_update();
        }
        if (n >= 5) { s_st.slot_type[0] = v[3]; s_st.slot_type[1] = v[4]; }
    }
    else if (cat == 9 && par == 2 && n >= 1) {
        int best = 0;                                  // first slot with time left
        for (int i = 0; i < n && i < 5; i++) if (v[i] > 0) { best = v[i]; break; }
        s_st.media_s = best;
        s_st.slot_n = n < 4 ? n : 4;
        for (int i = 0; i < s_st.slot_n; i++) s_st.slot_s[i] = v[i];
        for (int i = 0; i < 2 && i < n; i++) {         // a fresh, different time = a card again
            if (v[i] > 0 && v[i] != s_slot_last[i]) s_slot_ejected[i] = false;
            s_slot_last[i] = v[i];
        }
        slots_update();
    }
    else if (cat == 0 && par == 2 && n >= 1 && type == 128) {   // aperture value -> f-number
        const double av = v[0] / 2048.0;
        s_st.lens_fstop_x10 = av > -2 && av < 20 ? (int)(pow(2.0, av / 2.0) * 10.0 + 0.5) : 0;
    }
    else if (cat == 0 && par == 7 && n >= 1) {
        s_st.lens_focal_mm = v[0] > 0 && v[0] < 5000 ? v[0] : 0;
    }
    else if (cat == 9 && par == 0 && n >= 1) {        // battery / DC input: mV, then optional fields
        s_st.batt_mv    = v[0];
        s_st.batt_pct   = n >= 2 ? v[1] : -1;
        s_st.batt_flags = n >= 3 ? v[2] : 0;
    }

    if ((s_got & (GOT_ISO | GOT_WB | GOT_FMT)) == (GOT_ISO | GOT_WB | GOT_FMT)) s_st.have_data = true;
    s_st.seq++;
    camproto_unlock();
}

void camproto_decode(const uint8_t *p, int len)
{
    int pos = 0;
    while (pos + 4 <= len) {
        const int clen = p[pos + 1];
        const int cmd  = p[pos + 2];
        const uint8_t *body = p + pos + 4;
        if (pos + 4 + clen > len) break;               // truncated packet
        pos += 4 + clen;
        pos += (4 - (pos & 3)) & 3;                    // 32-bit padding
        if (cmd != 0 || clen < 4) continue;
        handle_param(body[0], body[1], body[2], body + 4, clen - 4);
    }
}

static bool bcd_ok(uint32_t v)
{
    for (int i = 0; i < 8; i++) if (((v >> (4 * i)) & 0xF) > 9) return false;
    return ((v >> 24) & 0xFF) <= 0x23 && ((v >> 16) & 0xFF) <= 0x59 &&
           ((v >> 8) & 0xFF) <= 0x59 && (v & 0xFF) <= 0x59;
}

static int bcd(uint32_t b) { return (int)((b >> 4) * 10 + (b & 0xF)); }

void camproto_decode_timecode(const uint8_t *p, int len)
{
    if (len < 4) return;
    // Try the most likely layouts: the last or first 4 bytes, little- then big-endian.
    // Bit 31 may carry a drop-frame flag, so it's masked off before checking.
    const int offs[2] = { len - 4, 0 };
    for (int o = 0; o < 2; o++) {
        const uint8_t *b = p + offs[o];
        uint32_t cand[2] = {
            (uint32_t)b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24),
            (uint32_t)b[3] | (b[2] << 8) | (b[1] << 16) | ((uint32_t)b[0] << 24),
        };
        for (int e = 0; e < 2; e++) {
            uint32_t v = cand[e] & 0x3FFFFFFF;
            if (!bcd_ok(v)) continue;
            camproto_lock();
            s_st.tc_h = bcd(v >> 24); s_st.tc_m = bcd((v >> 16) & 0xFF);
            s_st.tc_s = bcd((v >> 8) & 0xFF); s_st.tc_f = bcd(v & 0xFF);
            s_st.tc_valid = true;
            s_st.seq++;
            camproto_unlock();
            return;
        }
    }
}

int camproto_build(int cat, int par, int type, const int32_t *vals, int n, uint8_t out[32])
{
    const int sz = type_size(type);
    const int clen = 4 + n * sz;
    if (!sz || n < 0 || 4 + clen > 32) return 0;
    memset(out, 0, 32);
    out[0] = 255; out[1] = clen; out[2] = 0; out[3] = 0;
    out[4] = cat; out[5] = par; out[6] = type; out[7] = 0;   // operation 0 = assign
    for (int i = 0; i < n; i++)
        for (int k = 0; k < sz; k++) out[8 + i * sz + k] = (uint8_t)(vals[i] >> (8 * k));
    int total = 4 + clen;
    return total + ((4 - (total & 3)) & 3);
}

// Recording (sensor) rate. The project rate is never changed: the project rate itself means
// off-speed off (sensor follows the project, including 23.98 etc.); anything else is off-speed.
static int fps_packet(const cam_state_t *st, int fps, uint8_t out[32])
{
    const int cur = st->offspeed ? st->offspeed_fps : st->project_fps;
    if (fps < 1 || fps > 120 || fps == cur) return 0;
    const int pm = st->project_mrate ? 1 : 0;
    int32_t v[5] = { st->project_fps, fps, st->res_w, st->res_h, 0 };
    v[4] = (fps == st->project_fps) ? (pm | (pm << 1)) : (pm | 4);
    return camproto_build(1, 9, 2, v, 5, out);
}

int camproto_build_fps(int fps, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected || !st.have_data || st.recording) return 0;
    return fps_packet(&st, fps, out);
}

int camproto_build_step(cam_param_t p, int dir, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected || !st.have_data || st.recording) return 0;
    dir = dir > 0 ? 1 : -1;

    switch (p) {
    case CAM_ISO: {
        const int n = (int)(sizeof ISO_LIST / sizeof ISO_LIST[0]);
        int i = 0;
        while (i < n - 1 && ISO_LIST[i] < st.iso) i++;  // first entry >= current ISO
        if (ISO_LIST[i] == st.iso) i += dir;              // on a step: move one
        else if (dir < 0) i -= 1;                         // between steps: down = the lower one, up = i
        i = clampi(i, 0, n - 1);
        if (ISO_LIST[i] == st.iso) return 0;
        int32_t v = ISO_LIST[i];
        return camproto_build(1, 14, 3, &v, 1, out);
    }
    case CAM_SHUTTER: {
        const int n = (int)(sizeof ANGLES / sizeof ANGLES[0]);
        int32_t v = st.shutter_angle_x100;
        if (dir > 0) { for (int i = 0; i < n; i++) if (ANGLES[i] > st.shutter_angle_x100) { v = ANGLES[i]; break; } }
        else         { for (int i = n - 1; i >= 0; i--) if (ANGLES[i] < st.shutter_angle_x100) { v = ANGLES[i]; break; } }
        if (v == st.shutter_angle_x100) return 0;
        return camproto_build(1, 11, 3, &v, 1, out);
    }
    case CAM_SHUTTER_SPEED: {
        // Next preset speed that is faster (+) or slower (−) than now and still a legal angle.
        const int n = (int)(sizeof SPEEDS / sizeof SPEEDS[0]);
        const int cur = camproto_speed_for_angle(&st, st.shutter_angle_x100);
        int32_t a = 0;
        int pick = 0;
        if (dir > 0) { for (int i = 0; i < n; i++) if (SPEEDS[i] > cur) { pick = SPEEDS[i]; break; } }
        else         { for (int i = n - 1; i >= 0; i--) if (SPEEDS[i] < cur) { pick = SPEEDS[i]; break; } }
        a = angle_for_speed(&st, pick);
        if (a < 1 || a > 36000 || a == st.shutter_angle_x100) return 0;
        remember_speed(&st, pick, a);
        return camproto_build(1, 11, 3, &a, 1, out);
    }
    case CAM_WB: {
        // Step size from the settings; the first step lands on a multiple of it (5640 -> 5700).
        const int step = settings()->wb_step;
        int k = st.wb_kelvin + step * dir;
        k = dir > 0 ? k / step * step : (st.wb_kelvin - 1) / step * step;
        int32_t v[2] = { clampi(k, 2500, 10000), st.tint };
        if (v[0] == st.wb_kelvin) return 0;
        return camproto_build(1, 2, 2, v, 2, out);
    }
    case CAM_TINT: {
        int32_t v[2] = { st.wb_kelvin, clampi(st.tint + dir, -50, 50) };
        if (v[1] == st.tint) return 0;
        return camproto_build(1, 2, 2, v, 2, out);
    }
    case CAM_FPS: {
        const int cur = st.offspeed ? st.offspeed_fps : st.project_fps;
        return fps_packet(&st, clampi(cur + dir, 1, 120), out);
    }
    }
    return 0;
}

int camproto_build_record(bool start, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected) return 0;
    camproto_lock();
    int32_t v[5];
    for (int i = 0; i < 5; i++) v[i] = s_transport[i];
    const bool full = s_have_transport;
    camproto_unlock();
    v[0] = start ? 2 : 0;
    v[1] = 0;                                          // no playback speed
    v[2] &= ~3;                                        // nor loop / play all: REC rolls a clip, even from playback
    const int len = camproto_build(10, 1, 1, v, full ? 5 : 1, out);
    if (len) {                                         // lets the UI show the press straight away
        camproto_lock();
        s_st.rec_req_seq++;
        s_st.rec_req_start = start;
        s_st.seq++;
        camproto_unlock();
    }
    return len;
}

int camproto_build_shutter_angle(int angle_x100, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected || !st.have_data || st.recording) return 0;
    if (angle_x100 < 1 || angle_x100 > 36000) return 0;
    int32_t v = angle_x100;
    return camproto_build(1, 11, 3, &v, 1, out);
}

int camproto_build_shutter_speed(int denom, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    const int a = angle_for_speed(&st, denom);
    const int len = camproto_build_shutter_angle(a, out);
    if (len) remember_speed(&st, denom, a);
    return len;
}

int camproto_build_iso(int iso, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected || !st.have_data || st.recording || iso < 50 || iso > 25600 || iso == st.iso) return 0;
    int32_t v = iso;
    return camproto_build(1, 14, 3, &v, 1, out);
}

int camproto_build_wb(int kelvin, int tint, uint8_t out[32])
{
    cam_state_t st;
    camproto_get(&st);
    if (!st.connected || !st.have_data || st.recording) return 0;
    int32_t v[2] = { clampi(kelvin, 2500, 10000), clampi(tint, -50, 50) };
    if (v[0] == st.wb_kelvin && v[1] == st.tint) return 0;
    return camproto_build(1, 2, 2, v, 2, out);
}
