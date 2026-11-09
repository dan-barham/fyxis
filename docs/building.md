# Flashing and building

> **Never connect a computer and the camera to the board at the same time.** The board has two USB-C
> ports (UART and USB). Use the UART port with a computer only while the camera cable is unplugged,
> and plug the camera back in only after the computer is disconnected.

## Flashing (no tools needed)

`fyxis-<version>-full.bin` from the [Releases page](https://github.com/dan-barham/fyxis/releases) is the complete image (bootloader,
partition table, app) to write at **0x0**. Use it for a first install. After that, update over Wi-Fi
(Settings › Update) with `fyxis-<version>-update.bin`.

1. With the camera cable unplugged, plug the board into the computer with the board's **UART** USB-C port (not the one marked USB).
2. In **Chrome**, open https://espressif.github.io/esptool-js/ (Espressif's own web flasher).
3. **Program** section: Baudrate **921600**, click **Connect**, pick the board's port in the pop-up
   (usually `cu.usbmodem…` or `cu.wchusbserial…`).
4. In the file row, change the address from **0x1000** (its default) to **0x0**, and choose
   `fyxis-<version>-full.bin`. Leave Flash Mode / Frequency / Size on **keep**. Click **Program** (about 30 s).
5. Press the board's **RESET** button. The screen shows FYXIS with **SEARCHING** under the timecode.

If **Connect** fails: hold **BOOT**, tap **RESET**, release **BOOT**, then Connect again (and press RESET
after programming). To see the board's log, click **Disconnect**, then in the **Console** section set
115200 and click **Start**.

## Building

