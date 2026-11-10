// Big Six side display for the PYXIS 12K — LVGL v9 UI.
//
// Layout:   FPS                | SHUTTER         | EI
//           WB + TINT          | TIMECODE (two columns wide)
//                              | status · cards  |
//           presets · settings | slide to lock   | battery
//
// Behaviour
//  - Editing is unlocked at start-up (or locked: Settings > Display). Slide the knob under the
//    timecode right to lock (it turns amber), left to unlock again. Tap a tile to open its
//    editor; tapping a tile while locked nudges the knob as a hint.
//  - Editors show the value very large with round − / + buttons (hold to repeat) and Done.
//    White balance and tint share one editor. The shutter editor has an ANGLE | SPEED switch
//    and Custom, a keypad for an exact angle or 1/x speed. The FPS editor has quick off-speed
//    chips (OFF = back to the project rate) and Custom.
//  - REC is the physical button (recbutton.c). While recording: a red frame drawn over
//    everything (nothing moves), editing blocked, and the status line runs a clip timer. The
//    frame appears dimmed the moment the button is pressed and goes solid when the camera
//    confirms, so a press always shows at once.
//  - When not live, the status line shows the link state in amber. If the link drops, the last
//    values stay in grey and the tally is suppressed.
//  - First connection: the camera shows a 6-digit PIN; a keypad appears here to enter it.
//  - Bottom left: Presets (bookmarks icon) and Settings (gear icon). The REC button always rolls
//    a new clip, even if the camera is playing one back.
//  - Exposure lock (Settings > Camera): a frame-rate change also moves the shutter angle, so the
//    shutter speed, and so the brightness, stays the same. The shutter tile shows LOCKED.
//  - Sleep: with no camera for a minute (Settings > Display), the screen goes black and its
//    backlight off, and Bluetooth scans slowly. The camera, a touch or the REC button wakes it.
//
// Drawing: every widget is only touched when its text or colour actually changes, so a
// timecode tick redraws two or three digits and nothing else. State is read every 10 ms.
//
// Typeface: Inter (SIL Open Font License), rendered at 14 / 20 / 26 / 36 / 64 / 132 px.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "camera.h"
#include "camproto.h"
#include "bigsix.h"
#include "recbutton.h"
#include "settings.h"
#include "update.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

LV_FONT_DECLARE(font_label_14);
LV_FONT_DECLARE(font_label_20);
LV_FONT_DECLARE(font_mid_36);
LV_FONT_DECLARE(font_value_64);
LV_FONT_DECLARE(font_big_132);
// Icons: Tabler Icons, outline (MIT licence, licenses/Tabler-Icons-MIT.txt), as fonts.
LV_FONT_DECLARE(font_icons_22);   // battery levels, memory card, settings, bluetooth
LV_FONT_DECLARE(font_icons_28);   // lock, lock-open, backspace, bookmarks, settings
LV_FONT_DECLARE(font_icons_48);   // minus, plus

#define ICON_BATT_0     "\xEE\xA8\xB4"   // battery              U+EA34
#define ICON_BATT_1     "\xEE\xA8\xAF"   // battery-1            U+EA2F
#define ICON_BATT_2     "\xEE\xA8\xB0"   // battery-2            U+EA30
#define ICON_BATT_3     "\xEE\xA8\xB1"   // battery-3            U+EA31
#define ICON_BATT_4     "\xEE\xA8\xB2"   // battery-4            U+EA32
#define ICON_BATT_CRIT  "\xEF\xBC\x9D"   // battery-exclamation  U+FF1D
#define ICON_CARD       "\xEF\x8E\x84"   // device-sd-card       U+F384
#define ICON_GEAR       "\xEE\xAC\xA0"   // settings             U+EB20
#define ICON_LOCK       "\xEE\xAB\xA2"   // lock                 U+EAE2
#define ICON_LOCK_OPEN  "\xEE\xAB\xA1"   // lock-open            U+EAE1
#define ICON_BACKSPACE  "\xEE\xA8\xAD"   // backspace            U+EA2D
#define ICON_MINUS      "\xEE\xAB\xB2"   // minus                U+EAF2
#define ICON_PLUS       "\xEE\xAC\x8B"   // plus                 U+EB0B
#define ICON_PRESETS    "\xEE\xB4\x88"   // bookmarks            U+ED08

// ---------- look ----------
// Apple dark-mode palette, and an all-red night palette (Settings > Display). Greys are picked so
// they stay neutral on the board's 16-bit (RGB565) panel, which otherwise tints them green.
typedef struct { uint32_t bg, tile, tile_pr, btn, btn_pr, seg_on, text, text2, stale, orange, blue, red, rule; } pal_t;
static const pal_t PAL_DAY = {
    .bg = 0x000000, .tile = 0x181818, .tile_pr = 0x282828, .btn = 0x282828, .btn_pr = 0x484848,
    .seg_on = 0x606060, .text = 0xFFFFFF, .text2 = 0x8A8A8A, .stale = 0x484848, .orange = 0xFF9F0A,
    .blue = 0x0A84FF, .red = 0xFF453A, .rule = 0x303030,
};
// Night: red only, dimmer, so it keeps your eyes adapted and throws little light on set.
// Warnings stay distinct by brightness and a warmer orange-red.
static const pal_t PAL_NIGHT = {
    .bg = 0x000000, .tile = 0x140000, .tile_pr = 0x2A0404, .btn = 0x240303, .btn_pr = 0x4A0808,
    .seg_on = 0x5A0A0A, .text = 0xE02010, .text2 = 0x801008, .stale = 0x400804, .orange = 0xFF6A10,
    .blue = 0x7A0A06, .red = 0xFF2000, .rule = 0x2A0404,
};
static const pal_t *s_pal = &PAL_DAY;

#define C_BG       lv_color_hex(s_pal->bg)
#define C_TILE     lv_color_hex(s_pal->tile)
#define C_TILE_PR  lv_color_hex(s_pal->tile_pr)
#define C_BTN      lv_color_hex(s_pal->btn)
#define C_BTN_PR   lv_color_hex(s_pal->btn_pr)
#define C_SEG_ON   lv_color_hex(s_pal->seg_on)
#define C_TEXT     lv_color_hex(s_pal->text)
#define C_TEXT2    lv_color_hex(s_pal->text2)
#define C_STALE    lv_color_hex(s_pal->stale)
#define C_ORANGE   lv_color_hex(s_pal->orange)
#define C_BLUE     lv_color_hex(s_pal->blue)
#define C_RED      lv_color_hex(s_pal->red)
#define C_RULE     lv_color_hex(s_pal->rule)
#define C_TALLY    lv_color_hex(0xFF0000)     // the record frame stays full red in both palettes

// The panel's pixels aren't square: its 800 x 480 pixels cover 95.04 x 53.86 mm, so each one is
// about 6 % wider than it is tall, and a circle drawn with equal width and height looks like a
// wide oval. Round things are drawn this much narrower so they look round.
#define PX_W_PER_1000_H 944
static int rw(int h) { return (h * PX_W_PER_1000_H + 500) / 1000; }

#define MARGIN     12      // screen edge
#define GAP        12      // between tiles
#define RADIUS     22      // tile corners
#define OUTLINE    3       // tile border, kept transparent (insets the content consistently)
#define LOCK_H     72      // slide-to-lock track and battery strip height
#define TC_PAD     18      // timecode tile: side padding

#define MINUS "\xE2\x88\x92"   // U+2212
#define MIDDOT "\xC2\xB7"      // U+00B7
#define DEG "\xC2\xB0"         // U+00B0
#define ELLIPSIS "\xE2\x80\xA6"

enum { T_FPS, T_SHUT, T_EI, T_WB, T_COUNT };
static const char *TITLES[T_COUNT] = { "FPS", "SHUTTER", "EI", "WHITE BALANCE" };

// ---------- objects ----------
static lv_obj_t *s_tile[T_COUNT], *s_title[T_COUNT], *s_val[T_COUNT], *s_sub[T_COUNT];
static lv_obj_t *s_btn_pre, *s_btn_set;    // bottom left: Presets, Settings (icon labels)
static lv_obj_t *s_tc_tile, *s_tc_cell[11], *s_tc_lbl;   // timecode: one fixed-width cell per character
static lv_obj_t *s_status, *s_status_dot;               // under the timecode, left
static lv_obj_t *s_slot_icon[2], *s_slot_num[2], *s_slot_lbl[2];   // under the timecode, right
static lv_obj_t *s_tally[4];                            // record frame: top, bottom, left, right

typedef struct { lv_obj_t *box, *label, *value, *sub, *minus, *plus; cam_param_t param; } row_t;
static lv_obj_t *s_editor, *s_done, *s_custom;
static lv_obj_t *s_seg, *s_seg_btn[2], *s_seg_lbl[2];   // ANGLE | SPEED switch (shutter editor)
static bool s_shut_speed;                               // shutter editor works in 1/x speeds
static lv_obj_t *s_num, *s_num_title, *s_num_hint, *s_num_disp, *s_num_err, *s_num_dot;
static char s_num_buf[8];
typedef enum { NUM_ANGLE, NUM_SPEED, NUM_FPS } num_mode_t;
static num_mode_t s_num_mode;

// Quick off-speed rates for the FPS editor (0 = OFF: back to the project rate).
static const int CHIPS[] = { 0, 30, 36, 48, 50, 60, 72, 96, 120 };
#define NCHIPS ((int)(sizeof CHIPS / sizeof CHIPS[0]))
static lv_obj_t *s_chips, *s_chip[NCHIPS], *s_chip_lbl[NCHIPS];
static row_t s_row[2];
static int s_ed_tile = -1;

static lv_obj_t *s_pin, *s_pin_digits;
static char s_pin_buf[7];

static cam_state_t s_st;
static float s_xl_short;          // exposure lock: stops darker than the locked speed (angle at its limit)
static uint32_t s_seq_drawn = UINT32_MAX;
static bool s_unlocked = true;    // editing allowed (until the user slides it locked)

// Recording, as the UI tracks it.
static bool s_was_rec, s_saw_idle;         // for the clip timer: only if the start was seen
static uint32_t s_rec_since;
static uint32_t s_req_seen;                // last rec_req_seq acted on
static int s_pending = -1;                 // REC pressed, not confirmed yet: 1 start, 0 stop
static uint32_t s_pending_ms;
static uint32_t s_hint_seen, s_hint_until; // "Hold to stop" hint

static void force_redraw(void) { s_seq_drawn = UINT32_MAX; }

// ---------- change-only setters ----------
// LVGL redraws an object every time its text or a style is set, even to the same value, and a
// style change also re-cascades to its children. These skip the call when nothing changes.
static void set_text(lv_obj_t *l, const char *s)
{
    const char *cur = lv_label_get_text(l);
    if (cur && !strcmp(cur, s)) return;
    lv_label_set_text(l, s);
}

static void set_text_color(lv_obj_t *o, lv_color_t c)
{
    if (lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) return;
    lv_obj_set_style_text_color(o, c, 0);
}

static void set_bg(lv_obj_t *o, lv_color_t c)
{
    if (lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) return;
    lv_obj_set_style_bg_color(o, c, 0);
}

static void set_bg_opa(lv_obj_t *o, lv_opa_t v)
{
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) == v) return;
    lv_obj_set_style_bg_opa(o, v, 0);
}

static void set_shown(lv_obj_t *o, bool on)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != on) return;   // already as wanted
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ---------- formatting ----------
static void fmt_fps(char *b, size_t n, int fps, bool mrate)
{
    if (mrate) snprintf(b, n, "%.2f", fps * 1000.0 / 1001.0);
    else       snprintf(b, n, "%d", fps);
}

static double sensor_fps(const cam_state_t *st)
{
    return st->offspeed ? st->offspeed_fps * (st->offspeed_mrate ? 1000.0 / 1001.0 : 1.0)
                        : st->project_fps  * (st->project_mrate  ? 1000.0 / 1001.0 : 1.0);
}

static void fmt_angle(char *b, size_t n, int a)
{
    if (a % 100 == 0) snprintf(b, n, "%d" DEG, a / 100);
    else              snprintf(b, n, "%.1f" DEG, a / 100.0);   // e.g. 172.63° shows as 172.6°
}

static int speed_for_angle(const cam_state_t *st, int a)
{
    int s = camproto_speed_for_angle(st, a);   // shared with the protocol code (exact if requested)
    return s > 0 ? s : st->shutter_speed;
}

static void fmt_tint(char *b, size_t n, int t)
{
    if (t > 0)      snprintf(b, n, "+%d", t);
    else if (t < 0) snprintf(b, n, MINUS "%d", -t);
    else            snprintf(b, n, "0");
}

// Remaining time on a card (seconds, > 0).
static void fmt_slot(char *b, size_t n, int s)
{
    if (s >= 3600) snprintf(b, n, "%dh %02dm", s / 3600, (s % 3600) / 60);
    else           snprintf(b, n, "%d:%02d", s / 60, s % 60);
}

// Sensor area as people say it: "12K 17:9", "8K UHD", "4K 2.4:1".
static void fmt_res(char *b, size_t n, int w, int h)
{
    b[0] = 0;
    if (w <= 0 || h <= 0) return;
    if (w == 7680 && h == 4320) { snprintf(b, n, "8K UHD"); return; }
    if (w == 3840 && h == 2160) { snprintf(b, n, "4K UHD"); return; }
    if (w == 1920 && h == 1080) { snprintf(b, n, "HD"); return; }
    static const struct { float r; const char *s; } AR[] = {
        { 1.0f, "1:1" }, { 1.2f, "6:5" }, { 1.3333f, "4:3" }, { 1.5f, "3:2" }, { 1.7778f, "16:9" },
        { 1.8963f, "17:9" }, { 2.0f, "2:1" }, { 2.4f, "2.4:1" },
    };
    const float r = (float)w / h;
    const char *ar = NULL;
    for (size_t i = 0; i < sizeof AR / sizeof AR[0]; i++)
        if (r > AR[i].r * 0.98f && r < AR[i].r * 1.02f) { ar = AR[i].s; break; }
    const int k2 = (w * 2 + 512) / 1024;                 // width in half-K steps
    char k[16];
    if (k2 % 2) snprintf(k, sizeof k, "%d.5K", k2 / 2);
    else        snprintf(k, sizeof k, "%dK", k2 / 2);
    if (ar) snprintf(b, n, "%s %s", k, ar);
    else    snprintf(b, n, "%s %d" "\xC3\x97" "%d", k, w, h);   // fallback: "6K 6144×3240"
}

