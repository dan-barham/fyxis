# FYXIS

A small touchscreen for the side of the Blackmagic PYXIS 12K. It shows the camera's main settings,
timecode, card time and battery, lets you change settings by touch, and has a REC button. It connects
to the camera over Bluetooth (Blackmagic's camera-control protocol) and is powered from the camera's
USB-C port. Nothing on the camera is modified. I built it for my own camera and I'm sharing it in case
it's useful to someone else.

![FYXIS](media/fyxis-hero.jpg)

![FYXIS in use](media/fyxis-demo.gif)

*The screen is the real firmware running in its simulator; the case is a render of the printable model.*

## What it does

- Shows FPS (including off-speed), shutter angle and speed, EI, white balance and tint.
- Shows timecode, time left on both cards, and battery voltage with an estimate of time left.
- Settings can be changed by touch (− / +, off-speed shortcuts, exact values on a keypad), and locked with a slider.
- A physical REC key on the case; a red border goes round the screen while recording. Optional
  "hold to stop".
- Six presets (FPS, shutter, EI, white balance and tint together), a take log, a red night mode, a
  battery graph, and firmware updates over Wi-Fi.
- Shows aperture and focal length if the camera reports them.
- Playback remote: play, pause, 2×, reverse and previous / next clip, with the clip's timecode.
- Exposure lock: change the frame rate and the shutter angle follows, so the brightness doesn't change.
- Goes to sleep (screen off, slow Bluetooth scan) when there's no camera, and wakes when it comes back.

| | | |
|---|---|---|
| ![main](media/screen-main.png) | ![recording](media/screen-recording.png) | ![editor](media/screen-editor.png) |
| Main screen | Recording | Shutter editor |
| ![presets](media/screen-presets.png) | ![battery](media/screen-battery.png) | ![night](media/screen-night.png) |
| Presets | Battery settings and graph | Night mode |

## What you need

- A **Waveshare ESP32-S3-Touch-LCD-4.3** (the 800 × 480 version, not the 4.3B).
- A short **USB-C cable with a right-angle plug**.
- For the REC key: a **Kailh Choc V2** switch, a **1 kΩ** resistor and two wires.
- A **3D printer** for the case, two **1/4"-20 × 3/8"** button-head screws, four **M2 × 5** screws and
  some 1 mm foam tape.

Full shopping list with quantities and what to look for: [docs/hardware.md](docs/hardware.md#shopping-list).

## Get started

> **Never connect a computer and the camera to the board at the same time.** Flash with the camera
> cable unplugged; plug the camera in only after the computer is disconnected.

1. **Flash the firmware.** Download `fyxis-<version>-full.bin` from
   [Releases](https://github.com/dan-barham/fyxis/releases) and write it at 0x0 with Espressif's web flasher in Chrome. No tools to
   install. Step by step: [docs/building.md](docs/building.md#flashing-no-tools-needed).
2. **Print the case.** Files and settings in [enclosure/](enclosure/README.md).
3. **Wire the REC key** to the board's sensor header: [docs/hardware.md](docs/hardware.md#physical-rec-button).
4. **Mount it** on the PYXIS side plate with the two 1/4"-20 screws, and connect the USB-C cable.
5. **Pair:** turn on Bluetooth in the camera's Setup menu. The camera shows a 6-digit PIN; type it on
   the board. That's the only time you'll need it.

Then read the [user guide](docs/user-guide.md) for everything on the screen and in Settings (tap the
battery strip).

## Documentation

| | |
|---|---|
| [User guide](docs/user-guide.md) | the screen, editors, presets, takes, settings, Wi-Fi updates |
| [Hardware](docs/hardware.md) | parts, how it connects, REC key wiring |
| [Flashing and building](docs/building.md) | web flashing, building with ESP-IDF, the Mac simulator, how it's made fast |
| [Bluetooth protocol](docs/protocol.md) | the camera-control parameters it reads and writes |
| [Enclosure](enclosure/README.md) | printing, hardware, assembly |
| [Changelog](CHANGELOG.md) | |

## Repository

| | |
|---|---|
| `main/` | the firmware's own code: UI (`bigsix.c`), Bluetooth (`camera.c`), protocol (`camproto.c`), REC key, settings, Wi-Fi update |
| `firmware/` | the ESP-IDF project: board set-up, build settings, partition table, a patch for Espressif's display adapter |
| `sim/` | a Mac simulator that runs the same UI and protocol code in a window, against the real camera or a demo |
| `enclosure/` | the OpenSCAD case model, ready-to-print STL/3MF files and a script to re-export them |
| `usb-probe/` | the test firmware that showed USB control isn't possible without extra hardware (the PYXIS is always the USB host) |
| `docs/`, `media/` | documentation, screenshots, renders and the demo video |

## Status

Built for and tested on a PYXIS 12K. Other Blackmagic cameras with the same Bluetooth camera control
(the Pocket and URSA families) will probably mostly work, but haven't been tried. Issues and pull
requests are welcome.

## Licence

Firmware and tools: [MIT](LICENSE). Case design, documentation and media:
[CC BY 4.0](enclosure/LICENSE). Fonts: Inter (SIL OFL 1.1) and Tabler Icons (MIT), see
[licenses/](licenses/).

FYXIS is an independent project by [Dan Barham](https://danbarham.com), a director of photography in
Vancouver. It isn't affiliated with or endorsed by Blackmagic Design. PYXIS is a trademark of
Blackmagic Design Pty. Ltd. Use at your own risk.
