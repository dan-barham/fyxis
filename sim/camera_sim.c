// Mac simulator transport: implements camera.h using the shared protocol code.
//
//   live mode : a Python helper (ble_bridge.py) talks Bluetooth to the camera.
//               It sends us "LINK <state>" / "RX <hex>" / "LOG <text>" lines on its stdout,
//               and we send it "TX <hex>" lines on its stdin.
//   demo mode : no camera. Changes you make loop straight back as if the camera
//               accepted them, and terminal commands fake recording, link loss, etc.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
#include "../main/camproto.h"
#include "../main/camera.h"

extern char **environ;

bool g_demo;
bool g_headless;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *s_to_helper;
static pid_t s_helper_pid;

void camproto_lock(void)   { pthread_mutex_lock(&s_mu); }
void camproto_unlock(void) { pthread_mutex_unlock(&s_mu); }

static void send_to_camera(const uint8_t *b, int len);
static volatile bool s_demo_rec;

static int hex_to_bytes(const char *h, uint8_t *out, int cap)
{
    int n = 0;
    while (h[0] && h[1] && n < cap) {
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}

static void loop_back(int cat, int par, int type, const int32_t *v, int n)
{
    uint8_t b[32];
    int len = camproto_build(cat, par, type, v, n, b);
    if (len) camproto_decode(b, len);
}

// ---------- live: helper process ----------
static void *reader(void *arg)
{
    FILE *in = arg;
    char line[1024];
    while (fgets(line, sizeof line, in)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "RX ", 3)) {
            uint8_t buf[512];
            camproto_decode(buf, hex_to_bytes(line + 3, buf, sizeof buf));
            static int last_mv = -1;                 // log battery changes (0.1 V or more)
            cam_state_t st;
            camproto_get(&st);
            if (st.batt_mv > 0 && (last_mv < 0 || abs(st.batt_mv - last_mv) >= 100)) {
                printf("[batt] %.2f V   (other fields: %d, flags 0x%x)\n", st.batt_mv / 1000.0, st.batt_pct, st.batt_flags);
                fflush(stdout);
                last_mv = st.batt_mv;
            }
            static char last_tr[96];                 // log transport changes (playback testing)
            char tr[96];
            snprintf(tr, sizeof tr, "mode %d (%s), speed %d, flags 0x%02x", st.transport_mode,
                     st.transport_mode == 2 ? "recording" : st.transport_mode == 1 ? "playback" : "preview",
                     st.transport_speed, st.transport_flags);
            if (strcmp(tr, last_tr)) {
                printf("[transport] %s   tc %02d:%02d:%02d:%02d\n", tr, st.tc_h, st.tc_m, st.tc_s, st.tc_f);
                fflush(stdout);
                strcpy(last_tr, tr);
            }
            static char last_media[96];              // log card-slot changes (to check empty / full)
            char m[96];
            snprintf(m, sizeof m, "slots %d: %d / %d s, flags 0x%02x, card in %d/%d, medium %d / %d", st.slot_n,
                     st.slot_s[0], st.slot_s[1], st.transport_flags, st.slot_present[0], st.slot_present[1],
                     st.slot_type[0], st.slot_type[1]);
            if (st.slot_n && strcmp(m, last_media)) {
                printf("[media] %s\n", m);
                fflush(stdout);
                strcpy(last_media, m);
            }
        } else if (!strncmp(line, "TC ", 3)) {
            static int shown;
            uint8_t buf[64];
            int n = hex_to_bytes(line + 3, buf, sizeof buf);
            camproto_decode_timecode(buf, n);
            if (shown < 5) {                         // so the decoding can be checked against the camera
                cam_state_t st;
                camproto_get(&st);
                printf("[tc] raw %s -> %02d:%02d:%02d:%02d%s\n", line + 3, st.tc_h, st.tc_m, st.tc_s, st.tc_f,
                       st.tc_valid ? "" : " (not decoded)");
                if (++shown == 5) printf("[tc] (further timecode lines hidden)\n");
            }
        } else if (!strncmp(line, "LINK ", 5)) {
            const char *s = line + 5;
            if (!strcmp(s, "online"))          camproto_set_link(LINK_ONLINE, false);
            else if (!strcmp(s, "connecting")) camproto_set_link(LINK_CONNECTING, false);
            else if (!strcmp(s, "pairing"))    camproto_set_link(LINK_PAIRING, false);  // PIN goes in the macOS dialog
            else                               camproto_set_link(LINK_SEARCHING, false);
            printf("[link] %s\n", s);
        } else {
            printf("[bt] %s\n", !strncmp(line, "LOG ", 4) ? line + 4 : line);
        }
        fflush(stdout);
    }
    printf("[sim] Bluetooth helper exited\n");
    camproto_set_link(LINK_SEARCHING, false);
    return NULL;
}