// Main value for a tile (and the editor for the same parameter).
static void fmt_value(int t, char *v, size_t vn, char *sub, size_t sn)
{
    const cam_state_t *st = &s_st;
    sub[0] = 0;
    if (!st->have_data) { snprintf(v, vn, MINUS MINUS); return; }
    char proj[12];
    fmt_fps(proj, sizeof proj, st->project_fps, st->project_mrate);

    switch (t) {
    case T_FPS:
        if (st->offspeed) {
            fmt_fps(v, vn, st->offspeed_fps, st->offspeed_mrate);
            snprintf(sub, sn, "OFF-SPEED " MIDDOT " %s", proj);
        } else {
            snprintf(v, vn, "%s", proj);
            snprintf(sub, sn, "PROJECT");
        }
        break;
    case T_SHUT: {
        const int a = st->shutter_angle_x100;
        fmt_angle(v, vn, a);
        // The PYXIS sends only the angle in angle mode: derive the speed.
        int spd = speed_for_angle(st, a);
        if (spd > 0) snprintf(sub, sn, "1/%d", spd);
        if (spd > 0 && settings()->exp_lock && st->connected) {
            if (s_xl_short >= 0.1f) snprintf(sub, sn, "1/%d " MIDDOT " " MINUS "%.1f STOP", spd, s_xl_short);
            else                    snprintf(sub, sn, "1/%d " MIDDOT " LOCKED", spd);
        }
        break;
    }
    case T_EI:  snprintf(v, vn, "%d", st->iso); break;
    case T_WB:  snprintf(v, vn, "%dK", st->wb_kelvin); break;
    default: v[0] = 0;
    }
}

// ---------- lock ----------
static bool unlocked(void) { return s_unlocked; }

// Lock track (under the timecode tile). Unlocked: white knob on the left; drag it right to lock
// (knob turns amber). Drag it back left to unlock. It stays as you leave it.
static lv_obj_t *s_lk, *s_lk_fill, *s_lk_knob, *s_lk_lbl, *s_lk_icon;
static int s_lk_min, s_lk_max;         // knob x at rest (locked / unlocked)
static int s_lk_knob_w;
static bool s_lk_drag, s_lk_shown_unl, s_lk_enabled;
static int32_t s_lk_from_x, s_lk_knob_from;

static bool lock_allowed(void) { return s_st.connected && s_st.have_data && !s_st.recording; }

static void lock_label_fade(void);
static void anim_x_cb(void *o, int32_t v) { lv_obj_set_x(o, v); lock_label_fade(); }

static void knob_to(int x, bool animate)
{
    lv_anim_delete(s_lk_knob, anim_x_cb);
    if (!animate) { lv_obj_set_x(s_lk_knob, x); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lk_knob);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(s_lk_knob), x);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// Fade the caption as the knob travels away from its resting end.
static void lock_label_fade(void)
{
    const int x = lv_obj_get_x(s_lk_knob);
    const int travel = s_lk_max - s_lk_min;
    const int moved = s_lk_shown_unl ? x - s_lk_min : s_lk_max - x;   // away from its resting end
    int opa = 255 - moved * 255 * 2 / (travel ? travel : 1);
    opa = opa < 0 ? 0 : opa > 255 ? 255 : opa;
    if (lv_obj_get_style_text_opa(s_lk_lbl, 0) != opa) lv_obj_set_style_text_opa(s_lk_lbl, opa, 0);
}

// Bring the track in line with the lock state (knob end, colours, padlock, caption).
static void lock_sync(bool animate)
{
    const bool unl = unlocked();
    const bool en = lock_allowed();
    if (unl != s_lk_shown_unl || !animate) {
        s_lk_shown_unl = unl;
        // Unlocked: white knob on the left (open padlock). Locked: amber knob on the right.
        lv_obj_set_style_bg_color(s_lk_knob, unl ? C_TEXT : C_ORANGE, 0);
        lv_label_set_text(s_lk_icon, unl ? ICON_LOCK_OPEN : ICON_LOCK);
        lv_obj_set_style_text_color(s_lk_icon, unl ? C_TILE : C_TEXT, 0);
        lv_obj_center(s_lk_icon);
        lv_label_set_text(s_lk_lbl, unl ? "slide to lock" : "slide to unlock");
        // The caption fills the free part of the track, beside the knob.
        lv_obj_align(s_lk_lbl, LV_ALIGN_LEFT_MID, unl ? s_lk_knob_w + s_lk_min : s_lk_min, 0);
        if (!s_lk_drag) {
            knob_to(unl ? s_lk_min : s_lk_max, animate);
            lv_obj_set_style_text_opa(s_lk_lbl, 255, 0);
        }
    }
    if (en != s_lk_enabled || !animate) {
        s_lk_enabled = en;
        lv_obj_set_style_opa(s_lk, en ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    set_shown(s_lk_fill, !unl);                    // faint amber tint on the track while locked
}

static void lock_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);

    if (code == LV_EVENT_PRESSED) {
        if (!lock_allowed()) return;
        s_lk_drag = true;
        lv_anim_delete(s_lk_knob, anim_x_cb);
        s_lk_from_x = p.x;
        s_lk_knob_from = lv_obj_get_x(s_lk_knob);
    } else if (code == LV_EVENT_PRESSING && s_lk_drag) {
        int x = s_lk_knob_from + (p.x - s_lk_from_x);
        x = x < s_lk_min ? s_lk_min : x > s_lk_max ? s_lk_max : x;
        if (x != lv_obj_get_x(s_lk_knob)) {
            lv_obj_set_x(s_lk_knob, x);
            lock_label_fade();
        }
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_lk_drag) {
        s_lk_drag = false;
        const int x = lv_obj_get_x(s_lk_knob);
        const int reach = (s_lk_max - s_lk_min) * 85 / 100;     // must slide 85% of the way
        if (unlocked() && x - s_lk_min >= reach)                   s_unlocked = false;  // slid right
        else if (!unlocked() && s_lk_max - x >= reach && lock_allowed()) s_unlocked = true;  // slid left
        lock_sync(true);
        knob_to(unlocked() ? s_lk_min : s_lk_max, true);
        lv_obj_set_style_text_opa(s_lk_lbl, 255, 0);
        force_redraw();
    }
}

// A tap on a tile while locked: nudge the knob (leftwards) so it's obvious how to unlock.
static void lock_nudge(void)
{
    if (s_lk_drag || unlocked()) return;
    lv_anim_delete(s_lk_knob, anim_x_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lk_knob);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, s_lk_max, s_lk_max - 44);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_reverse_duration(&a, 260);
    lv_anim_set_repeat_count(&a, 2);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ---------- editor ----------
static void editor_close(void)
{
    if (s_ed_tile < 0) return;
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    s_ed_tile = -1;
}

static void row_place(row_t *r, int y, int btn)
{
    lv_obj_set_y(r->box, y);
    lv_obj_set_size(r->minus, rw(btn), btn);
    lv_obj_set_size(r->plus, rw(btn), btn);
    lv_obj_remove_flag(r->box, LV_OBJ_FLAG_HIDDEN);
}

static void seg_render(void)
{
    for (int i = 0; i < 2; i++) {
        const bool on = (i == 1) == s_shut_speed;
        lv_obj_set_style_bg_opa(s_seg_btn[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_seg_lbl[i], on ? C_TEXT : C_TEXT2, 0);
    }
    s_row[0].param = s_shut_speed ? CAM_SHUTTER_SPEED : CAM_SHUTTER;
}

static void editor_refresh(void);

static void editor_open(int t)
{
    s_ed_tile = t;
    lv_obj_add_flag(s_num, LV_OBJ_FLAG_HIDDEN);
    const bool shut = (t == T_SHUT), fps = (t == T_FPS), custom = shut || fps;
    set_shown(s_seg, shut);
    set_shown(s_chips, fps);
    set_shown(s_custom, custom);
    lv_obj_align(s_done, LV_ALIGN_BOTTOM_MID, custom ? 85 : 0, -20);

    if (t == T_WB) {
        s_row[0].param = CAM_WB;   lv_label_set_text(s_row[0].label, "WHITE BALANCE");
        s_row[1].param = CAM_TINT; lv_label_set_text(s_row[1].label, "TINT");
        row_place(&s_row[0], 24, 112);
        row_place(&s_row[1], 214, 112);
    } else {
        if (shut) {
            lv_label_set_text(s_row[0].label, "");        // the ANGLE | SPEED switch is the heading
            seg_render();
            row_place(&s_row[0], 100, 132);
        } else {
            s_row[0].param = fps ? CAM_FPS : CAM_ISO;
            lv_label_set_text(s_row[0].label, fps ? "RECORDING FRAME RATE" : TITLES[t]);
            row_place(&s_row[0], fps ? 64 : 110, 132);
        }
        lv_obj_add_flag(s_row[1].box, LV_OBJ_FLAG_HIDDEN);
    }
    editor_refresh();
    lv_obj_remove_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
}

static void editor_refresh(void)
{
    char v[24], sub[40];
    if (s_ed_tile < 0) return;
    if (s_ed_tile == T_WB) {
        fmt_value(T_WB, v, sizeof v, sub, sizeof sub);
        set_text(s_row[0].value, v);
        set_text(s_row[0].sub, "");
        fmt_tint(v, sizeof v, s_st.tint);
        set_text(s_row[1].value, s_st.have_data ? v : MINUS MINUS);
        set_text(s_row[1].sub, "");
    } else if (s_ed_tile == T_SHUT && s_st.have_data) {
        char ang[16], spd[16];
        fmt_angle(ang, sizeof ang, s_st.shutter_angle_x100);
        snprintf(spd, sizeof spd, "1/%d", speed_for_angle(&s_st, s_st.shutter_angle_x100));
        set_text(s_row[0].value, s_shut_speed ? spd : ang);
        set_text(s_row[0].sub, s_shut_speed ? ang : spd);
        set_text_color(s_row[0].value, C_TEXT);
    } else {
        fmt_value(s_ed_tile, v, sizeof v, sub, sizeof sub);
        set_text(s_row[0].value, v);
        set_text(s_row[0].sub, sub);
        bool os = s_ed_tile == T_FPS && s_st.offspeed;
        set_text_color(s_row[0].value, os ? C_ORANGE : C_TEXT);
        if (s_ed_tile == T_FPS) {
            for (int i = 0; i < NCHIPS; i++) {
                const bool on = s_st.have_data &&
                                (CHIPS[i] == 0 ? !s_st.offspeed : (s_st.offspeed && s_st.offspeed_fps == CHIPS[i]));
                set_bg(s_chip[i], on ? C_TEXT : C_BTN);
                set_text_color(s_chip_lbl[i], on ? C_BG : C_TEXT);
            }
        }
    }
}

// ---------- exact shutter / frame-rate entry ----------
static void num_render(void)
{
    char d[24];
    const char *b = s_num_buf[0] ? s_num_buf : "_";
    switch (s_num_mode) {
    case NUM_SPEED: snprintf(d, sizeof d, "1/%s", b); break;
    case NUM_ANGLE: snprintf(d, sizeof d, "%s" DEG, b); break;
    case NUM_FPS:   snprintf(d, sizeof d, "%s fps", b); break;
    }
    lv_label_set_text(s_num_disp, d);
}

static void num_open(num_mode_t mode)
{
    char hint[96];
    s_num_mode = mode;
    s_num_buf[0] = 0;
    if (mode == NUM_FPS) {
        char proj[12];
        fmt_fps(proj, sizeof proj, s_st.project_fps, s_st.project_mrate);
        snprintf(hint, sizeof hint, "Whole frames, e.g. 48. Enter %d to turn off-speed off (project %s).",
                 s_st.project_fps, proj);
    }
    lv_label_set_text(s_num_title, mode == NUM_FPS ? "Recording frame rate" :
                                   mode == NUM_SPEED ? "Shutter speed" : "Shutter angle");
    lv_label_set_text(s_num_hint, mode == NUM_FPS ? hint :
                                  mode == NUM_SPEED ? "Enter x for 1/x, e.g. 50."
                                                    : "Degrees, e.g. 172.8. Up to two decimals.");
    set_shown(s_num_dot, mode == NUM_ANGLE);
    lv_obj_add_flag(s_num_err, LV_OBJ_FLAG_HIDDEN);
    num_render();
    lv_obj_remove_flag(s_num, LV_OBJ_FLAG_HIDDEN);
}

static void num_error(const char *msg)
{
    lv_label_set_text(s_num_err, msg);
    lv_obj_remove_flag(s_num_err, LV_OBJ_FLAG_HIDDEN);
}

static void num_key_cb(lv_event_t *e)
{
    const char *k = lv_event_get_user_data(e);
    size_t n = strlen(s_num_buf);
    lv_obj_add_flag(s_num_err, LV_OBJ_FLAG_HIDDEN);
    if (!strcmp(k, "DEL")) { if (n) s_num_buf[n - 1] = 0; }
    else if (n < sizeof s_num_buf - 1) {
        const char *dot = strchr(s_num_buf, '.');
        if (k[0] == '.' && dot) return;                        // one decimal point
        if (dot && strlen(dot) > 2) return;                    // two decimals max
        s_num_buf[n] = k[0];
        s_num_buf[n + 1] = 0;
    }
    num_render();
}

static void num_set_cb(lv_event_t *e)
{
    if (!s_num_buf[0]) return;
    if (s_num_mode == NUM_FPS) {
        const int f = atoi(s_num_buf);
        if (f < 1 || f > 120) { num_error("Enter 1 to 120 (the camera may allow less at this resolution)"); return; }
        camera_set_fps(f);
    } else if (s_num_mode == NUM_SPEED) {
        const int d = atoi(s_num_buf);
        const double f = sensor_fps(&s_st);
        const int slowest = (int)(f + 0.999);
        if (d < slowest || d > 5000) {
            char msg[48];
            snprintf(msg, sizeof msg, "Enter 1/%d to 1/5000 at this frame rate", slowest);
            num_error(msg);
            return;
        }
        camera_set_shutter_speed(d);
    } else {
        const int a = (int)(strtod(s_num_buf, NULL) * 100.0 + 0.5);
        if (a < 1 || a > 36000) { num_error("Enter an angle from 0.01 to 360"); return; }
        camera_set_shutter_angle(a);
    }
    lv_obj_add_flag(s_num, LV_OBJ_FLAG_HIDDEN);
}

static void num_cancel_cb(lv_event_t *e) { lv_obj_add_flag(s_num, LV_OBJ_FLAG_HIDDEN); }
static void custom_cb(lv_event_t *e)
{
    num_open(s_ed_tile == T_FPS ? NUM_FPS : s_shut_speed ? NUM_SPEED : NUM_ANGLE);
}

static void chip_cb(lv_event_t *e)
{
    if (!unlocked() || s_st.recording) return;
    const int v = (int)(intptr_t)lv_event_get_user_data(e);
    camera_set_fps(v ? v : s_st.project_fps);
}

static void seg_cb(lv_event_t *e)
{
    s_shut_speed = (intptr_t)lv_event_get_user_data(e) == 1;
    seg_render();
    force_redraw();
}

// ---------- events ----------
static void tile_cb(lv_event_t *e)
{
    const int t = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) return;
    if (s_st.recording || !s_st.connected || !s_st.have_data) return;
    if (unlocked()) editor_open(t);
    else            lock_nudge();
}

