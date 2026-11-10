# Bluetooth protocol and reliability

## Protocol (Blackmagic Camera Control over Bluetooth LE)

Service `291D567A-6D75-11E6-8B77-86F30CA893D3`; settings arrive as notifications on
*Incoming Camera Control* `B864E140-…` and changes are written to *Outgoing Camera Control* `5DD3465F-…`.

| Param | Meaning | Data |
|---|---|---|
| 1.14 | ISO | int32 |
| 1.11 | Shutter angle | int32, degrees × 100 |
| 1.12 | Shutter speed | int32, 1/x |
| 1.2 | White balance + tint | int16 ×2 |
| 1.9 | Recording format | int16 ×5: project fps, sensor fps, width, height, flags (bit0 project M-rate, bit1 sensor M-rate, bit2 off-speed) |
| 9.2 | Remaining record time | int16 per card slot, seconds. An empty slot reads 0, like a full card |
| 10.1 flags | Card slots | bit 5 = slot 1, bit 6 = slot 2. Cleared on eject (the time isn't resent) |
| 9.0 | Battery / DC input | int16: mV at the 2-pin LEMO (further fields logged by the simulator, not used yet) |
| 10.1 | Transport mode | int8 ×5: mode (0 preview, 1 playback, 2 record), speed, flags, slot 1, slot 2. Record start/stop writes it back with the mode changed; starting also clears the speed and flag bits 0–1, so REC rolls a clip straight from playback (checked on the PYXIS) |
| 10.2 | Playback step | int8 ×2: 0 clip / 1 frame, steps (−1 previous, +1 next) |

### Playback (tested on a PYXIS 12K, 9 Nov 2026, with the simulator's test keys)

- **Works:** writing 10.1 with mode 1 plays (speed 1), pauses (speed 0), plays at 2× (speed 2) and
  backwards (speed −1); mode 0 returns to live view. 10.2 clip steps move to the previous / next
  clip. While playing, the Timecode characteristic sends the clip's own timecode.
- **REC from playback:** the REC packet (mode 2, speed 0, loop and bit 1 cleared) goes straight from
  playback to recording in one press.
- **No effect:** 10.2 frame steps (type 1) are acknowledged but the picture and timecode don't move.
- **Loop** (10.1 flag bit 0) is acknowledged but never reported back; not confirmed either way.
- **Flag bit 1** is set by the camera while a clip is actually playing and cleared when paused (the
  protocol document calls it "play all").
- Playback only finds clips recorded in the camera's current format; right after formatting the
  card the camera answered play with "No clips in this format".
- 2× and backwards were reported by the camera but not watched on its screen.

Timecode comes from its own characteristic (`6D8F2110-…`, notifications) as 32-bit BCD
`HH:MM:SS:FF`. The Mac simulator prints the first few raw values so the decoding can be checked.

The M-rate flags mean 23.98 / 29.97 / 59.94 are shown correctly with no manual setting.

## Reliability measures

- **Auto-reconnect:** any drop restarts scanning straight away; no touch needed.
- **Never shows stale values as live:** if the link drops, the last values stay up **in grey**, the
  line under the timecode shows **RECONNECTING** in amber, and the tally is suppressed until live data returns.
- **Short range:** the board sits ~10 cm from the camera, so signal strength is not a concern.
  Keep the enclosure non-metallic over the board's antenna area.
- **One controller at a time:** the camera most likely accepts a single Bluetooth controller. While
  FYXIS is connected, an iPad or phone camera-control app probably can't connect.
