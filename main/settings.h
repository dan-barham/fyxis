// On-board settings, kept in flash (NVS) on the board and in memory in the Mac simulator.
// Also a few platform helpers the UI needs (clock, restart, memory), so bigsix.c stays portable.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BIGSIX_VERSION "2.1"

enum { TALLY_THICK, TALLY_THIN, TALLY_OFF };
enum { BATT_SHOW_VOLTS, BATT_SHOW_PERCENT };

typedef struct {
    uint8_t  rec_hold_stop;    // 1 = the REC button has to be held 0.5 s to stop (start is instant)
    uint8_t  tally;            // TALLY_*: red frame while recording
    uint8_t  clip_timer;       // 1 = show how long the take has been rolling
    uint8_t  flip;             // 1 = screen turned 180° (USB-C ports at the camera's rear)
    uint8_t  start_locked;     // 1 = editing locked at start-up
    uint8_t  batt_show;        // BATT_SHOW_*
    uint16_t batt_low_mv;      // amber below this
    uint16_t batt_crit_mv;     // red + flashing frame below this
    uint16_t wb_step;          // kelvin per white-balance step
    // --- added in 2.1 (older saved settings get the defaults for these) ---
    uint8_t  night;            // 1 = all-red night palette
} settings_t;

// Presets: a saved FPS / shutter / EI / WB + tint, applied in one tap.
#define NPRESETS 6
typedef struct {
    uint8_t  used;
    uint8_t  fps;              // recording rate, whole frames; the project rate = off-speed off
    uint16_t angle_x100;       // shutter angle
    uint16_t iso;
    uint16_t wb;
    int8_t   tint;
    uint8_t  reserved;
} preset_t;

// Loads the settings (initialising NVS first on the board). Call once, before anything reads them.
void settings_init(void);
// The live settings. Change fields through settings_changed() so they're saved.
const settings_t *settings(void);
settings_t *settings_edit(void);
// Saves after a short pause (so holding + doesn't write flash on every step).
void settings_changed(void);
void settings_reset(void);        // back to defaults (and saved)

preset_t *presets(void);          // NPRESETS slots
void presets_changed(void);       // saves them

// ---- platform helpers ----
uint32_t plat_ms(void);
uint64_t plat_us(void);
void plat_restart(void);          // saves settings first
uint32_t plat_free_internal(void);
uint32_t plat_free_psram(void);