static void stop_helper(void) { if (s_helper_pid > 0) kill(s_helper_pid, SIGTERM); }

// ---------- live: playback test keys ----------
// Checks which of Blackmagic's playback commands the camera accepts over Bluetooth. Type a key and
// Return; the [transport] lines show what the camera reports back.
static void live_help(void)
{
    printf("\nPlayback test keys (type a key, then Return):\n"
           "  p  play 1x            s  pause              x  stop playback (preview)\n"
           "  n  next clip          v  previous clip\n"
           "  S <n>  play at speed n (e.g. S 4, S -8)\n"
           "  L <n>  play 1x with loop flags n: 0 off, 1 loop, 2 play all, 3 loop + play all\n"
           "  r  REC (what the REC button sends: should roll a clip, even from playback)\n"
           "  t  show transport state and timecode      h  this help\n\n");
    fflush(stdout);
}

static void live_send(const char *what, const uint8_t *b, int len)
{
    printf("[test] %s: ", what);
    if (!len) { printf("nothing sent (not online, or recording)\n"); fflush(stdout); return; }
    for (int i = 0; i < len; i++) printf("%02x", b[i]);
    printf("\n");
    fflush(stdout);
    send_to_camera(b, len);
}

static void *live_console(void *arg)
{
    (void)arg;
    char line[64];
    live_help();
    while (fgets(line, sizeof line, stdin)) {
        uint8_t b[32];
        cam_state_t st;
        camproto_get(&st);
        char what[48];
        const int arg = atoi(line + 1);
        switch (line[0]) {
        case 'p': live_send("play 1x", b, camproto_build_transport(1, 1, 0, b)); break;
        case 's': live_send("pause", b, camproto_build_transport(1, 0, 0, b)); break;
        case 'x': live_send("stop playback", b, camproto_build_transport(0, 0, 0, b)); break;
        case 'n': live_send("next clip", b, camproto_build_clip_step(true, b)); break;
        case 'v': live_send("previous clip", b, camproto_build_clip_step(false, b)); break;
        case 'S': snprintf(what, sizeof what, "speed %d", arg);
                  live_send(what, b, camproto_build_transport(1, arg, 0, b)); break;
        case 'L': snprintf(what, sizeof what, "play with flags %d", arg);
                  live_send(what, b, camproto_build_transport(1, 1, arg, b)); break;
        case 'r': camera_record(true); break;
        case 't':
            printf("[test] mode %d, speed %d, flags 0x%02x, recording %d, tc %02d:%02d:%02d:%02d\n", st.transport_mode,
                   st.transport_speed, st.transport_flags, st.recording, st.tc_h, st.tc_m, st.tc_s, st.tc_f);
            fflush(stdout);
            break;
        case 'h': live_help(); break;
        }
    }
    return NULL;
}

static void start_helper(void)
{
    const char *cmd = getenv("BIGSIX_BRIDGE");
    if (!cmd) cmd = "python3 -u ble_bridge.py";
    int to_child[2], from_child[2];
    if (pipe(to_child) || pipe(from_child)) { perror("pipe"); exit(1); }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, to_child[0], 0);
    posix_spawn_file_actions_adddup2(&fa, from_child[1], 1);
    posix_spawn_file_actions_addclose(&fa, to_child[1]);
    posix_spawn_file_actions_addclose(&fa, from_child[0]);
    char *argv[] = { "/bin/sh", "-c", (char *)cmd, NULL };
    if (posix_spawn(&s_helper_pid, "/bin/sh", &fa, NULL, argv, environ) != 0) { perror("spawn"); exit(1); }
    posix_spawn_file_actions_destroy(&fa);
    close(to_child[0]);
    close(from_child[1]);
    atexit(stop_helper);

    s_to_helper = fdopen(to_child[1], "w");
    pthread_t t;
    pthread_create(&t, NULL, reader, fdopen(from_child[0], "r"));
    pthread_detach(t);
}