static void step_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    if (!unlocked() || s_st.recording || s_ed_tile < 0) return;
    const intptr_t ud = (intptr_t)lv_event_get_user_data(e);   // row * 2 + (plus ? 1 : 0)
    camera_step(s_row[ud / 2].param, (ud & 1) ? +1 : -1);
}

static void done_cb(lv_event_t *e) { editor_close(); }

// ---------- PIN entry ----------
static void pin_render(void)
{
    char shown[24];
    int o = 0;
    const int n = (int)strlen(s_pin_buf);
    for (int i = 0; i < 6; i++) {
        shown[o++] = i < n ? s_pin_buf[i] : '-';
        if (i < 5) shown[o++] = ' ';
    }
    shown[o] = 0;
    lv_label_set_text(s_pin_digits, shown);
}

static void pin_show(bool on)
{
    if (on && lv_obj_has_flag(s_pin, LV_OBJ_FLAG_HIDDEN)) {
        s_pin_buf[0] = 0;
        pin_render();
        lv_obj_remove_flag(s_pin, LV_OBJ_FLAG_HIDDEN);
    } else if (!on) {
        set_shown(s_pin, false);
    }
}

static void pin_key_cb(lv_event_t *e)
{
    const char *k = lv_event_get_user_data(e);
    size_t n = strlen(s_pin_buf);
    if (!strcmp(k, "DEL")) {
        if (n) s_pin_buf[n - 1] = 0;
    } else if (!strcmp(k, "OK")) {
        if (n == 6) camera_enter_pin((uint32_t)atoi(s_pin_buf));
    } else if (n < 6) {
        s_pin_buf[n] = k[0];
        s_pin_buf[n + 1] = 0;
    }
    pin_render();
}

// ---------- battery ----------
// The Tilta plate passes the V-mount battery straight through to the 2-pin LEMO, so the
// camera's DC-input voltage tracks the battery. Lithium packs sit on a long, gentle slope and
// then fall off a cliff, so the voltage is mapped through a curve rather than read as a straight
// line. Two points were measured on a real rig under camera load (15.3 V = 79 %, 12.8 V = 5 %);
// the rest follows the usual 4-cell lithium-ion shape. Warning levels are in Settings > Battery.
static const struct { int mv, pct; } BATT_CURVE[] = {
    { 16600, 100 }, { 16000, 92 }, { 15300, 79 }, { 14900, 66 }, { 14600, 53 }, { 14300, 40 },
    { 14000, 28 }, { 13700, 18 }, { 13400, 11 }, { 13100, 7 }, { 12800, 5 }, { 12400, 2 }, { 12000, 0 },
};
#define BATT_HYST_MV   150     // must recover this much above a threshold to clear it
#define BATT_TICK_MS   250     // batt_tick() rate
#ifndef BATT_SAMPLE_MS
#define BATT_SAMPLE_MS 30000   // time-left estimate: one sample every 30 s...
#endif
#define BATT_SAMPLES   21      // ...over the last 10 minutes
#define BATT_MIN_SAMPLES 7     // and only once there are 3 minutes of history

enum { B_NONE, B_OK, B_LOW, B_CRIT };
static lv_obj_t *s_bat, *s_batt_icon, *s_batt_lbl;
static float s_batt_v;                         // smoothed mV (load ripple and recording steps)
static int s_batt_level = B_NONE;
static float s_batt_hist[BATT_SAMPLES];
static uint32_t s_batt_hist_t[BATT_SAMPLES];
static int s_batt_n, s_batt_head;
static uint32_t s_batt_next;
static int s_batt_mins = -1;                   // estimated minutes left (-1 = not yet known)

static float batt_pct(float mv)
{
    const int n = (int)(sizeof BATT_CURVE / sizeof BATT_CURVE[0]);
    if (mv >= BATT_CURVE[0].mv) return 100;
    for (int i = 1; i < n; i++) {
        if (mv >= BATT_CURVE[i].mv) {
            const float f = (mv - BATT_CURVE[i].mv) / (float)(BATT_CURVE[i - 1].mv - BATT_CURVE[i].mv);
            return BATT_CURVE[i].pct + f * (BATT_CURVE[i - 1].pct - BATT_CURVE[i].pct);
        }
    }
    return 0;
}

static void batt_tick(void)
{
    const int mv = s_st.batt_mv;
    if (mv <= 0) return;
    // Smooth over ~5 s. A sudden jump of 0.7 V or more (a fresh battery, a different supply)
    // starts over; a real battery never moves that fast on its own.
    if (s_batt_level == B_NONE || mv > s_batt_v + 700 || mv < s_batt_v - 700) {
        s_batt_v = mv;
        s_batt_n = s_batt_head = 0;
        s_batt_next = lv_tick_get() + 15000;   // let the smoothing settle before sampling
        s_batt_mins = -1;
        s_batt_level = B_OK;
    } else {
        s_batt_v += (mv - s_batt_v) * (BATT_TICK_MS / 5000.0f);
    }

    // Level with hysteresis, so it doesn't flicker on a threshold.
    const int low = settings()->batt_low_mv, crit = settings()->batt_crit_mv;
    const float v = s_batt_v;
    int lvl = v < crit ? B_CRIT : v < low ? B_LOW : B_OK;
    if (lvl < s_batt_level) {                   // recovering: needs a margin
        const int thr = s_batt_level == B_CRIT ? crit : low;
        if (v < thr + BATT_HYST_MV) lvl = s_batt_level;
    }
    s_batt_level = lvl;

    // Time left: how fast the charge estimate is falling over the last few minutes.
    if ((int32_t)(lv_tick_get() - s_batt_next) >= 0) {
        s_batt_next = lv_tick_get() + BATT_SAMPLE_MS;
        s_batt_hist[s_batt_head] = batt_pct(v);
        s_batt_hist_t[s_batt_head] = lv_tick_get();
        s_batt_head = (s_batt_head + 1) % BATT_SAMPLES;
        if (s_batt_n < BATT_SAMPLES) s_batt_n++;
        int mins = -1;
        if (s_batt_n >= BATT_MIN_SAMPLES) {
            const int newest = (s_batt_head + BATT_SAMPLES - 1) % BATT_SAMPLES;
            const int oldest = (s_batt_head + BATT_SAMPLES - s_batt_n) % BATT_SAMPLES;
            const float used = s_batt_hist[oldest] - s_batt_hist[newest];
            const float span_min = (s_batt_hist_t[newest] - s_batt_hist_t[oldest]) / 60000.0f;
            if (used > 0.3f && span_min > 0) {
                const float m = s_batt_hist[newest] / (used / span_min);
                if (m < 600) mins = (int)m;
            }
        }
        s_batt_mins = mins;
    }
}

static void batt_draw(void)
{
    if (s_st.batt_mv <= 0) return;                 // keeps the placeholder until the camera reports
    const bool live = s_st.connected;
    const lv_color_t c = !live ? C_STALE : s_batt_level == B_CRIT ? C_RED
                       : s_batt_level == B_LOW ? C_ORANGE : C_TEXT;
    char head[12], b[40];
    const float pct = batt_pct(s_batt_v);
    if (settings()->batt_show == BATT_SHOW_PERCENT) snprintf(head, sizeof head, "%d%%", (int)(pct + 0.5f));
    else                                            snprintf(head, sizeof head, "%.1fV", s_batt_v / 1000.0f);
    if (live && s_batt_mins >= 0) {
        int m = s_batt_mins > 20 ? (s_batt_mins + 2) / 5 * 5 : s_batt_mins;   // 5-min steps when far off
        if (m < 1)        snprintf(b, sizeof b, "%s " MIDDOT " <1m", head);
        else if (m >= 60) snprintf(b, sizeof b, "%s " MIDDOT " %dh %02dm", head, m / 60, m % 60);
        else              snprintf(b, sizeof b, "%s " MIDDOT " %dm", head, m);
    } else {
        snprintf(b, sizeof b, "%s", head);
    }
    set_text(s_batt_lbl, b);
    set_text_color(s_batt_lbl, s_batt_level == B_OK && live ? C_TEXT : c);
    set_text(s_batt_icon, s_batt_level == B_CRIT ? ICON_BATT_CRIT :
                          s_batt_level == B_LOW  ? ICON_BATT_1 :
                          pct > 75 ? ICON_BATT_4 : pct > 50 ? ICON_BATT_3 : ICON_BATT_2);
    set_text_color(s_batt_icon, c);
}

// ---------- record frame ----------
// Four bars round the edge (not one full-screen border), so showing, hiding or recolouring the
// frame redraws only the edges. Red while recording; dimmed between a REC press and the
// camera's answer; at critical battery it flashes amber (alternating with red while recording).
static int s_tally_w = -1;

static void tally_layout(void)
{
    const int W = lv_display_get_horizontal_resolution(NULL);
    const int H = lv_display_get_vertical_resolution(NULL);
    const int t = settings()->tally == TALLY_THICK ? 14 : 6;   // "Off" still uses the thin bars for the battery warning
    if (t == s_tally_w) return;
    s_tally_w = t;
    lv_obj_set_size(s_tally[0], W, t);     lv_obj_set_pos(s_tally[0], 0, 0);
    lv_obj_set_size(s_tally[1], W, t);     lv_obj_set_pos(s_tally[1], 0, H - t);
    lv_obj_set_size(s_tally[2], t, H - 2 * t); lv_obj_set_pos(s_tally[2], 0, t);
    lv_obj_set_size(s_tally[3], t, H - 2 * t); lv_obj_set_pos(s_tally[3], W - t, t);
}

static void frame_tick(uint32_t now)
{
    const bool live = s_st.connected;
    const bool rec = live && s_st.recording;
    const bool pend = live && s_pending >= 0;
    const bool crit = live && s_batt_level == B_CRIT;
    const bool phase = (now / 500) & 1;
    const bool tally_on = settings()->tally != TALLY_OFF;

    bool show = false;
    lv_color_t c = C_TALLY;
    lv_opa_t opa = LV_OPA_COVER;
    if (tally_on && (rec || pend)) {
        show = true;
        if (pend) opa = LV_OPA_50;                // pressed, waiting for the camera
    }
    if (crit) {
        if (show) c = phase ? C_ORANGE : C_TALLY;
        else { show = phase; c = C_ORANGE; }
    }
    tally_layout();
    for (int i = 0; i < 4; i++) {
        if (show) { set_bg(s_tally[i], c); set_bg_opa(s_tally[i], opa); }
        set_shown(s_tally[i], show);
    }
}

// ---------- take log ----------
// Every take whose start this board saw: number, start timecode, length. Kept until restart.
#define MAX_TAKES 99
typedef struct { int no; bool tc; uint8_t h, m, s, f; uint32_t secs; } take_t;
static take_t s_takes[MAX_TAKES];            // oldest first
static int s_ntakes, s_take_no;
static take_t s_cur_take;                    // the take rolling now
static bool s_cur_take_on;
static uint32_t s_last_take_until;           // "LAST TAKE" shows under the timecode until then

static void takes_clear(void) { s_ntakes = 0; s_take_no = 0; s_last_take_until = 0; }

