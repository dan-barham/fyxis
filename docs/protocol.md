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
| 10.2 | Playback control | int8, one value: 0 = previous clip, 1 = next clip (Blackmagic's protocol document; there is no frame step). Not used |

### Playback (tested on a PYXIS 12K, 9 Nov 2026; not used)

FYXIS had playback controls for a while; they were removed because looping over Bluetooth was
never reliable. What was found:

- **Play / pause / speed:** 10.1 with mode 1 and a speed. 0 pauses; every speed tried works,
  forwards and backwards: ±1, 2, 4, 8, 16, 32. Playback runs across clip boundaries and stops at
  either end of the card (the camera then reports speed 0). Mode 0 returns to live view.
- **Clip steps:** 10.2 with 0 goes to the start of the previous clip, 1 to the start of the next,
  paused; nothing happens at either end of the card. (An earlier version sent two values, so both
  buttons meant "previous" and next-clip did nothing.)
- **Timecode:** while in playback, the Timecode characteristic sends the clip's own timecode. There
  is no seek to a position over Bluetooth. (IN / OUT marks done from the timecode, by shuttling back
  at 32×, were tried and weren't reliable enough, so they were dropped.)
- **REC from playback:** the REC packet (mode 2, speed 0, flag bits 0–1 cleared) goes straight from
  playback to recording in one press.
- **Loop / play all flags:** ignored. Whatever 10.1 flag bits 0 (loop) and 1 (play all) are set to,
  the camera plays on through every clip and stops at the end of the card; it never loops. It sets
  bit 1 itself while playing and clears it when paused. (An earlier note here said it stopped at the
  end of each clip without the flags; a test on the first of two clips showed it doesn't.)
- **Previous clip** goes to the start of the clip before, even from the end of a clip (it doesn't
  restart the current one); on the first clip, to its own start.
- Playback only finds clips recorded in the camera's current format; right after formatting the
  card the camera answered play with "No clips in this format".
- Clip lists, thumbnails and frame-accurate positioning exist only in the camera's REST API, over
  its Ethernet port; FYXIS has no Ethernet, so it doesn't use them.

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