// ---------- demo: terminal commands ----------
static volatile int s_demo_mv = 15300;   // demo battery voltage, mV
static volatile int s_demo_card2 = 3725; // demo slot 2 remaining, s (0 = empty, as the PYXIS sends it)
static volatile int s_demo_card1 = 1;    // card 1 inserted

static void demo_help(void)
{
    printf("\nDemo commands (type a letter, then Return):\n"
           "  r  toggle recording          o  toggle off-speed (48 fps)\n"
           "  f  cycle project rate\n"
           "  x  cycle sensor area\n"
           "  l  toggle link lost          p  show the PIN keypad\n"
           "  c  card 2: 1h 02m / empty     e  eject / insert card 1\n"
           "  b  battery: 15.3 / 14.2 / 13.6 / 13.0 / 12.7 V (it also drains slowly by itself)\n"
           "  h  this help\n\n");
    fflush(stdout);
}

static int s_rate, s_res;
static bool s_lost;

// One demo command (from the terminal, or from a headless script).
void sim_demo_cmd(char cmd)
{
    static const int RATES[][2] = { {24, 1}, {24, 0}, {25, 0}, {30, 1}, {30, 0}, {60, 1} };
    static const int RES[][2] = { {8192, 4608}, {12288, 6480}, {4096, 2160}, {12288, 8040} };
    cam_state_t st;
    camproto_get(&st);
    switch (cmd) {
    case 'r': { bool rec = !st.recording; s_demo_rec = rec; int32_t v[5] = { rec ? 2 : 0, 0, s_demo_card1 ? 0x20 : 0, 4, 0 }; loop_back(10, 1, 1, v, 5);
                printf("recording %s\n", rec ? "ON" : "off"); break; }
    case 'o': { int fl = (st.project_mrate ? 1 : 0) | (st.offspeed ? 0 : 4);
                int32_t v[5] = { st.project_fps, st.offspeed ? st.project_fps : 48, st.res_w, st.res_h, fl };
                loop_back(1, 9, 2, v, 5); printf("off-speed %s\n", st.offspeed ? "off" : "ON"); break; }
    case 'f': { s_rate = (s_rate + 1) % 6;
                int32_t v[5] = { RATES[s_rate][0], RATES[s_rate][0], st.res_w, st.res_h, RATES[s_rate][1] ? 3 : 0 };
                loop_back(1, 9, 2, v, 5); break; }
    case 'x': { s_res = (s_res + 1) % 4;
                int fl = (st.project_mrate ? 1 : 0) | (st.offspeed_mrate ? 2 : 0) | (st.offspeed ? 4 : 0);
                int32_t v[5] = { st.project_fps, st.offspeed_fps, RES[s_res][0], RES[s_res][1], fl };
                loop_back(1, 9, 2, v, 5); break; }
    case 'l': s_lost = !s_lost; camproto_set_link(s_lost ? LINK_SEARCHING : LINK_ONLINE, false);
              printf("link %s\n", s_lost ? "LOST" : "back"); break;
    case 'p': camproto_set_link(LINK_PAIRING, true); printf("PIN keypad shown; any 6 digits + OK\n"); break;
    case 'c': s_demo_card2 = s_demo_card2 ? 0 : 3725;            // the PYXIS sends 0 for an empty slot
              printf("card 2 %s\n", s_demo_card2 ? "in (1h 02m)" : "empty"); break;
    case 'e': { s_demo_card1 = !s_demo_card1;                   // eject: flag clears, time isn't resent
                int32_t v[5] = { st.recording ? 2 : 0, 0, s_demo_card1 ? 0x20 : 0, 4, 0 };
                loop_back(10, 1, 1, v, 5); printf("card 1 %s\n", s_demo_card1 ? "inserted" : "ejected"); break; }
    case 'b': { static const int B[] = { 15300, 14200, 13600, 13000, 12700 }; static int bi;
                bi = (bi + 1) % 5; s_demo_mv = B[bi]; printf("battery %.1f V\n", B[bi] / 1000.0); break; }
    case 'h': demo_help(); break;
    }
    fflush(stdout);
}

