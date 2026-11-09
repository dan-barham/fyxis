#!/bin/bash
# Fetches the components this firmware needs into deps/ (from GitHub, pinned), and copies the
# Big Six sources from ../main into main/. Run once before `idf.py build`.
set -e
cd "$(dirname "$0")"
mkdir -p deps && cd deps
[ -d lvgl ] || git clone -q --depth 1 -b v9.3.0 https://github.com/lvgl/lvgl.git lvgl
if [ ! -d esp_lvgl_adapter ]; then                       # esp_lvgl_adapter 0.5.3
  git clone -q --filter=blob:none --no-checkout https://github.com/espressif/esp-iot-solution.git _iot
  (cd _iot && git fetch -q --filter=blob:none origin a70adf1 2>/dev/null || git fetch -q --filter=blob:none --shallow-since=2026-06-01 origin master)
  mkdir esp_lvgl_adapter
  (cd _iot && git archive a70adf1 components/display/tools/esp_lvgl_adapter) | tar -x -C esp_lvgl_adapter --strip-components=4
  rm -rf _iot esp_lvgl_adapter/test_apps esp_lvgl_adapter/benchmark
  # Use the local component names (the Espressif registry isn't used)
  sed -i.bak 's/list(APPEND ADAPTER_PRIV_REQ espressif__esp_lcd_touch)/list(APPEND ADAPTER_PRIV_REQ esp_lcd_touch)/' esp_lvgl_adapter/CMakeLists.txt
  perl -0pi -e 's/if\(NOT lvgl_name\)\n    set\(lvgl_name "lvgl__lvgl"\)/if(NOT lvgl_name)\n    set(lvgl_name "lvgl")/' esp_lvgl_adapter/CMakeLists.txt
  rm -f esp_lvgl_adapter/CMakeLists.txt.bak
  # Two fixes for a 180-degree picture in DOUBLE_DIRECT mode (see patches/): ask the panel for the
  # third buffer it needs, and rotate only the changed area (unpatched, the CPU path rotates the
  # whole screen for every changed area, about 200 ms a frame).
  patch -s -p1 -d esp_lvgl_adapter < ../patches/esp_lvgl_adapter-rotate180.patch
fi
if [ ! -d esp_lcd_touch ]; then                          # esp_lcd_touch + GT911 1.2.1
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/espressif/esp-bsp.git _bsp
  (cd _bsp && git sparse-checkout set components/lcd_touch/esp_lcd_touch components/lcd_touch/esp_lcd_touch_gt911)
  cp -r _bsp/components/lcd_touch/esp_lcd_touch _bsp/components/lcd_touch/esp_lcd_touch_gt911 .
  rm -rf _bsp esp_lcd_touch_gt911/test_apps
fi
cd ..
cp ../main/*.c ../main/*.h main/
echo "deps ready. Build with: IDF_COMPONENT_MANAGER=0 idf.py set-target esp32s3 build"