static void fmt_hms(char *b, size_t n, uint32_t t)
{
    snprintf(b, n, "%02u:%02u:%02u", (unsigned)(t / 3600), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
}

// ---------- battery history (for the graph) ----------
#define BG_POINTS   240                      // one every 30 s: the last 2 hours
#define BG_MIN_MV   11500
#define BG_MAX_MV   16800
static int32_t s_bg[BG_POINTS];
static int s_bg_n;
static uint32_t s_bg_next, s_bg_ver, s_bg_drawn = UINT32_MAX;

static void bg_sample(uint32_t now)
{
    if (s_batt_level == B_NONE || (int32_t)(now - s_bg_next) < 0) return;
#ifndef BG_SAMPLE_MS
#define BG_SAMPLE_MS 30000
#endif
    s_bg_next = now + BG_SAMPLE_MS;
    memmove(s_bg, s_bg + 1, (BG_POINTS - 1) * sizeof s_bg[0]);
    s_bg[BG_POINTS - 1] = (int32_t)s_batt_v;
    if (s_bg_n < BG_POINTS) s_bg_n++;
    s_bg_ver++;
}

// ---------- settings screen ----------
enum { PG_REC, PG_DISPLAY, PG_BATT, PG_CAMERA, PG_TAKES, PG_UPDATE, PG_ABOUT, NPAGES };
static lv_obj_t *s_set, *s_nav[NPAGES], *s_nav_lbl[NPAGES], *s_sec[NPAGES];
static int s_set_page;
enum { SID_REC_HOLD, SID_TALLY, SID_CLIP, SID_FLIP, SID_START_LOCK, SID_BATT_LOW, SID_BATT_CRIT,
       SID_BATT_SHOW, SID_WB_STEP, SID_NIGHT, SID_SLEEP, SID_EXP_LOCK };

static void rebuild_async(void *arg);
static void xl_resync(uint32_t hold_ms);

static int sget(int id)
{
    const settings_t *c = settings();
    switch (id) {
    case SID_REC_HOLD:   return c->rec_hold_stop;
    case SID_TALLY:      return c->tally;
    case SID_CLIP:       return c->clip_timer;
    case SID_FLIP:       return c->flip;
    case SID_START_LOCK: return c->start_locked;
    case SID_BATT_LOW:   return c->batt_low_mv;
    case SID_BATT_CRIT:  return c->batt_crit_mv;
    case SID_BATT_SHOW:  return c->batt_show;
    case SID_WB_STEP:    return c->wb_step;
    case SID_NIGHT:      return c->night;
    case SID_SLEEP:      return c->sleep;
    case SID_EXP_LOCK:   return c->exp_lock;
    }
    return 0;
}

static void sput(int id, int v)
{
    if (sget(id) == v) return;
    settings_t *c = settings_edit();
    switch (id) {
    case SID_REC_HOLD:   c->rec_hold_stop = v; break;
    case SID_TALLY:      c->tally = v; break;
    case SID_CLIP:       c->clip_timer = v; break;
    case SID_FLIP:       c->flip = v; break;
    case SID_START_LOCK: c->start_locked = v; break;
    case SID_BATT_LOW:
        c->batt_low_mv = v;
        if (c->batt_crit_mv > v - 200) c->batt_crit_mv = v - 200;   // critical stays below low
        break;
    case SID_BATT_CRIT:  c->batt_crit_mv = v; break;
    case SID_BATT_SHOW:  c->batt_show = v; break;
    case SID_WB_STEP:    c->wb_step = v; break;
    case SID_NIGHT:      c->night = v; break;
    case SID_SLEEP:      c->sleep = v; break;
    case SID_EXP_LOCK:   c->exp_lock = v; xl_resync(0); break;
    }
    settings_changed();
    if (id == SID_FLIP) plat_restart();            // the display is set up once, at start-up
    if (id == SID_NIGHT) lv_async_call(rebuild_async, NULL);   // new palette: rebuild the screens
    force_redraw();
}

// Segmented controls
typedef struct { int id, n; int val[3]; lv_obj_t *btn[3], *lbl[3]; } sseg_t;
static sseg_t s_ssegs[12];
static int s_nssegs;

// Steppers
typedef struct { int id, lo, hi, step; lv_obj_t *val; } sstep_t;
static sstep_t s_ssteps[2];
static int s_nssteps;

// Read-only lines
enum { INFO_CAM_NAME, INFO_CAM_LINK, INFO_LENS, INFO_FW, INFO_BT, INFO_TC, INFO_DISP, INFO_MEM, INFO_UP,
       INFO_UPD_NET, INFO_UPD_PASS, INFO_UPD_URL, INFO_UPD_STATUS, INFO_UPD_SLOT, INFO_TAKES, INFO_COUNT };
static lv_obj_t *s_info[INFO_COUNT];

// Two-tap actions
enum { ACT_FORGET, ACT_RESET, ACT_CLEAR_TAKES, NACTS };
static lv_obj_t *s_act_lbl[NACTS];
static uint32_t s_act_armed[NACTS];             // tick of the first tap (0 = not armed)
static const char *ACT_TEXT[NACTS] = { "Forget camera", "Reset settings", "Clear list" };

static lv_obj_t *s_upd_btn_lbl;                 // Update page: Start / Stop
static lv_obj_t *s_take_col[3];                 // Takes page: number, start TC, length (one line each)
static lv_obj_t *s_chart, *s_chart_lo, *s_chart_crit, *s_chart_left;
static lv_chart_series_t *s_chart_ser;

// Display timing, for About.
static uint64_t s_refr_t0;
static uint32_t s_frames, s_fps, s_frame_us, s_frames_t0;

static int chart_y(int mv, int h)                // a voltage's row in the graph
{
    if (mv < BG_MIN_MV) mv = BG_MIN_MV;
    if (mv > BG_MAX_MV) mv = BG_MAX_MV;
    return (int)((int64_t)(BG_MAX_MV - mv) * (h - 1) / (BG_MAX_MV - BG_MIN_MV));
}

static void render_takes(void)
{
    static char col[3][MAX_TAKES > 10 ? 10 * 16 : 160];
    char b[64], t[16];
    uint32_t total = 0;
    for (int i = 0; i < s_ntakes; i++) total += s_takes[i].secs;
    fmt_hms(t, sizeof t, total);
    if (s_ntakes) snprintf(b, sizeof b, "%d take%s " MIDDOT " %s recorded", s_take_no, s_take_no == 1 ? "" : "s", t);
    else snprintf(b, sizeof b, "No takes yet");
    set_text(s_info[INFO_TAKES], b);
    for (int c = 0; c < 3; c++) col[c][0] = 0;
    for (int k = 0, i = s_ntakes - 1; i >= 0 && k < 10; i--, k++) {     // newest first
        const take_t *tk = &s_takes[i];
        char line[24];
        snprintf(line, sizeof line, "%s%d", k ? "\n" : "", tk->no);
        strcat(col[0], line);
        if (tk->tc) snprintf(line, sizeof line, "%s%02d:%02d:%02d:%02d", k ? "\n" : "", tk->h, tk->m, tk->s, tk->f);
        else        snprintf(line, sizeof line, "%s" MINUS MINUS, k ? "\n" : "");
        strcat(col[1], line);
        fmt_hms(t, sizeof t, tk->secs);
        snprintf(line, sizeof line, "%s%s", k ? "\n" : "", t);
        strcat(col[2], line);
    }
    for (int c = 0; c < 3; c++) set_text(s_take_col[c], col[c]);
}

static void render_chart(void)
{
    const int h = lv_obj_get_height(s_chart);
    lv_obj_set_y(s_chart_lo, lv_obj_get_y(s_chart) + chart_y(settings()->batt_low_mv, h));
    lv_obj_set_y(s_chart_crit, lv_obj_get_y(s_chart) + chart_y(settings()->batt_crit_mv, h));
    char b[48];
    if (s_batt_mins >= 0 && s_st.connected) {
        const int m = s_batt_mins;
        if (m >= 60) snprintf(b, sizeof b, "Empty in about %dh %02dm", m / 60, m % 60);
        else         snprintf(b, sizeof b, "Empty in about %dm", m);
    } else {
        snprintf(b, sizeof b, s_bg_n ? "Time left: not enough data yet" : "Waiting for the camera");
    }
    set_text(s_chart_left, b);
    if (s_bg_drawn == s_bg_ver) return;
    s_bg_drawn = s_bg_ver;
    for (int i = 0; i < BG_POINTS; i++)
        if (i < BG_POINTS - s_bg_n) s_bg[i] = LV_CHART_POINT_NONE;
    lv_chart_set_ext_y_array(s_chart, s_chart_ser, s_bg);
    lv_chart_refresh(s_chart);
}

static void settings_render(void)
{
    if (!s_set || lv_obj_has_flag(s_set, LV_OBJ_FLAG_HIDDEN)) return;
    for (int i = 0; i < NPAGES; i++) {
        const bool on = i == s_set_page;
        set_bg_opa(s_nav[i], on ? LV_OPA_COVER : LV_OPA_TRANSP);
        set_text_color(s_nav_lbl[i], on ? C_TEXT : C_TEXT2);
        set_shown(s_sec[i], on);
    }
    for (int r = 0; r < s_nssegs; r++) {
        const sseg_t *g = &s_ssegs[r];
        const int v = sget(g->id);
        for (int i = 0; i < g->n; i++) {
            set_bg_opa(g->btn[i], g->val[i] == v ? LV_OPA_COVER : LV_OPA_TRANSP);
            set_text_color(g->lbl[i], g->val[i] == v ? C_TEXT : C_TEXT2);
        }
    }
    char b[64];
    for (int i = 0; i < s_nssteps; i++) {
        snprintf(b, sizeof b, "%.1f V", sget(s_ssteps[i].id) / 1000.0);
        set_text(s_ssteps[i].val, b);
    }
    const uint32_t now = lv_tick_get();
    for (int i = 0; i < NACTS; i++) {
        if (s_act_armed[i] && now - s_act_armed[i] > 3000) s_act_armed[i] = 0;
        set_text(s_act_lbl[i], s_act_armed[i] ? "Tap again to confirm" : ACT_TEXT[i]);
    }

    switch (s_set_page) {
    case PG_BATT: render_chart(); break;
    case PG_TAKES: render_takes(); break;
    case PG_CAMERA: {
        cam_diag_t d;
        camera_get_diag(&d);
        set_text(s_info[INFO_CAM_NAME], d.name[0] ? d.name : (d.bonded ? "Paired" : "Not paired"));
        if (s_st.connected) snprintf(b, sizeof b, "Connected " MIDDOT " %d dBm", d.rssi);
        else snprintf(b, sizeof b, "%s", s_st.link == LINK_PAIRING ? "Pairing" ELLIPSIS :
                                         s_st.link == LINK_CONNECTING ? "Connecting" ELLIPSIS : "Searching" ELLIPSIS);
        set_text(s_info[INFO_CAM_LINK], b);
        set_text_color(s_info[INFO_CAM_LINK], s_st.connected ? C_TEXT : C_ORANGE);
        if (s_st.lens_type[0]) snprintf(b, sizeof b, "%.40s", s_st.lens_type);
        else if (s_st.lens_fstop_x10 || s_st.lens_focal_mm) snprintf(b, sizeof b, "Electronic lens");
        else snprintf(b, sizeof b, "Not reported");
        set_text(s_info[INFO_LENS], b);
        break;
    }
    case PG_UPDATE: {
        upd_status_t u;
        update_get(&u);
        const bool on = u.state != UPD_OFF && u.state != UPD_FAILED;
        set_text(s_info[INFO_UPD_NET], on ? u.ssid : MINUS MINUS);
        set_text(s_info[INFO_UPD_PASS], on ? u.pass : MINUS MINUS);
        set_text(s_info[INFO_UPD_URL], on ? u.url : MINUS MINUS);
        switch (u.state) {
        case UPD_OFF:       snprintf(b, sizeof b, "Wi-Fi off"); break;
        case UPD_STARTING:  snprintf(b, sizeof b, "Starting Wi-Fi" ELLIPSIS); break;
        case UPD_READY:     snprintf(b, sizeof b, "Waiting for the file"); break;
        case UPD_RECEIVING: snprintf(b, sizeof b, "Receiving %d%%", u.pct); break;
        case UPD_DONE:      snprintf(b, sizeof b, "Done, restarting"); break;
        case UPD_FAILED:    snprintf(b, sizeof b, "%.60s", u.msg); break;
        }
        set_text(s_info[INFO_UPD_STATUS], b);
        set_text_color(s_info[INFO_UPD_STATUS], u.state == UPD_FAILED ? C_RED : u.state == UPD_OFF ? C_TEXT2 : C_TEXT);
        snprintf(b, sizeof b, "%s " MIDDOT " slot %s", BIGSIX_VERSION, u.slot);
        set_text(s_info[INFO_UPD_SLOT], b);
        set_text(s_upd_btn_lbl, on ? "Stop" : "Start");
        break;
    }
    case PG_ABOUT: {
        cam_diag_t d;
        camera_get_diag(&d);
        static char date[16];                          // __DATE__ pads the day: "Nov  8 2026"
        if (!date[0]) {
            const char *p = __DATE__;
            for (int i = 0, o = 0; p[i] && o < (int)sizeof date - 1; i++)
                if (!(p[i] == ' ' && p[i + 1] == ' ')) date[o++] = p[i];
        }
        snprintf(b, sizeof b, "%s " MIDDOT " %s", BIGSIX_VERSION, date);
        set_text(s_info[INFO_FW], b);
        if (d.conn_itvl_us) snprintf(b, sizeof b, "%.2f ms " MIDDOT " MTU %d " MIDDOT " %d B", d.conn_itvl_us / 1000.0, d.mtu, d.data_len);
        else snprintf(b, sizeof b, "Not connected");
        set_text(s_info[INFO_BT], b);
        snprintf(b, sizeof b, "%d / s " MIDDOT " settings %d / s", d.tc_per_s, d.rx_per_s);
        set_text(s_info[INFO_TC], b);
        const unsigned fps = plat_ms() - s_frames_t0 > 2000 ? 0 : (unsigned)s_fps;   // nothing redrawn lately
        snprintf(b, sizeof b, "%u fps " MIDDOT " %.1f ms a frame", fps, s_frame_us / 1000.0);
        set_text(s_info[INFO_DISP], b);
        snprintf(b, sizeof b, "%u KB internal " MIDDOT " %.1f MB PSRAM", (unsigned)(plat_free_internal() / 1024),
                 plat_free_psram() / 1048576.0);
        set_text(s_info[INFO_MEM], b);
        const uint32_t up = plat_ms() / 1000;
        snprintf(b, sizeof b, "%uh %02um %02us", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60));
        set_text(s_info[INFO_UP], b);
        break;
    }
    }
}

static void presets_close(void);

static void settings_open(void)
{
    editor_close();
    presets_close();
    lv_obj_remove_flag(s_set, LV_OBJ_FLAG_HIDDEN);
    settings_render();
}

static void settings_close(void) { set_shown(s_set, false); }

static void nav_cb(lv_event_t *e)
{
    s_set_page = (int)(intptr_t)lv_event_get_user_data(e);
    settings_render();
}

static void sseg_cb(lv_event_t *e)
{
    const intptr_t ud = (intptr_t)lv_event_get_user_data(e);   // row * 4 + option
    const sseg_t *g = &s_ssegs[ud / 4];
    sput(g->id, g->val[ud % 4]);
    settings_render();
}

static void sstep_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
    const intptr_t ud = (intptr_t)lv_event_get_user_data(e);   // stepper * 2 + (plus ? 1 : 0)
    const sstep_t *s = &s_ssteps[ud / 2];
    int hi = s->hi;
    if (s->id == SID_BATT_CRIT) hi = settings()->batt_low_mv - 200;   // critical stays below low
    int v = sget(s->id) + ((ud & 1) ? s->step : -s->step);
    v = v < s->lo ? s->lo : v > hi ? hi : v;
    sput(s->id, v);
    settings_render();
}

static void act_cb(lv_event_t *e)
{
    const int a = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_act_armed[a]) { s_act_armed[a] = lv_tick_get() | 1; settings_render(); return; }
    s_act_armed[a] = 0;
    if (a == ACT_FORGET) camera_forget();
    else if (a == ACT_CLEAR_TAKES) takes_clear();
    else {
        const bool flip = settings()->flip, night = settings()->night;
        settings_reset();
        if (settings()->flip != flip) plat_restart();
        else if (settings()->night != night) lv_async_call(rebuild_async, NULL);
    }
    force_redraw();
    settings_render();
}

static void upd_cb(lv_event_t *e)
{
    upd_status_t u;
    update_get(&u);
    if (u.state == UPD_OFF || u.state == UPD_FAILED) update_start();
    else update_stop();
    settings_render();
}

static void set_done_cb(lv_event_t *e) { settings_close(); }

static void bat_cb(lv_event_t *e)                // the Settings button
{
    if (s_st.recording) return;                    // the main screen stays up while rolling
    settings_open();
}

// ---------- presets ----------
static lv_obj_t *s_pre, *s_pre_card[NPRESETS], *s_pre_l1[NPRESETS], *s_pre_l2[NPRESETS], *s_pre_l3[NPRESETS];
static lv_obj_t *s_pre_hint;
static uint32_t s_pre_press_ms, s_pre_hint_until;
static int s_pre_saved = -1;                     // card whose hold already saved (ignore the release)
#define PRE_HINT "Tap to apply. Hold 1 s to save what the camera is set to now."   /* 14 px font: ASCII only */

static int cur_fps(void) { return s_st.offspeed ? s_st.offspeed_fps : s_st.project_fps; }

static bool preset_matches(const preset_t *p)
{
    return p->used && s_st.have_data && p->fps == cur_fps() && p->angle_x100 == s_st.shutter_angle_x100 &&
           p->iso == s_st.iso && p->wb == s_st.wb_kelvin && p->tint == s_st.tint;
}

static void pre_hint(const char *msg, bool warn)
{
    set_text(s_pre_hint, msg);
    set_text_color(s_pre_hint, warn ? C_ORANGE : C_TEXT2);
    s_pre_hint_until = lv_tick_get() + 2500;
}

