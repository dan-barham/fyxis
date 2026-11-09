// Mac simulator: runs the real Big Six UI (../main/bigsix.c) in an 800 x 480 window.
//   ./bigsix_sim          talk to the camera over Bluetooth (both ways)
//   ./bigsix_sim --demo   no camera; fake values, terminal commands for edge cases
//   ./bigsix_sim --demo --shots DIR "SCRIPT"
//                         no window: runs SCRIPT and writes screenshots (BMP) into DIR, then exits.
//                         SCRIPT is ';'-separated: "w MS" wait, "t X Y" tap, "d X Y" / "u" press /
//                         release, "k" REC button press, "K MS" REC held for MS, "c L" demo command
//                         letter (see --demo help), "s NAME" screenshot to DIR/NAME.bmp.
// Mouse = finger. Space bar = the physical REC button.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lvgl.h"
#include "../main/bigsix.h"
#include "../main/recbutton.h"
#include "../main/settings.h"
#include "SDL.h"

extern bool g_demo, g_headless;
void sim_demo_cmd(char cmd);
void recbutton_sim_poll(void);

#define W 800
#define H 480

static uint32_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Space bar stands in for the physical REC button.
static int key_watch(void *ud, SDL_Event *e)
{
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 0;
    if (e->key.keysym.sym != SDLK_SPACE || e->key.repeat) return 0;
    if (e->type == SDL_KEYDOWN) recbutton_down();
    else if (e->type == SDL_KEYUP) recbutton_up();
    return 0;
}

// ---------- headless ----------
static uint16_t s_fb[W * H];
static lv_point_t s_pt;
static bool s_pressed;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;                 // direct mode: the draw buffer is s_fb itself
    lv_display_flush_ready(d);
}

static void ptr_read(lv_indev_t *in, lv_indev_data_t *data)
{
    (void)in;
    data->point = s_pt;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Frame recording: while a "v" command runs, every few ms a numbered frame is written.
static char s_vid_prefix[512];
static int s_vid_n, s_vid_ms;
static uint32_t s_vid_next;
static bool s_show_touch;                   // draw a translucent finger mark where the pointer is pressed

static void write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    const int row = W * 3, size = 54 + row * H;
    uint8_t hdr[54] = { 'B', 'M' };
    *(uint32_t *)(hdr + 2) = size;
    *(uint32_t *)(hdr + 10) = 54;
    *(uint32_t *)(hdr + 14) = 40;
    *(int32_t *)(hdr + 18) = W;
    *(int32_t *)(hdr + 22) = -H;       // top-down
    *(uint16_t *)(hdr + 26) = 1;
    *(uint16_t *)(hdr + 28) = 24;
    *(uint32_t *)(hdr + 34) = row * H;
    fwrite(hdr, 1, 54, f);
    static uint8_t line[W * 3];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint16_t c = s_fb[y * W + x];
            const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            int R = (r << 3) | (r >> 2), G = (g << 2) | (g >> 4), B = (b << 3) | (b >> 2);
            if (s_show_touch && s_pressed) {         // finger mark: soft white disc
                const int dx = x - s_pt.x, dy = y - s_pt.y, d2 = dx * dx + dy * dy;
                if (d2 < 26 * 26) {
                    const int a = d2 > 22 * 22 ? 150 : 90;
                    R += (255 - R) * a / 255; G += (255 - G) * a / 255; B += (255 - B) * a / 255;
                }
            }
            line[x * 3 + 0] = B;
            line[x * 3 + 1] = G;
            line[x * 3 + 2] = R;
        }
        fwrite(line, 1, row, f);
    }
    fclose(f);
    if (!s_vid_prefix[0]) { printf("[shot] %s\n", path); fflush(stdout); }
}

static void run_for(uint32_t ms)
{
    const uint32_t end = ms_now() + ms;
    while ((int32_t)(ms_now() - end) < 0) {
        lv_timer_handler();
        recbutton_sim_poll();
        if (s_vid_prefix[0] && (int32_t)(ms_now() - s_vid_next) >= 0) {
            s_vid_next += s_vid_ms;
            char path[600];
            snprintf(path, sizeof path, "%s_%05d.bmp", s_vid_prefix, s_vid_n++);
            lv_refr_now(NULL);
            write_bmp(path);
        }
        usleep(2000);
    }
}

static int headless(const char *dir, const char *script)
{
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, ptr_read);

    bigsix_start();
    run_for(300);

    char *buf = strdup(script), *save = NULL;
    for (char *cmd = strtok_r(buf, ";", &save); cmd; cmd = strtok_r(NULL, ";", &save)) {
        while (*cmd == ' ') cmd++;
        int a = 0, b = 0;
        char name[128];
        switch (cmd[0]) {
        case 'w': run_for(atoi(cmd + 1)); break;
        case 't': sscanf(cmd + 1, "%d %d", &a, &b); s_pt.x = a; s_pt.y = b; s_pressed = true; run_for(90);
                  s_pressed = false; run_for(150); break;
        case 'd': sscanf(cmd + 1, "%d %d", &a, &b); s_pt.x = a; s_pt.y = b; s_pressed = true; run_for(40); break;
        case 'm': sscanf(cmd + 1, "%d %d", &a, &b); s_pt.x = a; s_pt.y = b; run_for(40); break;
        case 'u': s_pressed = false; run_for(150); break;
        case 'k': recbutton_down(); run_for(60); recbutton_up(); run_for(60); break;
        case 'K': recbutton_down(); run_for(atoi(cmd + 1)); recbutton_up(); run_for(60); break;
        case 'c': { const char *p = cmd + 1; while (*p == ' ') p++; sim_demo_cmd(*p); run_for(100); break; }
        case 'V': sscanf(cmd + 1, "%127s %d", name, &a);          // start recording frames every A ms
                  snprintf(s_vid_prefix, sizeof s_vid_prefix, "%s/%s", dir, name);
                  s_vid_n = 0; s_vid_ms = a > 0 ? a : 40; s_vid_next = ms_now(); s_show_touch = true; break;
        case 'E': s_vid_prefix[0] = 0; s_show_touch = false; break; // stop recording
        case 's': sscanf(cmd + 1, "%127s", name);
                  { char path[512]; snprintf(path, sizeof path, "%s/%s.bmp", dir, name); lv_refr_now(disp); write_bmp(path); }
                  break;
        }
    }
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    const char *shots = NULL, *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) g_demo = true;
        else if (!strcmp(argv[i], "--shots") && i + 2 < argc) { shots = argv[i + 1]; script = argv[i + 2]; i += 2; }
    }
    g_headless = shots != NULL;
    settings_init();
    lv_init();
    lv_tick_set_cb(ms_now);
    if (shots) return headless(shots, script);

    printf("PYXIS Big Six simulator — %s mode\n", g_demo ? "demo" : "live camera");
    if (!g_demo) printf("Turn on Bluetooth in the camera's Setup menu. Editing sends real changes to the camera.\n");
    fflush(stdout);

    lv_display_t *disp = lv_sdl_window_create(W, H);
    lv_sdl_window_set_title(disp, g_demo ? "PYXIS Big Six (demo)" : "PYXIS Big Six");
    lv_sdl_mouse_create();

    bigsix_start();
    SDL_AddEventWatch(key_watch, NULL);
    printf("Space bar = the physical REC button.\n");
    fflush(stdout);

    for (;;) {
        uint32_t wait = lv_timer_handler();
        recbutton_sim_poll();
        usleep((wait > 5 ? 5 : wait) * 1000);
    }
}