static void *demo_console(void *arg)
{
    char line[64];
    demo_help();
    while (fgets(line, sizeof line, stdin)) sim_demo_cmd(line[0]);
    return NULL;
}

// Demo playback: two clips on a timeline (24 fps), played at the requested speed, stopping at
// either end of the card; clip steps go to the start of the previous / next clip, paused. Modelled
// on what the PYXIS did in the Bluetooth tests.
static const struct { int len, tc; } DEMO_CLIPS[] = { { 20 * 24, 10 * 3600 * 24 }, { 15 * 24, (10 * 3600 + 300) * 24 } };
#define DEMO_NCLIPS 2
static volatile int s_pb_pos = -1;          // frame on the timeline (-1 = not in playback yet)
static volatile bool s_pb_all;              // play-all flag as last sent: on through every clip

static int demo_clip_of(int pos) { return pos < DEMO_CLIPS[0].len ? 0 : 1; }
static int demo_clip_start(int c) { return c ? DEMO_CLIPS[0].len : 0; }
static int demo_total(void) { return DEMO_CLIPS[0].len + DEMO_CLIPS[1].len; }

static void demo_report(int speed)
{
    cam_state_t st;
    camproto_get(&st);
    int32_t v[5] = { st.transport_mode, speed, (st.transport_flags & ~3) | (speed ? 2 : 0), 4, 0 };
    loop_back(10, 1, 1, v, 5);
}

static void demo_pb_tc(uint8_t tc[4])
{
    const int c = demo_clip_of(s_pb_pos);
    const int f = DEMO_CLIPS[c].tc + (s_pb_pos - demo_clip_start(c));
    const int fr = f % 24, sec = f / 24 % 60, m = f / 1440 % 60, h = f / 86400 % 24;
    tc[0] = (uint8_t)((fr / 10) << 4 | fr % 10); tc[1] = (uint8_t)((sec / 10) << 4 | sec % 10);
    tc[2] = (uint8_t)((m / 10) << 4 | m % 10);   tc[3] = (uint8_t)((h / 10) << 4 | h % 10);
}

// A packet the screen sent: playback needs more than a straight loop-back.
static bool demo_playback_packet(const uint8_t *b, int len)
{
    if (len < 9 || b[4] != 10) return false;
    if (b[5] == 2) {                                      // clip step
        const int c = demo_clip_of(s_pb_pos < 0 ? 0 : s_pb_pos);
        const int to = b[8] ? (c + 1 < DEMO_NCLIPS ? c + 1 : c) : (c > 0 ? c - 1 : c);
        if (to != c || !b[8]) s_pb_pos = demo_clip_start(to);     // previous on the first clip: its start
        demo_report(0);
        printf("[demo] %s clip -> clip %d\n", b[8] ? "next" : "previous", to + 1);
        return true;
    }
    if (b[5] == 1 && b[8] == 1 && s_pb_pos < 0) s_pb_pos = demo_clip_start(DEMO_NCLIPS - 1);   // opens on the last clip
    if (b[5] == 1 && len >= 11) s_pb_all = (b[10] & 3) != 0;
    if (b[5] == 1 && b[8] != 1) s_pb_pos = -1;
    return false;
}