static void presets_render(void)
{
    if (!s_pre || lv_obj_has_flag(s_pre, LV_OBJ_FLAG_HIDDEN)) return;
    char a[40], b[40], c[40];
    for (int i = 0; i < NPRESETS; i++) {
        const preset_t *p = &presets()[i];
        if (p->used) {
            snprintf(a, sizeof a, "%d fps", p->fps);
            fmt_angle(b, sizeof b, p->angle_x100);
            snprintf(b + strlen(b), sizeof b - strlen(b), "  " MIDDOT "  EI %d", p->iso);
            char t[12];
            fmt_tint(t, sizeof t, p->tint);
            snprintf(c, sizeof c, "%dK  " MIDDOT "  tint %s", p->wb, t);
        } else {
            snprintf(a, sizeof a, "Empty");
            snprintf(b, sizeof b, "Hold to save the");
            snprintf(c, sizeof c, "camera's settings");
        }
        set_text(s_pre_l1[i], a);
        set_text(s_pre_l2[i], b);
        set_text(s_pre_l3[i], c);
        set_text_color(s_pre_l1[i], p->used ? C_TEXT : C_TEXT2);
        lv_obj_set_style_border_opa(s_pre_card[i], preset_matches(p) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (s_pre_hint_until && lv_tick_get() > s_pre_hint_until) {
        s_pre_hint_until = 0;
        set_text(s_pre_hint, PRE_HINT);
        set_text_color(s_pre_hint, C_TEXT2);
    }
}

static void presets_open(void)
{
    editor_close();
    lv_obj_remove_flag(s_pre, LV_OBJ_FLAG_HIDDEN);
    presets_render();
}

static void presets_close(void) { if (s_pre) set_shown(s_pre, false); }

static void pre_card_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    preset_t *p = &presets()[i];
    if (code == LV_EVENT_PRESSED) { s_pre_press_ms = lv_tick_get(); s_pre_saved = -1; return; }
    if (code == LV_EVENT_PRESSING && s_pre_saved != i && lv_tick_get() - s_pre_press_ms >= 1000) {
        s_pre_saved = i;                              // hold: save the camera's current settings
        if (!s_st.have_data) { pre_hint("No camera settings to save yet", true); return; }
        *p = (preset_t){ .used = 1, .fps = cur_fps(), .angle_x100 = s_st.shutter_angle_x100,
                         .iso = s_st.iso, .wb = s_st.wb_kelvin, .tint = s_st.tint };
        presets_changed();
        char m[32];
        snprintf(m, sizeof m, "Saved to %c", 'A' + i);
        pre_hint(m, false);
        presets_render();
        return;
    }
    if (code != LV_EVENT_SHORT_CLICKED || s_pre_saved == i) return;
    if (!p->used) { pre_hint("Empty: hold 1 s to save the camera's settings here", false); return; }
    if (!s_st.connected || !s_st.have_data) { pre_hint("Not connected to the camera", true); return; }
    if (s_st.recording) { pre_hint("Stop recording first", true); return; }
    if (!unlocked()) { pre_hint("Editing is locked", true); return; }
    xl_resync(1500);                                 // exposure lock: keep the preset's angle as saved
    camera_set_fps(p->fps);                          // frame rate first: the angle stays as set
    camera_set_shutter_angle(p->angle_x100);
    camera_set_iso(p->iso);
    camera_set_wb(p->wb, p->tint);
    presets_close();
}

static void pre_done_cb(lv_event_t *e) { presets_close(); }

// The Presets button.
static void tc_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED || s_st.recording) return;
    if (!unlocked() && s_st.connected) { lock_nudge(); return; }
    presets_open();
}

// ---------- drawing ----------
static void update_tc(void)
{
    char tc[16];                                    // 11 used; room for out-of-range fields
    if (s_st.tc_valid) snprintf(tc, sizeof tc, "%02d:%02d:%02d:%02d", s_st.tc_h % 100, s_st.tc_m % 100, s_st.tc_s % 100, s_st.tc_f % 100);
    else               snprintf(tc, sizeof tc, "--:--:--:--");
    const lv_color_t c = s_st.connected ? C_TEXT : C_STALE;
    for (int i = 0; i < 11; i++) {
        const char ch[2] = { tc[i], 0 };
        set_text(s_tc_cell[i], ch);
        set_text_color(s_tc_cell[i], c);
    }
}

static void update_cards(void)
{
    char v[24];
    const bool live = s_st.connected;
    for (int i = 0; i < 2; i++) {
        const bool known = i < s_st.slot_n || s_st.slot_present[i];
        const bool card = s_st.slot_present[i];
        const int left = s_st.slot_s[i];
        const bool active = live && card && s_st.slot_active == i;
        if (!known)         snprintf(v, sizeof v, MINUS MINUS);
        else if (!card)     snprintf(v, sizeof v, "EMPTY");
        else if (left <= 0) snprintf(v, sizeof v, "FULL");
        else                fmt_slot(v, sizeof v, left);
        set_text(s_slot_lbl[i], v);
        set_text_color(s_slot_lbl[i], !live ? C_STALE : !card ? C_STALE : left < 300 ? C_RED : C_TEXT);
        // Card icon + number: white for the slot being recorded to (red while rolling), grey for a
        // second card standing by, dim for an empty slot.
        const lv_color_t ic = !live || !card ? C_STALE : active ? (s_st.recording ? C_RED : C_TEXT) : C_TEXT2;
        set_text_color(s_slot_icon[i], ic);
        set_text_color(s_slot_num[i], ic);
    }
}

// Under the timecode: link state, the clip timer while rolling, else the sensor area.
static void update_status(uint32_t now)
{
    char b[40];
    const bool live = s_st.connected;
    lv_color_t c = C_TEXT2;
    bool dot = false;
    if (!live) {
        snprintf(b, sizeof b, "%s", s_st.link == LINK_PAIRING ? "PAIRING" :
                                    s_st.link == LINK_CONNECTING ? "CONNECTING" :
                                    s_st.have_data ? "RECONNECTING" : "SEARCHING");
        c = C_ORANGE;
    } else if (recbutton_hold_progress() > 0 || now < s_hint_until) {
        snprintf(b, sizeof b, recbutton_hold_progress() > 0 ? "KEEP HOLDING TO STOP" : "HOLD REC TO STOP");
        c = C_ORANGE;
    } else if (s_st.recording || s_pending == 1) {
        dot = true;
        c = C_RED;
        if (s_st.recording && settings()->clip_timer && s_saw_idle) {
            const uint32_t t = (now - s_rec_since) / 1000;
            snprintf(b, sizeof b, "REC  %02u:%02u:%02u", (unsigned)(t / 3600), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
            if (s_cur_take_on) snprintf(b + strlen(b), sizeof b - strlen(b), "  T%d", s_cur_take.no);
        } else {
            snprintf(b, sizeof b, "REC");
        }
    } else if (now < s_last_take_until && s_ntakes) {
        char t[16];
        fmt_hms(t, sizeof t, s_takes[s_ntakes - 1].secs);
        snprintf(b, sizeof b, "TAKE %d  %s", s_takes[s_ntakes - 1].no, t);
        c = C_TEXT;
    } else if (s_st.lens_fstop_x10 || s_st.lens_focal_mm) {           // lens, if the camera reports it
        char f[16] = "", m[16] = "";
        if (s_st.lens_fstop_x10) snprintf(f, sizeof f, "f/%d.%d", s_st.lens_fstop_x10 / 10, s_st.lens_fstop_x10 % 10);
        if (s_st.lens_focal_mm) snprintf(m, sizeof m, "%dmm", s_st.lens_focal_mm);
        snprintf(b, sizeof b, "%s%s%s", f, f[0] && m[0] ? "  " MIDDOT "  " : "", m);
    } else if (s_st.lens_iris[0] || s_st.lens_focal[0]) {
        snprintf(b, sizeof b, "%.12s%s%.12s", s_st.lens_iris, s_st.lens_iris[0] && s_st.lens_focal[0] ? "  " MIDDOT "  " : "",
                 s_st.lens_focal);
    } else {
        fmt_res(b, sizeof b, s_st.res_w, s_st.res_h);
    }
    set_text(s_status, b);
    set_text_color(s_status, c);
    set_shown(s_status_dot, dot);
}

static void update_values(void)
{
    char v[24], sub[40];
    const bool live = s_st.connected;

    for (int t = 0; t < T_COUNT; t++) {
        fmt_value(t, v, sizeof v, sub, sizeof sub);
        set_text(s_val[t], v);
        set_text(s_sub[t], sub);
        set_text_color(s_val[t], !live ? C_STALE : (t == T_FPS && s_st.offspeed) ? C_ORANGE : C_TEXT);
    }
    set_text_color(s_sub[T_FPS], (live && s_st.offspeed) ? C_ORANGE : C_TEXT2);
    set_text_color(s_sub[T_SHUT], (live && settings()->exp_lock && s_xl_short >= 0.1f) ? C_ORANGE : C_TEXT2);

    fmt_tint(v, sizeof v, s_st.tint);                // tint: the WB tile's bottom line
    set_text(s_sub[T_WB], s_st.have_data ? v : MINUS MINUS);
    set_text_color(s_sub[T_WB], live ? C_TEXT : C_STALE);

    update_tc();
    update_cards();
    set_text_color(s_btn_pre, s_st.recording ? C_STALE : C_TEXT);
    set_text_color(s_btn_set, s_st.recording ? C_STALE : C_TEXT);
    pin_show(s_st.need_pin);
    lock_sync(true);
    editor_refresh();
}

// Recording transitions: clip timer start, and the press-to-confirm "pending" state.
static void track_recording(uint32_t now)
{
    const bool rec = s_st.connected && s_st.recording;
    if (!s_st.connected) s_saw_idle = false;
    else if (!rec) s_saw_idle = true;
    if (rec && !s_was_rec) {
        s_rec_since = now;
        s_cur_take_on = s_saw_idle;                 // a take we saw start
        s_cur_take = (take_t){ .no = s_take_no + 1, .tc = s_st.tc_valid,
                               .h = s_st.tc_h, .m = s_st.tc_m, .s = s_st.tc_s, .f = s_st.tc_f };
    }
    if (!rec && s_was_rec && s_cur_take_on) {     // stopped: log it
        s_cur_take_on = false;
        s_cur_take.secs = (now - s_rec_since + 500) / 1000;
        if (s_ntakes == MAX_TAKES) { memmove(s_takes, s_takes + 1, (MAX_TAKES - 1) * sizeof s_takes[0]); s_ntakes--; }
        s_takes[s_ntakes++] = s_cur_take;
        s_take_no = s_cur_take.no;
        if (s_st.connected) s_last_take_until = now + 8000;
    }
    s_was_rec = rec;

    if (s_st.rec_req_seq != s_req_seen) {          // a start / stop just went out
        s_req_seen = s_st.rec_req_seq;
        s_pending = s_st.rec_req_start ? 1 : 0;
        s_pending_ms = now;
    }
    if (s_pending >= 0 && (rec == (s_pending == 1) || now - s_pending_ms > 1500 || !s_st.connected))
        s_pending = -1;

    if (recbutton_hint_seq() != s_hint_seen) {
        s_hint_seen = recbutton_hint_seq();
        s_hint_until = now + 1500;
    }
}

// ---------- exposure lock ----------
// Angle mode keeps the shutter angle when the frame rate changes, so the shutter speed, and the
// brightness, change with it (24 -> 48 fps at 180 deg: 1/48 -> 1/96, a stop darker). With the lock
// on, the board sends the angle that keeps the speed: 24 -> 48 fps at 180 deg becomes 360 deg.
//
// It follows whatever happens: a shutter change (here or on the camera) sets the speed to keep; a
// frame-rate change (here or on the camera) is answered with the angle for that speed, once the
// rate has been steady for XL_SETTLE_MS. If the angle changes along with the rate (the camera, or
// another controller, set both), that pair is taken as meant. Presets apply exactly as saved.
// Past 360 deg the angle stays at 360 and the shutter tile shows how many stops short it is; the
// speed is kept, so going back to a lower rate restores the original angle.
#define XL_SETTLE_MS  300                // frame rate steady this long before correcting
#define XL_ECHO_MS    1500               // an angle change this soon after ours is the camera's answer
#define XL_MAX_ANGLE  36000
static double s_xl_exp;                  // shutter time to keep, seconds (0 = not known yet)
static double s_xl_fps;                  // recording rate it was last checked against
static int s_xl_angle;                   // angle last seen
static int s_xl_sent = -1;               // angle we sent, until the camera answers
static uint32_t s_xl_sent_ms, s_xl_wait_ms, s_xl_hold_until;
static bool s_xl_wait;                   // a rate change is waiting to settle
static int s_xl_wait_angle;              // the angle when it started
static double s_xl_wait_fps;             // the rate last seen while waiting

// Forget the speed and take it again from what the camera shows, after hold_ms (presets: once
// their writes have landed).
static void xl_resync(uint32_t hold_ms)
{
    s_xl_exp = 0;
    s_xl_wait = false;
    s_xl_sent = -1;
    s_xl_short = 0;
    s_xl_hold_until = lv_tick_get() + hold_ms;
}

static void xl_adopt(double f, int a)
{
    s_xl_fps = f;
    s_xl_angle = a;
    s_xl_exp = a / 36000.0 / f;
    s_xl_wait = false;
    s_xl_short = 0;
}

static void xl_tick(uint32_t now)
{
    if (!settings()->exp_lock || !s_st.connected || !s_st.have_data) { s_xl_exp = 0; return; }
    const double f = sensor_fps(&s_st);
    const int a = s_st.shutter_angle_x100;
    if (f <= 0 || a <= 0) return;
    if (s_st.transport_mode == 1) s_xl_hold_until = now + 1500;   // playback: the clip's format, not ours
    if (s_xl_exp == 0 || (int32_t)(now - s_xl_hold_until) < 0 || s_st.recording) {
        if (s_xl_exp == 0 || a != s_xl_angle || f != s_xl_fps) { xl_adopt(f, a); force_redraw(); }
        return;
    }
    const bool echo = s_xl_sent >= 0 && now - s_xl_sent_ms < XL_ECHO_MS;
    if (a != s_xl_angle) {
        if (echo) { s_xl_sent = -1; s_xl_angle = a; if (s_xl_wait) s_xl_wait_angle = a; }   // our correction
        else if (!s_xl_wait) { xl_adopt(f, a); force_redraw(); return; }                   // a new speed
    }
    if (f != s_xl_fps && !s_xl_wait) { s_xl_wait = true; s_xl_wait_ms = now; s_xl_wait_angle = a; s_xl_wait_fps = f; }
    if (!s_xl_wait) return;
    if (a != s_xl_wait_angle) { xl_adopt(f, a); force_redraw(); return; }   // rate and angle changed together
    if (f != s_xl_wait_fps) { s_xl_wait_fps = f; s_xl_wait_ms = now; }      // still moving (+ held down)
    if (now - s_xl_wait_ms < XL_SETTLE_MS) return;
    s_xl_fps = f;
    s_xl_wait = false;
    const double want = s_xl_exp * f * 36000.0;
    int send = (int)(want + 0.5);
    s_xl_short = 0;
    if (send > XL_MAX_ANGLE) {
        send = XL_MAX_ANGLE;
        s_xl_short = (float)log2(want / XL_MAX_ANGLE);
    }
    if (send < 1) send = 1;
    if (send != a) {
        camera_set_shutter_angle(send);
        s_xl_sent = send;
        s_xl_sent_ms = now;
#ifdef ESP_PLATFORM
        ESP_LOGI("explock", "%.3f fps: shutter %.2f -> %.2f deg (keeps 1/%.0f)", f, a / 100.0, send / 100.0, 1.0 / s_xl_exp);
#endif
    }
    force_redraw();
}

// ---------- sleep ----------
// With no camera for a while (Settings > Display > Sleep), a black cover goes over everything,
// then the backlight goes off, the panel is refreshed slowly and Bluetooth scans at a low duty
// cycle. Anything that counts as activity wakes it: the camera link leaving SEARCHING (the camera
// is back, or asking for its PIN), a touch, the REC button, or a Wi-Fi update running.
//
// Espressif's display adapter has its own auto-sleep, but it pauses LVGL, and LVGL is what polls
// the touch controller here (its interrupt line isn't wired), so a touch couldn't wake it.
#ifndef SLEEP_MIN_MS
#define SLEEP_MIN_MS  60000              // one "minute" of the Sleep setting (shorter in sim tests)
#endif
#define SLEEP_BLANK_MS 150               // time for the black cover to reach the panel
enum { SL_AWAKE, SL_BLANKING, SL_ASLEEP };
static int s_sleep_state = SL_AWAKE;
static uint32_t s_sleep_t;               // when blanking started
static uint32_t s_active_ms;             // last activity other than touch
static uint32_t s_press_seen;            // recbutton_press_seq() last seen
static lv_obj_t *s_sleep;                // the black cover (top layer); swallows the waking touch
static bool s_sleep_touch;               // a finger is on the cover

static void sleep_cover_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { s_sleep_touch = true; return; }
    // Released: the touch that woke the screen is over; take the (now clear) cover away.
    s_sleep_touch = false;
    if (s_sleep_state == SL_AWAKE) set_shown(s_sleep, false);
}

