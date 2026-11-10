# Changelog

## Unreleased
- **Main screen:** **Presets** and **Playback** buttons, bottom left, instead of tapping the timecode
  (presets). The white balance tile is laid out like the timecode tile: `5600K`, then `WB · TINT 0`.
- **Playback screen:** big previous / play-pause / next / loop buttons and the clip's timecode.
  Opens by itself when the camera plays back. Live view returns. Loop is done by the board (the
  camera ignores its loop setting over Bluetooth).
- **REC from playback:** the REC key now always rolls a clip, even while the camera is playing one back.
- **Exposure lock:** changing the frame rate, on the board or the camera, also moves the shutter angle
  so the shutter speed and brightness stay the same (24 → 48 fps at 180° becomes 360°). Settings ›
  Camera. Shows how many stops short it is when 360° isn't enough.
- **Sleep:** with no camera for a minute (or 5, or never: Settings › Display), the screen and its
  backlight go off, the panel refreshes more slowly and Bluetooth scans at about a tenth of the duty
  cycle. The camera coming back, a touch or the REC key wakes it.

## 2.1 — November 2026
- **Presets:** six one-tap combinations of FPS, shutter angle, EI, white balance and tint (tap the timecode).
- **Take log:** take number, start timecode and length for every take; `TAKE 14 00:01:23` after each one.
- **Battery graph:** the last two hours of battery voltage against your warning levels.
- **Night mode:** an all-red palette for dark sets.
- **Lens info:** f-stop and focal length, if the camera reports them.
- **Wi-Fi updates:** load new firmware from a phone or computer with the board still on the camera.
  Falls back to the previous version if a new one doesn't start properly.
- Round shapes corrected for the panel's non-square pixels.
- Needs one USB (UART) flash to move to the new partition table; Wi-Fi updates after that.

## 2.0 — November 2026
- Display about 10x faster: only what changes is redrawn, and only changed areas are copied to the panel.
- Faster Bluetooth timing (7.5–15 ms connection interval) and an interrupt-driven REC button.
- New layout: two-column timecode, clip timer, card slots and sensor area under it, battery strip.
- On-board settings: REC button behaviour, record frame, battery levels, orientation, white-balance step,
  forget camera, link and display diagnostics.
- The on-screen REC button is gone (the physical button does it).

## 1.0 — November 2026
- First version: FPS, shutter, EI, white balance and tint, timecode, cards and battery over Bluetooth;
  touch editing; record tally; physical REC button.