static void *demo_clock(void *arg)
{
    int drain = 0;
    int h = 1, m = 0, sec = 0, f = 0, media = 1429, tick = 0;
    for (;;) {
        usleep(1000000 / 24);
        if (++f == 24) { f = 0; if (++sec == 60) { sec = 0; if (++m == 60) { m = 0; h = (h + 1) % 24; } } }
        uint8_t tc[4] = { (uint8_t)((f / 10) << 4 | f % 10), (uint8_t)((sec / 10) << 4 | sec % 10),
                          (uint8_t)((m / 10) << 4 | m % 10), (uint8_t)((h / 10) << 4 | h % 10) };
        cam_state_t pst;
        camproto_get(&pst);
        if (pst.transport_mode == 1 && s_pb_pos >= 0) {            // playback: the clip's timecode
            int np = s_pb_pos + pst.transport_speed;
            // Stop at an end of the card, or (without play-all) at an end of the clip.
            const int c = demo_clip_of(s_pb_pos);
            const int lo = s_pb_all ? 0 : demo_clip_start(c);
            const int hi = s_pb_all ? demo_total() - 1 : demo_clip_start(c) + DEMO_CLIPS[c].len - 1;
            if (np < lo || np > hi) {
                np = np < lo ? lo : hi;
                if (pst.transport_speed) demo_report(0);
            }
            s_pb_pos = np;
            demo_pb_tc(tc);
        }
        camproto_decode_timecode(tc, 4);
        if (++tick % 24 == 0) {
            if (s_demo_rec && media > 0 && s_demo_card1) media--;
            int32_t v[2] = { media, s_demo_card2 };
            loop_back(9, 2, 2, v, 2);
            if (++drain % 2 == 0 && s_demo_mv > 12000) s_demo_mv -= 2 + (s_demo_mv < 13600 ? 3 : 0);  // faster past the knee
            int32_t b[3] = { s_demo_mv + (rand() % 61) - 30, 0, 0 };     // a little load noise
            loop_back(9, 0, 2, b, 3);
        }
    }
    return NULL;
}

static void demo_start(void)
{
    int32_t iso = 800, ang = 18000, wb[2] = { 5600, 0 }, fmt[5] = { 24, 24, 8192, 4608, 3 }, tr[5] = { 0, 0, 0x20, 4, 0 };
    loop_back(1, 14, 3, &iso, 1);
    loop_back(1, 11, 3, &ang, 1);
    loop_back(1, 2, 2, wb, 2);
    loop_back(1, 9, 2, fmt, 5);
    loop_back(10, 1, 1, tr, 5);
    int32_t av = 3 * 2048, mm = 35;                  // lens: f/2.8 (aperture value 3), 35 mm
    loop_back(0, 2, 128, &av, 1);
    loop_back(0, 7, 2, &mm, 1);
    static const char lens[] = "Zeiss CP.3 35mm T2.1";   // 12.10 lens type (string)
    uint8_t pk[64] = { 255, (uint8_t)(4 + sizeof lens - 1), 0, 0, 12, 10, 5, 0 };
    memcpy(pk + 8, lens, sizeof lens - 1);
    camproto_decode(pk, 8 + sizeof lens - 1 + 3);
    camproto_set_link(LINK_ONLINE, false);
    pthread_t t;
    if (!g_headless) {
        pthread_create(&t, NULL, demo_console, NULL);
        pthread_detach(t);
    }
    pthread_create(&t, NULL, demo_clock, NULL);
    pthread_detach(t);
}

// ---------- camera.h ----------
void camera_start(void)
{
    camproto_set_link(LINK_SEARCHING, false);
    if (g_demo) demo_start();
    else {
        start_helper();
        pthread_t t;
        pthread_create(&t, NULL, live_console, NULL);
        pthread_detach(t);
    }
}

void camera_get(cam_state_t *out) { camproto_get(out); }

void camera_set_low_power(bool on) { printf("[sim] Bluetooth scan %s\n", on ? "slow" : "normal"); fflush(stdout); }

void camera_step(cam_param_t p, int dir)
{
    static const char *NAMES[] = { "EI", "SHUTTER ANGLE", "SHUTTER SPEED", "WB", "TINT", "FPS" };
    uint8_t b[32];
    int len = camproto_build_step(p, dir, b);
    if (!len) {
        cam_state_t st;
        camproto_get(&st);
        printf("[ui] %s %s pressed: nothing sent (live=%d, values=%d, recording=%d, off-speed=%d)\n",
               NAMES[p], dir > 0 ? "+" : "-", st.connected, st.have_data, st.recording, st.offspeed);
        fflush(stdout);
        return;
    }
    printf("[ui] %s %s pressed: sending ", NAMES[p], dir > 0 ? "+" : "-");
    for (int i = 0; i < len; i++) printf("%02x", b[i]);
    printf("\n");
    fflush(stdout);
    send_to_camera(b, len);
}