static uint32_t sleep_timeout(void)
{
    switch (settings()->sleep) {
    case SLEEP_1MIN: return SLEEP_MIN_MS;
    case SLEEP_5MIN: return 5 * SLEEP_MIN_MS;
    default:         return 0;
    }
}

static void sleep_tick(uint32_t now)
{
    upd_status_t u;
    update_get(&u);
    if (s_st.link != LINK_SEARCHING || (u.state != UPD_OFF && u.state != UPD_FAILED)) s_active_ms = now;
    if (recbutton_press_seq() != s_press_seen) { s_press_seen = recbutton_press_seq(); s_active_ms = now; }
    uint32_t idle = now - s_active_ms;
    const uint32_t touch_idle = lv_display_get_inactive_time(NULL);
    if (touch_idle < idle) idle = touch_idle;
    const uint32_t timeout = sleep_timeout();
    const bool want = timeout && idle >= timeout;

    if (want && s_sleep_state == SL_AWAKE) {
        s_sleep_state = SL_BLANKING;
        s_sleep_t = now;
        lv_obj_set_style_bg_opa(s_sleep, LV_OPA_COVER, 0);
        set_shown(s_sleep, true);
#ifdef ESP_PLATFORM
        ESP_LOGI("sleep", "no camera for %u s: sleeping", (unsigned)(idle / 1000));
#endif
    } else if (want && s_sleep_state == SL_BLANKING && now - s_sleep_t >= SLEEP_BLANK_MS) {
        s_sleep_state = SL_ASLEEP;
        plat_display_power(false);
        camera_set_low_power(true);
    } else if (!want && s_sleep_state != SL_AWAKE) {
        if (s_sleep_state == SL_ASLEEP) {
            plat_display_power(true);
            camera_set_low_power(false);
        }
        s_sleep_state = SL_AWAKE;
        force_redraw();
        // Woken by a touch: show the screen at once, but keep the clear cover until the finger
        // lifts, so the waking touch doesn't also press whatever is under it.
        if (s_sleep_touch) lv_obj_set_style_bg_opa(s_sleep, LV_OPA_TRANSP, 0);
        else set_shown(s_sleep, false);
#ifdef ESP_PLATFORM
        ESP_LOGI("sleep", "awake (%s)", s_st.link != LINK_SEARCHING ? "camera" : s_sleep_touch ? "touch" : "REC button or Wi-Fi update");
#endif
    }
}

static void tick_cb(lv_timer_t *t)
{
    static uint32_t slow;
    const uint32_t now = lv_tick_get();
    camera_get(&s_st);
    track_recording(now);
    xl_tick(now);
    if ((s_st.recording || !s_st.connected || !unlocked()) && s_ed_tile >= 0) editor_close();
    if (s_st.recording) { settings_close(); presets_close(); }
    if (s_st.seq != s_seq_drawn) {
        s_seq_drawn = s_st.seq;
        update_values();
    }
    update_status(now);
    frame_tick(now);
    sleep_tick(now);
    if (now - slow >= BATT_TICK_MS) {
        slow = now;
        batt_tick();
        bg_sample(now);
        batt_draw();
        settings_render();
        presets_render();
    }
}

// Frame timing for the About page.
static void refr_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RENDER_START) { s_refr_t0 = plat_us(); return; }
    const uint32_t us = (uint32_t)(plat_us() - s_refr_t0);
    s_frame_us = s_frame_us ? (s_frame_us * 7 + us) / 8 : us;
    s_frames++;
    const uint32_t now = plat_ms();
    if (now - s_frames_t0 >= 1000) {
        s_fps = s_frames * 1000 / (now - s_frames_t0);
#ifdef ESP_PLATFORM
        ESP_LOGI("display", "%u frames/s, %.1f ms a frame", (unsigned)s_fps, s_frame_us / 1000.0);
#endif
        s_frames = 0;
        s_frames_t0 = now;
    }
}

