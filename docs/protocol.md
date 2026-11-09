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
| 10.1 | Transport mode | int8 ×5: mode (2 = record), speed, flags, slot 1, slot 2. Record start/stop writes it back with only the mode changed |

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
