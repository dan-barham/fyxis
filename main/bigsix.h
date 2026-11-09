#pragma once

// Starts the camera link and builds the big-six screen on the active LVGL display.
// Call once, after the board's LCD, touch and LVGL are initialised, from the
// LVGL thread (or while holding the LVGL lock if the port uses one).
void bigsix_start(void);