// ---------- construction ----------
// A plain container. Not clickable (touches pass through to whatever holds it); the ones that
// take touches add LV_OBJ_FLAG_CLICKABLE themselves.
static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *caption(lv_obj_t *parent, const char *s)
{
    lv_obj_t *l = text(parent, &font_label_20, C_TEXT2);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *round_button(lv_obj_t *parent, const char *glyph, const lv_font_t *f, lv_color_t bg)
{
    lv_obj_t *b = box(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, C_BTN_PR, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = text(b, f, C_TEXT);
    lv_label_set_text(l, glyph);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *pill(lv_obj_t *parent, const char *label, lv_color_t bg)
{
    lv_obj_t *b = box(parent);
    lv_obj_set_size(b, 150, 52);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, C_BTN_PR, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = text(b, &font_label_20, C_TEXT);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *hairline(lv_obj_t *parent, int w)
{
    lv_obj_t *r = box(parent);
    lv_obj_set_size(r, w, 1);
    lv_obj_set_style_bg_color(r, C_RULE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

static void build_row(row_t *r, lv_obj_t *parent, int W, int index)
{
    r->box = box(parent);
    lv_obj_set_size(r->box, W, 200);
    r->label = caption(r->box, "");
    lv_obj_set_style_text_letter_space(r->label, 2, 0);
    lv_obj_align(r->label, LV_ALIGN_TOP_MID, 0, 0);
    // 200 px row: caption 0-24, value 30-145 (115 px font), second line 150-194.
    r->value = text(r->box, &font_big_132, C_TEXT);
    lv_obj_align(r->value, LV_ALIGN_TOP_MID, 0, 30);
    r->sub = text(r->box, &font_mid_36, C_TEXT2);
    lv_obj_align(r->sub, LV_ALIGN_TOP_MID, 0, 150);
    r->minus = round_button(r->box, ICON_MINUS, &font_icons_48, C_BTN);
    lv_obj_align(r->minus, LV_ALIGN_LEFT_MID, 28, -13);
    lv_obj_add_event_cb(r->minus, step_cb, LV_EVENT_ALL, (void *)(intptr_t)(index * 2));
    r->plus = round_button(r->box, ICON_PLUS, &font_icons_48, C_BTN);
    lv_obj_align(r->plus, LV_ALIGN_RIGHT_MID, -28, -13);
    lv_obj_add_event_cb(r->plus, step_cb, LV_EVENT_ALL, (void *)(intptr_t)(index * 2 + 1));
}

// ----- settings rows -----
#define SET_X   252                     // content column
#define SET_W   (800 - SET_X - 24)
#define ROW_H   96
#define INFO_H  54

static lv_obj_t *srow(lv_obj_t *sec, int *y, int h, const char *label, const char *desc)
{
    lv_obj_t *r = box(sec);
    lv_obj_set_size(r, SET_W, h);
    lv_obj_set_pos(r, 0, *y);
    lv_obj_t *l = text(r, &font_label_20, C_TEXT);
    lv_label_set_text(l, label);
    if (desc) {
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 16);
        lv_obj_t *d = text(r, &font_label_14, C_TEXT2);
        lv_obj_set_width(d, 226);
        lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
        lv_label_set_text(d, desc);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, 44);
    } else {
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_align(hairline(r, SET_W), LV_ALIGN_BOTTOM_LEFT, 0, 0);
    *y += h;
    return r;
}

static void sseg_row(lv_obj_t *sec, int *y, const char *label, const char *desc, int id,
                     int n, const char *const opts[], const int vals[])
{
    lv_obj_t *r = srow(sec, y, ROW_H, label, desc);
    sseg_t *g = &s_ssegs[s_nssegs];
    g->id = id;
    g->n = n;
    const int bw = n == 2 ? 136 : 92, pad = 4;
    lv_obj_t *track = box(r);
    lv_obj_set_size(track, n * bw + 2 * pad, 48);
    lv_obj_align(track, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(track, C_BTN, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = box(track);
        lv_obj_set_size(b, bw, 40);
        lv_obj_set_pos(b, pad + i * bw, pad);
        lv_obj_set_style_bg_color(b, C_SEG_ON, 0);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(b, 4);
        lv_obj_add_event_cb(b, sseg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(s_nssegs * 4 + i));
        g->lbl[i] = text(b, &font_label_20, C_TEXT2);
        lv_label_set_text(g->lbl[i], opts[i]);
        lv_obj_center(g->lbl[i]);
        g->btn[i] = b;
        g->val[i] = vals[i];
    }
    s_nssegs++;
}

static void sstep_row(lv_obj_t *sec, int *y, const char *label, const char *desc, int id, int lo, int hi, int step)
{
    lv_obj_t *r = srow(sec, y, ROW_H, label, desc);
    sstep_t *s = &s_ssteps[s_nssteps];
    *s = (sstep_t){ .id = id, .lo = lo, .hi = hi, .step = step };
    lv_obj_t *plus = round_button(r, "+", &font_mid_36, C_BTN);
    lv_obj_set_size(plus, rw(56), 56);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(plus, sstep_cb, LV_EVENT_ALL, (void *)(intptr_t)(s_nssteps * 2 + 1));
    s->val = text(r, &font_mid_36, C_TEXT);
    lv_obj_set_width(s->val, 136);
    lv_obj_set_style_text_align(s->val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s->val, LV_ALIGN_RIGHT_MID, -(rw(56) + 4), 0);
    lv_obj_t *minus = round_button(r, MINUS, &font_mid_36, C_BTN);
    lv_obj_set_size(minus, rw(56), 56);
    lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -(rw(56) + 4 + 136 + 4), 0);
    lv_obj_add_event_cb(minus, sstep_cb, LV_EVENT_ALL, (void *)(intptr_t)(s_nssteps * 2));
    s_nssteps++;
}

static void info_row(lv_obj_t *sec, int *y, const char *label, int which)
{
    lv_obj_t *r = srow(sec, y, INFO_H, label, NULL);
    s_info[which] = text(r, &font_label_20, C_TEXT);
    lv_obj_align(s_info[which], LV_ALIGN_RIGHT_MID, 0, 0);
}

static void act_row(lv_obj_t *sec, int *y, const char *label, const char *desc, int which)
{
    lv_obj_t *r = srow(sec, y, ROW_H, label, desc);
    lv_obj_t *b = pill(r, "", C_BTN);
    lv_obj_set_size(b, 236, 52);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, act_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);
    s_act_lbl[which] = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_color(s_act_lbl[which], C_RED, 0);
}

static void build_settings(lv_obj_t *scr, int W, int H)
{
    s_set = box(scr);
    lv_obj_set_size(s_set, W, H);
    lv_obj_set_style_bg_color(s_set, C_BG, 0);
    lv_obj_set_style_bg_opa(s_set, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_set, LV_OBJ_FLAG_CLICKABLE);              // swallow touches behind it

    lv_obj_t *done = pill(s_set, "Done", C_BLUE);
    lv_obj_set_size(done, 200, 50);
    lv_obj_set_pos(done, 16, 16);
    lv_obj_add_event_cb(done, set_done_cb, LV_EVENT_CLICKED, NULL);
    static const char *PAGES[NPAGES] = { "Recording", "Display", "Battery", "Camera", "Takes", "Update", "About" };
    for (int i = 0; i < NPAGES; i++) {
        lv_obj_t *b = box(s_set);
        lv_obj_set_size(b, 200, 46);
        lv_obj_set_pos(b, 16, 80 + i * 54);
        lv_obj_set_style_bg_color(b, C_BTN, 0);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_nav_lbl[i] = text(b, &font_label_20, C_TEXT2);
        lv_label_set_text(s_nav_lbl[i], PAGES[i]);
        lv_obj_align(s_nav_lbl[i], LV_ALIGN_LEFT_MID, 16, 0);
        s_nav[i] = b;
        s_sec[i] = box(s_set);
        lv_obj_set_size(s_sec[i], SET_W, H - 24);
        lv_obj_set_pos(s_sec[i], SET_X, 12);
    }
    lv_obj_t *rule = box(s_set);                                 // between the menu and the page
    lv_obj_set_size(rule, 1, H - 32);
    lv_obj_set_pos(rule, 232, 16);
    lv_obj_set_style_bg_color(rule, C_RULE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    int y;
    static const char *const ONOFF[] = { "On", "Off" };
    static const int ONOFF_V[] = { 1, 0 };

    y = 0;                                                       // Recording
    static const char *const HOLD[] = { "Press", "Hold to stop" };
    static const int HOLD_V[] = { 0, 1 };
    sseg_row(s_sec[PG_REC], &y, "REC button", "Hold to stop: ending a take needs a half-second hold.",
             SID_REC_HOLD, 2, HOLD, HOLD_V);
    static const char *const TAL[] = { "Thick", "Thin", "Off" };
    static const int TAL_V[] = { TALLY_THICK, TALLY_THIN, TALLY_OFF };
    sseg_row(s_sec[PG_REC], &y, "Record frame", "Red border round the screen while recording.", SID_TALLY, 3, TAL, TAL_V);
    sseg_row(s_sec[PG_REC], &y, "Clip timer", "How long the take has been rolling.", SID_CLIP, 2, ONOFF, ONOFF_V);

    y = 0;                                                       // Display
    static const char *const NIGHT[] = { "Normal", "Night" };
    static const int NIGHT_V[] = { 0, 1 };
    sseg_row(s_sec[PG_DISPLAY], &y, "Colours", "Night: everything in dim red, for dark sets.", SID_NIGHT, 2, NIGHT, NIGHT_V);
    static const char *const FLIP[] = { "USB at rear", "USB at front" };
    static const int FLIP_V[] = { 1, 0 };
    sseg_row(s_sec[PG_DISPLAY], &y, "Orientation", "The board restarts to apply it.", SID_FLIP, 2, FLIP, FLIP_V);
    static const char *const LOCKS[] = { "Unlocked", "Locked" };
    static const int LOCKS_V[] = { 0, 1 };
    sseg_row(s_sec[PG_DISPLAY], &y, "At start-up", "Whether editing starts unlocked or locked.", SID_START_LOCK, 2, LOCKS, LOCKS_V);
    static const char *const SLEEPS[] = { "1 min", "5 min", "Never" };
    static const int SLEEPS_V[] = { SLEEP_1MIN, SLEEP_5MIN, SLEEP_NEVER };
    sseg_row(s_sec[PG_DISPLAY], &y, "Sleep", "Screen off with no camera for this long. Touch to wake.",
             SID_SLEEP, 3, SLEEPS, SLEEPS_V);

    y = 0;                                                       // Battery
    sstep_row(s_sec[PG_BATT], &y, "Low warning", "Turns amber below this.", SID_BATT_LOW, 11500, 16500, 100);
    sstep_row(s_sec[PG_BATT], &y, "Critical", "Turns red and the frame flashes.", SID_BATT_CRIT, 10500, 16300, 100);
    static const char *const BS[] = { "Volts", "Percent" };
    static const int BS_V[] = { BATT_SHOW_VOLTS, BATT_SHOW_PERCENT };
    sseg_row(s_sec[PG_BATT], &y, "Show", "Percent is estimated from the voltage.", SID_BATT_SHOW, 2, BS, BS_V);
    {                                                            // the last 2 hours, low / critical lines
        lv_obj_t *cap = text(s_sec[PG_BATT], &font_label_14, C_TEXT2);
        lv_label_set_text(cap, "LAST 2 HOURS");
        lv_obj_set_style_text_letter_space(cap, 1, 0);
        lv_obj_set_pos(cap, 0, y + 12);
        s_chart_left = text(s_sec[PG_BATT], &font_label_14, C_TEXT2);
        lv_obj_set_width(s_chart_left, 320);
        lv_obj_set_style_text_align(s_chart_left, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s_chart_left, SET_W - 320, y + 12);
        const int cy = y + 36, ch = H - 24 - cy - 8;
        s_chart = lv_chart_create(s_sec[PG_BATT]);
        lv_obj_remove_style_all(s_chart);
        lv_obj_set_size(s_chart, SET_W, ch);
        lv_obj_set_pos(s_chart, 0, cy);
        lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(s_chart, C_TILE, 0);
        lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_chart, 10, 0);
        lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
        lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);   // no point markers
        lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_div_line_count(s_chart, 0, 0);
        lv_chart_set_point_count(s_chart, BG_POINTS);
        lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, BG_MIN_MV, BG_MAX_MV);
        s_chart_ser = lv_chart_add_series(s_chart, C_TEXT, LV_CHART_AXIS_PRIMARY_Y);
        for (int i = 0; i < BG_POINTS; i++) if (i < BG_POINTS - s_bg_n) s_bg[i] = LV_CHART_POINT_NONE;
        lv_chart_set_ext_y_array(s_chart, s_chart_ser, s_bg);
        s_bg_drawn = UINT32_MAX;
        s_chart_lo = hairline(s_sec[PG_BATT], SET_W);
        lv_obj_set_style_bg_color(s_chart_lo, C_ORANGE, 0);
        lv_obj_set_style_bg_opa(s_chart_lo, LV_OPA_60, 0);
        s_chart_crit = hairline(s_sec[PG_BATT], SET_W);
        lv_obj_set_style_bg_color(s_chart_crit, C_RED, 0);
        lv_obj_set_style_bg_opa(s_chart_crit, LV_OPA_60, 0);
    }

    y = 0;                                                       // Camera
    info_row(s_sec[PG_CAMERA], &y, "Camera", INFO_CAM_NAME);
    info_row(s_sec[PG_CAMERA], &y, "Bluetooth", INFO_CAM_LINK);
    info_row(s_sec[PG_CAMERA], &y, "Lens", INFO_LENS);
    static const char *const WBS[] = { "50K", "100K", "200K" };
    static const int WBS_V[] = { 50, 100, 200 };
    sseg_row(s_sec[PG_CAMERA], &y, "White balance step", "How far each press moves it.", SID_WB_STEP, 3, WBS, WBS_V);
    static const char *const XL[] = { "Off", "Keep speed" };
    static const int XL_V[] = { 0, 1 };
    sseg_row(s_sec[PG_CAMERA], &y, "Exposure lock", "Changing FPS moves the angle to keep the shutter speed.",
             SID_EXP_LOCK, 2, XL, XL_V);
    act_row(s_sec[PG_CAMERA], &y, "Pairing", "Forget this camera to pair a different one.", ACT_FORGET);

    y = 0;                                                       // Takes
    {
        lv_obj_t *r = srow(s_sec[PG_TAKES], &y, ROW_H, "", NULL);
        s_info[INFO_TAKES] = lv_obj_get_child(r, 0);
        lv_obj_t *b = pill(r, "", C_BTN);
        lv_obj_set_size(b, 200, 52);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(b, act_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_CLEAR_TAKES);
        s_act_lbl[ACT_CLEAR_TAKES] = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_color(s_act_lbl[ACT_CLEAR_TAKES], C_RED, 0);
        static const char *HEAD[3] = { "TAKE", "STARTED AT", "LENGTH" };
        static const int CX[3] = { 0, 90, 300 };
        for (int c = 0; c < 3; c++) {
            lv_obj_t *h = text(s_sec[PG_TAKES], &font_label_14, C_TEXT2);
            lv_obj_set_style_text_letter_space(h, 1, 0);
            lv_label_set_text(h, HEAD[c]);
            lv_obj_set_pos(h, CX[c], y + 14);
            s_take_col[c] = text(s_sec[PG_TAKES], &font_label_20, c ? C_TEXT : C_TEXT2);
            lv_obj_set_style_text_line_space(s_take_col[c], 6, 0);
            lv_obj_set_pos(s_take_col[c], CX[c], y + 40);
        }
    }

    y = 0;                                                       // Update
    {
        lv_obj_t *r = srow(s_sec[PG_UPDATE], &y, ROW_H, "Wi-Fi update",
                           "New firmware from a phone or Mac, no cables.");
        lv_obj_t *b = pill(r, "Start", C_BLUE);
        lv_obj_set_size(b, 200, 52);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(b, upd_cb, LV_EVENT_CLICKED, NULL);
        s_upd_btn_lbl = lv_obj_get_child(b, 0);
    }
    info_row(s_sec[PG_UPDATE], &y, "1. Join Wi-Fi", INFO_UPD_NET);
    info_row(s_sec[PG_UPDATE], &y, "Password", INFO_UPD_PASS);
    info_row(s_sec[PG_UPDATE], &y, "2. Open", INFO_UPD_URL);
    info_row(s_sec[PG_UPDATE], &y, "Status", INFO_UPD_STATUS);
    info_row(s_sec[PG_UPDATE], &y, "Running", INFO_UPD_SLOT);

    y = 0;                                                       // About
    info_row(s_sec[PG_ABOUT], &y, "Firmware", INFO_FW);
    info_row(s_sec[PG_ABOUT], &y, "Link", INFO_BT);
    info_row(s_sec[PG_ABOUT], &y, "Timecode", INFO_TC);
    info_row(s_sec[PG_ABOUT], &y, "Display", INFO_DISP);
    info_row(s_sec[PG_ABOUT], &y, "Free memory", INFO_MEM);
    info_row(s_sec[PG_ABOUT], &y, "Running", INFO_UP);
    act_row(s_sec[PG_ABOUT], &y, "Defaults", "Put every setting back as it came.", ACT_RESET);

    lv_obj_add_flag(s_set, LV_OBJ_FLAG_HIDDEN);
}

// Presets: six cards, 3 x 2.
static void build_presets(lv_obj_t *scr, int W, int H)
{
    s_pre = box(scr);
    lv_obj_set_size(s_pre, W, H);
    lv_obj_set_style_bg_color(s_pre, C_BG, 0);
    lv_obj_set_style_bg_opa(s_pre, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_pre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = text(s_pre, &font_mid_36, C_TEXT);
    lv_label_set_text(t, "Presets");
    lv_obj_set_pos(t, 28, 20);
    s_pre_hint = text(s_pre, &font_label_14, C_TEXT2);
    lv_label_set_text(s_pre_hint, PRE_HINT);
    lv_obj_set_pos(s_pre_hint, 30, 66);
    lv_obj_t *done = pill(s_pre, "Done", C_BLUE);
    lv_obj_set_pos(done, W - 24 - 150, 20);
    lv_obj_add_event_cb(done, pre_done_cb, LV_EVENT_CLICKED, NULL);
    const int top = 96, cw = (W - 2 * 24 - 2 * GAP) / 3, chh = (H - top - 16 - GAP) / 2;
    for (int i = 0; i < NPRESETS; i++) {
        lv_obj_t *c = box(s_pre);
        lv_obj_set_size(c, cw, chh);
        lv_obj_set_pos(c, 24 + (i % 3) * (cw + GAP), top + (i / 3) * (chh + GAP));
        lv_obj_set_style_bg_color(c, C_TILE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, C_TILE_PR, LV_STATE_PRESSED);
        lv_obj_set_style_radius(c, RADIUS, 0);
        lv_obj_set_style_border_width(c, 3, 0);
        lv_obj_set_style_border_color(c, C_BLUE, 0);
        lv_obj_set_style_border_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, pre_card_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        char n[2] = { (char)('A' + i), 0 };
        lv_obj_t *letter = caption(c, n);
        lv_obj_set_pos(letter, 18, 14);
        s_pre_l1[i] = text(c, &font_mid_36, C_TEXT);
        lv_obj_set_pos(s_pre_l1[i], 18, 50);
        s_pre_l2[i] = text(c, &font_label_20, C_TEXT2);
        lv_obj_set_pos(s_pre_l2[i], 18, 104);
        s_pre_l3[i] = text(c, &font_label_20, C_TEXT2);
        lv_obj_set_pos(s_pre_l3[i], 18, 132);
        s_pre_card[i] = c;
    }
    lv_obj_add_flag(s_pre, LV_OBJ_FLAG_HIDDEN);
}

static void build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const int W = lv_display_get_horizontal_resolution(NULL);
    const int H = lv_display_get_vertical_resolution(NULL);
    const int tw = (W - 2 * MARGIN - 2 * GAP) / 3;
    const int th = (H - 2 * MARGIN - GAP) / 2;
    const int x2 = MARGIN + tw + GAP, x3 = MARGIN + 2 * (tw + GAP), y2 = MARGIN + th + GAP;

    const int short_h = th - LOCK_H - GAP;                         // WB and timecode tiles
    for (int t = 0; t < T_COUNT; t++) {
        lv_obj_t *tile = box(scr);
        lv_obj_set_size(tile, tw, t == T_WB ? short_h : th);
        lv_obj_set_pos(tile, MARGIN + (t % 3) * (tw + GAP), MARGIN + (t / 3) * (th + GAP));
        lv_obj_set_style_bg_color(tile, C_TILE, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, C_TILE_PR, LV_STATE_PRESSED);
        lv_obj_set_style_radius(tile, RADIUS, 0);
        lv_obj_set_style_border_width(tile, OUTLINE, 0);          // constant, so nothing shifts
        lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        s_tile[t] = tile;

        s_title[t] = caption(tile, TITLES[t]);
        lv_obj_align(s_title[t], LV_ALIGN_TOP_MID, 0, 16 - OUTLINE);
        s_val[t] = text(tile, &font_value_64, C_TEXT);
        lv_obj_align(s_val[t], LV_ALIGN_CENTER, 0, 0);
        s_sub[t] = caption(tile, "");
        lv_obj_align(s_sub[t], LV_ALIGN_BOTTOM_MID, 0, -(16 - OUTLINE));
        lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_ALL, (void *)(intptr_t)t);
    }

    // WB tile, laid out like the timecode tile beside it: the value on top, "WB · TINT +2" under it.
    lv_obj_align(s_val[T_WB], LV_ALIGN_TOP_MID, 0, 6 - OUTLINE);
    lv_obj_t *wb_row = box(s_tile[T_WB]);
    lv_obj_set_size(wb_row, LV_SIZE_CONTENT, 26);
    lv_obj_set_flex_flow(wb_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wb_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wb_row, 10, 0);
    lv_obj_align(wb_row, LV_ALIGN_BOTTOM_MID, 0, -(14 - OUTLINE));
    lv_obj_set_parent(s_title[T_WB], wb_row);
    lv_label_set_text(s_title[T_WB], "WB  " MIDDOT "  TINT");
    lv_obj_set_parent(s_sub[T_WB], wb_row);

    // ---- Timecode tile: two columns wide, above the lock track and the battery strip ----
    const int tc_w = 2 * tw + GAP, tc_h = short_h;
    s_tc_tile = box(scr);
    lv_obj_set_size(s_tc_tile, tc_w, tc_h);
    lv_obj_set_pos(s_tc_tile, x2, y2);
    lv_obj_set_style_bg_color(s_tc_tile, C_TILE, 0);
    lv_obj_set_style_bg_opa(s_tc_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_tc_tile, RADIUS, 0);

    // Fixed-width cells, placed by hand (no layout pass), so digits never shuffle as they count.
    int dig_w = 0;
    for (char c = '0'; c <= '9'; c++) {
        const int g = lv_font_get_glyph_width(&font_value_64, c, 0);
        if (g > dig_w) dig_w = g;
    }
    const int col_w = lv_font_get_glyph_width(&font_value_64, ':', 0) + 4;
    const int lbl_gap = 10;
    const int lbl_w = lv_text_get_width("TC", 2, &font_label_14, 1);
    const int tc_text_w = lbl_w + lbl_gap + 8 * dig_w + 3 * col_w;
    const int tc_y = 6;                                           // label box top (78 px line)
    int x = (tc_w - tc_text_w) / 2;
    s_tc_lbl = text(s_tc_tile, &font_label_14, C_TEXT2);
    lv_obj_set_style_text_letter_space(s_tc_lbl, 1, 0);
    lv_label_set_text(s_tc_lbl, "TC");
    lv_obj_set_pos(s_tc_lbl, x, tc_y + 34);                       // on the digits' cap height
    x += lbl_w + lbl_gap;
    for (int i = 0; i < 11; i++) {
        const int w = i % 3 == 2 ? col_w : dig_w;
        s_tc_cell[i] = text(s_tc_tile, &font_value_64, C_STALE);
        lv_obj_set_size(s_tc_cell[i], w, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(s_tc_cell[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_tc_cell[i], x, tc_y);
        x += w;
    }

    // Bottom line: status (left), card slots (right).
    const int half_w = 116, slot_w = 2 * half_w + 1 + 2 * 12;
    lv_obj_t *st_row = box(s_tc_tile);
    lv_obj_set_size(st_row, tc_w - 2 * TC_PAD - slot_w - 8, 26);
    lv_obj_set_flex_flow(st_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(st_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(st_row, 8, 0);
    lv_obj_set_pos(st_row, TC_PAD, tc_h - 14 - 26);
    s_status_dot = box(st_row);
    lv_obj_set_size(s_status_dot, rw(12), 12);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, C_RED, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_status_dot, LV_OBJ_FLAG_HIDDEN);
    s_status = caption(st_row, "");

    lv_obj_t *slot_row = box(s_tc_tile);
    lv_obj_set_size(slot_row, slot_w, 26);
    lv_obj_set_flex_flow(slot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slot_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(slot_row, 12, 0);
    lv_obj_set_pos(slot_row, tc_w - TC_PAD - slot_w, tc_h - 14 - 26);
    for (int i = 0; i < 2; i++) {
        if (i) {                                                   // short divider between the slots
            lv_obj_t *div = box(slot_row);
            lv_obj_set_size(div, 1, 18);
            lv_obj_set_style_bg_color(div, C_RULE, 0);
            lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        }
        lv_obj_t *half = box(slot_row);                            // fixed width, so nothing moves
        lv_obj_set_size(half, half_w, 26);
        lv_obj_set_flex_flow(half, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(half, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(half, 1, 0);
        s_slot_icon[i] = text(half, &font_icons_22, C_STALE);
        lv_label_set_text(s_slot_icon[i], ICON_CARD);
        s_slot_num[i] = text(half, &font_label_20, C_STALE);
        lv_label_set_text(s_slot_num[i], i ? "2" : "1");
        lv_obj_set_style_margin_right(s_slot_num[i], 7, 0);
        s_slot_lbl[i] = text(half, &font_label_20, C_STALE);
        lv_label_set_text(s_slot_lbl[i], MINUS MINUS);
    }

    // ---- Lock track ----
    s_lk = box(scr);
    lv_obj_set_size(s_lk, tw, LOCK_H);
    lv_obj_set_pos(s_lk, x2, H - MARGIN - LOCK_H);
    lv_obj_set_style_bg_color(s_lk, C_TILE, 0);
    lv_obj_set_style_bg_opa(s_lk, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_lk, LV_RADIUS_CIRCLE, 0);
    // No clip_corner here: it makes LVGL draw the knob and caption into a separate ARGB layer and
    // mask it every frame, which is what made the slide look jerky. Nothing reaches the rounded
    // ends anyway, and the tint below has its own matching radius.
    lv_obj_add_flag(s_lk, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_lk, lock_cb, LV_EVENT_ALL, NULL);

    s_lk_fill = box(s_lk);                                        // tint while locked
    lv_obj_set_size(s_lk_fill, tw, LOCK_H);
    lv_obj_set_style_bg_color(s_lk_fill, C_ORANGE, 0);
    lv_obj_set_style_bg_opa(s_lk_fill, LV_OPA_20, 0);
    lv_obj_set_style_radius(s_lk_fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(s_lk_fill, LV_OBJ_FLAG_HIDDEN);

    const int kpad = 6, knob = LOCK_H - 2 * kpad, knob_w = rw(knob);
    s_lk_min = rw(kpad);
    s_lk_max = tw - s_lk_min - knob_w;
    s_lk_knob_w = knob_w;

    s_lk_lbl = text(s_lk, &font_label_20, C_TEXT2);
    lv_obj_set_width(s_lk_lbl, s_lk_max - s_lk_min);
    lv_obj_set_style_text_align(s_lk_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_lk_knob = box(s_lk);
    lv_obj_set_size(s_lk_knob, knob_w, knob);
    lv_obj_set_pos(s_lk_knob, s_lk_min, kpad);
    lv_obj_set_style_bg_opa(s_lk_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_lk_knob, LV_RADIUS_CIRCLE, 0);
    s_lk_icon = text(s_lk_knob, &font_icons_28, C_BG);
    lock_sync(false);

    // ---- Presets and Settings buttons (bottom left, under the WB tile): bookmarks and gear icons ----
    const int hb = (tw - GAP) / 2;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = box(scr);
        lv_obj_set_size(b, hb, LOCK_H);
        lv_obj_set_pos(b, MARGIN + i * (hb + GAP), H - MARGIN - LOCK_H);
        lv_obj_set_style_bg_color(b, C_TILE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, C_TILE_PR, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, i ? bat_cb : tc_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_t *l = text(b, &font_icons_28, C_TEXT);
        lv_label_set_text(l, i ? ICON_GEAR : ICON_PRESETS);
        lv_obj_center(l);
        if (i) s_btn_set = l; else s_btn_pre = l;
    }

    // ---- Battery strip ----
    s_bat = box(scr);
    lv_obj_set_size(s_bat, tw, LOCK_H);
    lv_obj_set_pos(s_bat, x3, H - MARGIN - LOCK_H);
    lv_obj_set_style_bg_color(s_bat, C_TILE, 0);
    lv_obj_set_style_bg_opa(s_bat, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_bat, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *brow = box(s_bat);                                  // icon and reading, centred
    lv_obj_set_size(brow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 12, 0);
    lv_obj_center(brow);
    s_batt_icon = text(brow, &font_icons_22, C_STALE);
    lv_label_set_text(s_batt_icon, ICON_BATT_0);
    s_batt_lbl = caption(brow, MINUS MINUS);
    lv_obj_set_style_text_letter_space(s_batt_lbl, 0, 0);
    lv_obj_set_style_text_color(s_batt_lbl, C_STALE, 0);

    // ---- Editor overlay ----
    s_editor = box(scr);
    lv_obj_set_size(s_editor, W, H);
    lv_obj_set_style_bg_color(s_editor, C_BG, 0);
    lv_obj_set_style_bg_opa(s_editor, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_CLICKABLE);              // swallow touches behind it
    build_row(&s_row[0], s_editor, W, 0);
    build_row(&s_row[1], s_editor, W, 1);
    s_done = pill(s_editor, "Done", C_BTN);
    lv_obj_align(s_done, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(s_done, done_cb, LV_EVENT_CLICKED, NULL);
    s_custom = pill(s_editor, "Custom", C_BTN);
    lv_obj_align(s_custom, LV_ALIGN_BOTTOM_MID, -85, -20);
    lv_obj_add_event_cb(s_custom, custom_cb, LV_EVENT_CLICKED, NULL);

    // Quick off-speed rates for the FPS editor.
    s_chips = box(s_editor);
    lv_obj_set_size(s_chips, W - 40, 56);
    lv_obj_align(s_chips, LV_ALIGN_BOTTOM_MID, 0, -92);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chips, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < NCHIPS; i++) {
        lv_obj_t *c = box(s_chips);
        lv_obj_set_size(c, 76, 52);
        lv_obj_set_style_bg_color(c, C_BTN, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, C_BTN_PR, LV_STATE_PRESSED);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)CHIPS[i]);
        char txt[8];
        if (CHIPS[i]) snprintf(txt, sizeof txt, "%d", CHIPS[i]);
        else          snprintf(txt, sizeof txt, "OFF");
        s_chip_lbl[i] = text(c, &font_label_20, C_TEXT);
        lv_label_set_text(s_chip_lbl[i], txt);
        lv_obj_center(s_chip_lbl[i]);
        s_chip[i] = c;
    }

    // ANGLE | SPEED switch for the shutter editor (iOS-style segmented control).
    s_seg = box(s_editor);
    lv_obj_set_size(s_seg, 280, 46);
    lv_obj_align(s_seg, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_seg, C_BTN, 0);
    lv_obj_set_style_bg_opa(s_seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_seg, LV_RADIUS_CIRCLE, 0);
    static const char *SEG[2] = { "ANGLE", "SPEED" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = box(s_seg);
        lv_obj_set_size(b, 136, 38);
        lv_obj_align(b, i ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, i ? -4 : 4, 0);
        lv_obj_set_style_bg_color(b, C_SEG_ON, 0);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, seg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_seg_lbl[i] = caption(b, SEG[i]);
        lv_obj_center(s_seg_lbl[i]);
        s_seg_btn[i] = b;
    }

    // Exact entry (inside the editor, so it closes with it).
    s_num = box(s_editor);
    lv_obj_set_size(s_num, W, H);
    lv_obj_set_style_bg_color(s_num, C_BG, 0);
    lv_obj_set_style_bg_opa(s_num, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_num, LV_OBJ_FLAG_CLICKABLE);
    s_num_title = text(s_num, &font_mid_36, C_TEXT);
    lv_obj_set_pos(s_num_title, 40, 64);
    s_num_hint = text(s_num, &font_label_20, C_TEXT2);
    lv_obj_set_width(s_num_hint, 340);
    lv_label_set_long_mode(s_num_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_num_hint, 40, 116);
    s_num_disp = text(s_num, &font_value_64, C_TEXT);
    lv_obj_set_pos(s_num_disp, 40, 186);
    s_num_err = text(s_num, &font_label_20, C_RED);
    lv_obj_set_width(s_num_err, 340);
    lv_label_set_long_mode(s_num_err, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_num_err, 40, 282);
    lv_obj_t *cancel = pill(s_num, "Cancel", C_BTN);
    lv_obj_set_pos(cancel, 40, 368);
    lv_obj_add_event_cb(cancel, num_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *set = pill(s_num, "Set", C_BLUE);
    lv_obj_set_pos(set, 206, 368);
    lv_obj_add_event_cb(set, num_set_cb, LV_EVENT_CLICKED, NULL);
    static const char *NKEYS[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "DEL" };
    {
        const int k = 88, kw = rw(k), kg = 16;
        const int kx = W - 40 - (3 * kw + 2 * kg), ky = (H - (4 * k + 3 * kg)) / 2;
        for (int i = 0; i < 12; i++) {
            lv_obj_t *b = i == 11 ? round_button(s_num, ICON_BACKSPACE, &font_icons_28, C_BTN)
                                  : round_button(s_num, NKEYS[i], &font_mid_36, C_BTN);
            lv_obj_set_size(b, kw, k);
            lv_obj_set_pos(b, kx + (i % 3) * (kw + kg), ky + (i / 3) * (k + kg));
            lv_obj_add_event_cb(b, num_key_cb, LV_EVENT_CLICKED, (void *)NKEYS[i]);
            if (i == 9) s_num_dot = b;
        }
    }
    lv_obj_add_flag(s_num, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);

    build_presets(scr, W, H);
    build_settings(scr, W, H);

    // ---- PIN overlay: heading and digits on the left, round keypad on the right ----
    s_pin = box(scr);
    lv_obj_set_size(s_pin, W, H);
    lv_obj_set_style_bg_color(s_pin, C_BG, 0);
    lv_obj_set_style_bg_opa(s_pin, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_pin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ph = text(s_pin, &font_mid_36, C_TEXT);
    lv_label_set_text(ph, "Pair with camera");
    lv_obj_set_pos(ph, 40, 110);
    lv_obj_t *pi = text(s_pin, &font_label_20, C_TEXT2);
    lv_obj_set_width(pi, 330);
    lv_label_set_long_mode(pi, LV_LABEL_LONG_WRAP);
    lv_label_set_text(pi, "Enter the 6-digit PIN shown on the camera's screen.");
    lv_obj_set_pos(pi, 40, 162);
    s_pin_digits = text(s_pin, &font_value_64, C_TEXT);
    lv_obj_set_pos(s_pin_digits, 40, 250);
    static const char *KEYS[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "OK" };
    const int k = 88, kw = rw(k), kg = 16;
    const int kx = W - 40 - (3 * kw + 2 * kg), ky = (H - (4 * k + 3 * kg)) / 2;
    for (int i = 0; i < 12; i++) {
        const bool del = !strcmp(KEYS[i], "DEL"), ok = !strcmp(KEYS[i], "OK");
        lv_obj_t *b = round_button(s_pin, del ? ICON_BACKSPACE : KEYS[i],
                                   del ? &font_icons_28 : ok ? &font_label_20 : &font_mid_36,
                                   ok ? C_BLUE : C_BTN);
        lv_obj_set_size(b, kw, k);
        lv_obj_set_pos(b, kx + (i % 3) * (kw + kg), ky + (i / 3) * (k + kg));
        lv_obj_add_event_cb(b, pin_key_cb, LV_EVENT_CLICKED, (void *)KEYS[i]);
    }
    lv_obj_add_flag(s_pin, LV_OBJ_FLAG_HIDDEN);

    // Above everything (top layer): the record frame.
    for (int i = 0; i < 4; i++) {
        s_tally[i] = box(lv_layer_top());
        lv_obj_set_style_bg_color(s_tally[i], C_TALLY, 0);
        lv_obj_set_style_bg_opa(s_tally[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_tally[i], LV_OBJ_FLAG_HIDDEN);
    }
    tally_layout();

    // Over even that: the sleep cover (black while asleep, clear for the touch that wakes it).
    s_sleep = box(lv_layer_top());
    lv_obj_set_size(s_sleep, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_sleep, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sleep, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sleep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_sleep, sleep_cover_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_sleep, sleep_cover_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_sleep, sleep_cover_cb, LV_EVENT_PRESS_LOST, NULL);
    set_shown(s_sleep, s_sleep_state != SL_AWAKE);
}

// Night mode changes every colour: rebuild all the screens with the new palette. Runs from
// lv_async_call, outside the event of the button that asked for it (which is deleted here).
static void rebuild_async(void *arg)
{
    (void)arg;
    s_pal = settings()->night ? &PAL_NIGHT : &PAL_DAY;
    lv_obj_clean(lv_layer_top());
    lv_obj_clean(lv_screen_active());
    s_nssegs = s_nssteps = 0;
    s_tally_w = -1;
    s_ed_tile = -1;
    s_lk_drag = false;
    build();
    force_redraw();
    settings_open();                                // back where the change was made
}

void bigsix_start(void)
{
    s_unlocked = !settings()->start_locked;
    s_pal = settings()->night ? &PAL_NIGHT : &PAL_DAY;
    camera_start();
    build();
    lv_display_t *d = lv_display_get_default();
    lv_display_add_event_cb(d, refr_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(d, refr_cb, LV_EVENT_RENDER_READY, NULL);
    recbutton_start();                 // physical REC button (GPIO6)
    lv_timer_create(tick_cb, 10, NULL);
}