The ESP-IDF project is in `firmware/` (based on Waveshare's `09_lvgl_v9_demo` board port).

1. Install **ESP-IDF v5.5.6 or later** (not plain v5.5: its NimBLE has an event-queue race that stops
   incoming Bluetooth data being processed, so the board stalls right after connecting and floods the
   log with `Failed to allocate buffer, retrying`). On macOS: clone it into `~/esp/esp-idf`, run
   `IDF_PYTHON=python3.13 ./install.sh esp32s3` (5.5 doesn't support Python 3.14), then
   `python tools/idf_tools.py install cmake ninja`.
2. `firmware/fetch_deps.sh` — fetches LVGL 9.3.0, Espressif's `esp_lvgl_adapter` 0.5.3 and the GT911
   touch driver from GitHub into `firmware/deps/`, and copies the sources from `main/` into
   `firmware/main/`. (Run it again after changing anything in `main/`.)
3. `cd firmware && IDF_COMPONENT_MANAGER=0 idf.py set-target esp32s3 build`
4. `idf.py -p /dev/cu.usbmodem… flash monitor`, or make a single image with
   `cd build && python -m esptool --chip esp32s3 merge_bin -o bigsix-firmware.bin @flash_args`.

Notes: two 6 MB app slots for Wi-Fi updates (`partitions.csv`; the app is about 1.6 MB). NVS stays at
0x9000 so re-flashing keeps the pairing and settings. LVGL allocates from the system
heap, so its larger blocks land in PSRAM (`CONFIG_LV_USE_CLIB_MALLOC`; with LVGL's own 64 KB pool the
screen runs out of memory). Settings come from two files, both read by `firmware/CMakeLists.txt`:
`firmware/sdkconfig.defaults` (Waveshare's flash, PSRAM and clock settings) and the root
`sdkconfig.defaults.append` (Bluetooth, LVGL memory, logging). After changing either, delete
`firmware/sdkconfig` before building so the new defaults are picked up.

**Mounting direction:** Settings › Display › Orientation (default: USB at rear, screen turned 180°).
The board restarts to apply it; `firmware/main/main.c` sets the picture and the touch from it.

**Display adapter patch:** `fetch_deps.sh` applies `firmware/patches/esp_lvgl_adapter-rotate180.patch`,
two fixes for a 180° picture in direct mode: the panel gets the third frame buffer it needs (unpatched,
the adapter asks for three only at 90° / 270°), and only the changed area is rotated (unpatched, on the
ESP32-S3, which has no rotation hardware, it rotates the whole screen for every changed area: about
200 ms a frame instead of about 20). The board logs `display: N frames/s, X ms a frame` once a second
while anything is redrawing.

**Version 2.1 changed the partition table**, so the first 2.1 flash has to be over UART with
`write-flash @flash_args` (it writes the new table, the slot selector and the app at 0x20000). After
that, Wi-Fi updates work.

**Re-flashing keeps the pairing only if you skip the gap.** The single image written at 0x0 also
overwrites the settings area (NVS, 0x9000), which erases the stored camera pairing, so the PIN is
needed again. For development, flash the three parts instead: `cd firmware/build && python -m esptool
--chip esp32s3 -p /dev/cu.usbmodem… -b 921600 write-flash @flash_args`.

## Under the hood: speed

- **Drawing:** a widget is only redrawn when its text or colour actually changes, so a timecode tick
  redraws two or three digits, not the whole screen. The display runs in LVGL's direct mode: only
  changed areas are copied (and turned 180°) into the panel's frame buffers. Before, every frame
  copied the whole unchanged rest of the screen between buffers (about 750 KB of PSRAM traffic).
  Drawing runs on core 1; Bluetooth, the panel refill interrupt and the REC button run on core 0.
- **State:** read every 10 ms (was 100 ms).
- **Bluetooth:** the board asks for a 7.5–15 ms connection interval (NimBLE's default is 30–50 ms) and no
  peripheral latency, and 251-byte link-layer packets. If the camera asks for slower timing it gets the
  fastest interval it offers. Settings › About shows what was agreed.
- **REC button:** interrupt-driven, acts on the first contact after a 1.5 ms spike check (was a 10 ms
  poll plus 20 ms debounce), from a high-priority task on core 0.
- Typeface: **Inter** (SIL Open Font License), pre-rendered at 14, 20, 26, 36, 64 and 132 px in `main/font_*.c`.
- Icons: **Tabler Icons**, outline (MIT licence, `licenses/Tabler-Icons-MIT.txt`), rendered as fonts:
  `font_icons_22.c` (battery, memory card), `font_icons_28.c` (lock, backspace), `font_icons_48.c` (minus, plus).


## Mac simulator (`sim/`)

Runs the **same UI code as the board** (`main/bigsix.c`) and the **same protocol code**
(`main/camproto.c`) in an 800 × 480 window, so anything you refine here carries straight over.

```
cd sim
./run.sh            # live: connects to the camera over Bluetooth, reads AND writes settings
./run.sh --demo     # no camera: fake values, for working on the layout
```

The first run installs SDL2 (Homebrew), downloads LVGL 9.3, and builds (about a minute).

- **Mouse = finger.** Drag the knob under the timecode to lock or unlock; click the battery strip for Settings.
- **Space bar = the physical REC button** (hold it for "Hold to stop").
- **Live mode:** the first connection triggers the camera's PIN; type it into the **macOS** pairing box
  (the on-screen keypad is for the board). Edits you make are **sent to the camera** — that's the point.
  Terminal shows `[bt] sent …` for every packet.
- **Demo mode:** a running timecode and card time; the space bar records. Type letters into the
  Terminal window to fake the cases that are hard to stage: `r` recording, `o` off-speed, `x` sensor area,
  `f` project rate, `l` link lost, `p` PIN keypad, `b` battery voltage (15.3 → 14.2 → 13.6 → 13.0 → 12.7 V), `c` card 2 in / empty, `e` eject / insert card 1.
- **Live mode, battery:** the Terminal prints `[batt] 15.31 V (other fields: …)` whenever the voltage
  moves by 0.1 V, so you can check it against the battery's own gauge. It also prints
  `[media] slots 2: 1429 / -1 s, active 0, …` whenever the card info changes: pull a card and
  check how an empty slot is reported.
- **Refining the UI:** edit `main/bigsix.c` (colours, fonts, layout are near the top and in `build()`),
  then re-run `./run.sh`. Only changed files rebuild.

- **Screenshots without a window:** `./bigsix_sim --demo --shots DIR "w 1000; s main; t 660 432; s settings"`
  runs a script (`w` wait ms, `t X Y` tap, `d`/`m`/`u` press, move, release, `k` REC press, `K MS` REC
  hold, `c L` demo letter, `s NAME` save `DIR/NAME.bmp`) and exits.

Files: `main_sim.c` (window) (stands in for the board's Bluetooth code),
`ble_bridge.py` (talks to the camera with CoreBluetooth via `bleak`), `lv_conf.h`, `Makefile`, `run.sh`.
