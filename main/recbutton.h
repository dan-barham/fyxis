#pragma once
#include <stdint.h>

// Physical REC button: a key switch on the board's sensor header. Not lit.
//
// Wiring:
//   GPIO6 (sensor header "AD") ── 1 kΩ ── switch ── GND
// GPIO6 uses its internal pull-up, so it reads low while the switch is pressed.
//
// Press = start / stop recording. The press acts on the first contact (an interrupt, not a
// poll), so there's no debounce delay before the command goes out; contact bounce is ignored
// afterwards. With "Hold to stop" on (settings), stopping needs a 0.5 s hold; starting is
// still instant.

// Firmware: configures GPIO6 and starts the button task. Simulator: does nothing.
void recbutton_start(void);

// Simulator (space bar) and the firmware task: the switch went down / came up.
void recbutton_down(void);
void recbutton_up(void);

// Bumps on every press, connected or not (wakes the screen from sleep).
uint32_t recbutton_press_seq(void);
// Bumps when a short press was ignored because "Hold to stop" is on (the UI shows a hint).
uint32_t recbutton_hint_seq(void);
// > 0 while the button is being held to stop: how far through the hold, 0-100.
int recbutton_hold_progress(void);
