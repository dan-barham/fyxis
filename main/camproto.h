// Blackmagic Camera Control protocol + camera state, shared by the firmware
// (camera.c, Bluetooth on the ESP32) and the Mac simulator (sim/camera_sim.c).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "camera.h"

// Provided by whichever transport file is linked in.
void camproto_lock(void);
void camproto_unlock(void);

void camproto_reset(void);                          // new connection: forget which values have arrived
void camproto_decode(const uint8_t *p, int len);    // one notification's worth of packets
void camproto_decode_timecode(const uint8_t *p, int len);  // Timecode characteristic payload
void camproto_set_link(cam_link_t link, bool need_pin);
void camproto_get(cam_state_t *out);

// Build one "assign" packet. Returns its length (padded to 4), or 0 on bad input.
int camproto_build(int cat, int par, int type, const int32_t *vals, int n, uint8_t out[32]);

// Packet for a one-step change from the touchscreen. Returns 0 when nothing should be
// sent (not live, no data yet, recording, or already at the limit).
int camproto_build_step(cam_param_t p, int dir, uint8_t out[32]);

// Exact shutter: angle x100 (1..36000), or 1/x speed converted to the angle for the
// current frame rate. Return 0 when not allowed (not live, recording, out of range).
int camproto_build_shutter_angle(int angle_x100, uint8_t out[32]);
int camproto_build_shutter_speed(int denom, uint8_t out[32]);

// Exact recording frame rate (1..120, whole frames). The project rate turns off-speed off;
// any other rate turns it on. The project rate itself is never changed.
int camproto_build_fps(int fps, uint8_t out[32]);

// Exact EI, and exact white balance + tint. Return 0 when not allowed (not live, recording).
int camproto_build_iso(int iso, uint8_t out[32]);
int camproto_build_wb(int kelvin, int tint, uint8_t out[32]);

// Shutter speed (1/x) the given angle gives at the current recording frame rate.
int camproto_speed_for_angle(const cam_state_t *st, int angle_x100);

// Packet to start / stop recording. Returns 0 when not live. Starting clears the playback speed
// and loop flags, so it rolls a clip straight from playback.
int camproto_build_record(bool start, uint8_t out[32]);

// Playback. Transport: mode 0 preview / 1 play, speed (0 pause, 1 normal, >1 faster, <0 backwards),
// loop = transport flag bits: 1 loop, 2 play all. Clip step: previous / next clip.
// Return 0 when not live or recording.
int camproto_build_transport(int mode, int speed, int loop, uint8_t out[32]);
int camproto_build_clip_step(bool next, uint8_t out[32]);