static void send_to_camera(const uint8_t *b, int len)
{
    if (g_demo) {
        if (!demo_playback_packet(b, len)) camproto_decode(b, len);   // the "camera" accepts it as-is
        return;
    }
    fprintf(s_to_helper, "TX ");
    for (int i = 0; i < len; i++) fprintf(s_to_helper, "%02x", b[i]);
    fprintf(s_to_helper, "\n");
    fflush(s_to_helper);
}

void camera_record(bool start)
{
    uint8_t b[32];
    int len = camproto_build_record(start, b);
    printf("[ui] record %s: %s\n", start ? "START" : "STOP", len ? "sending" : "nothing sent (not live)");
    fflush(stdout);
    if (!len) return;
    s_demo_rec = start;
    send_to_camera(b, len);
}

static void send_logged(const char *what, const uint8_t *b, int len)
{
    printf("[ui] %s: %s\n", what, len ? "sending" : "nothing sent (not live, recording, or out of range)");
    fflush(stdout);
    if (len) send_to_camera(b, len);
}

void camera_playback(int mode, int speed, int flags)
{
    uint8_t b[32];
    char what[48];
    snprintf(what, sizeof what, "playback mode %d speed %d%s", mode, speed, flags & 2 ? " (all clips)" : "");
    send_logged(what, b, camproto_build_transport(mode, speed, flags, b));
}

void camera_playback_clip(int steps)
{
    uint8_t b[32];
    send_logged(steps > 0 ? "next clip" : "previous clip", b, camproto_build_clip_step(steps > 0, b));
}

void camera_set_shutter_angle(int angle_x100)
{
    uint8_t b[32];
    char what[48];
    snprintf(what, sizeof what, "custom shutter angle %.2f", angle_x100 / 100.0);
    send_logged(what, b, camproto_build_shutter_angle(angle_x100, b));
}

void camera_set_shutter_speed(int denom)
{
    uint8_t b[32];
    char what[48];
    snprintf(what, sizeof what, "custom shutter speed 1/%d", denom);
    send_logged(what, b, camproto_build_shutter_speed(denom, b));
}

void camera_set_fps(int fps)
{
    uint8_t b[32];
    char what[48];
    snprintf(what, sizeof what, "recording frame rate %d", fps);
    send_logged(what, b, camproto_build_fps(fps, b));
}

void camera_enter_pin(uint32_t pin)
{
    printf("[sim] PIN entered: %06u\n", (unsigned)pin);
    fflush(stdout);
    camproto_set_link(LINK_ONLINE, false);
}

void camera_set_iso(int iso)
{
    uint8_t b[32];
    char what[32];
    snprintf(what, sizeof what, "EI %d", iso);
    send_logged(what, b, camproto_build_iso(iso, b));
}

void camera_set_wb(int kelvin, int tint)
{
    uint8_t b[32];
    char what[40];
    snprintf(what, sizeof what, "white balance %dK tint %d", kelvin, tint);
    send_logged(what, b, camproto_build_wb(kelvin, tint, b));
}

void camera_get_diag(cam_diag_t *out)
{
    cam_state_t st;
    camproto_get(&st);
    *out = (cam_diag_t){ .conn_itvl_us = st.connected ? 7500 : 0, .mtu = 247, .data_len = 251,
                         .rssi = st.connected ? -48 : 0, .tc_per_s = 24, .rx_per_s = 1, .bonded = true };
    snprintf(out->name, sizeof out->name, "%s", g_demo ? "Pyxis 12K (demo)" : "");
}

void camera_forget(void)
{
    printf("[sim] 'forget camera' pressed (on the board this clears the stored pairing)\n");
    fflush(stdout);
}
