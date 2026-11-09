// LVGL settings for the Mac simulator. Match the board where it matters:
// 16-bit colour, same fonts.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_MEM_SIZE (4 * 1024 * 1024)

#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_FULLSCREEN 0
#define LV_SDL_DIRECT_EXIT 1

#define LV_FONT_MONTSERRAT_14 1   // LVGL default; the UI itself uses the Inter fonts in ../main

#define LV_USE_LOG 0

#endif
