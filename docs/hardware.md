# Hardware

> **Never connect a computer and the camera to the board at the same time.** The board has two USB-C
> ports (UART and USB). Use the UART port with a computer only while the camera cable is unplugged,
> and plug the camera back in only after the computer is disconnected.

## Shopping list

Everything for one unit. No camera parts are modified.

| Qty | Part | What to look for |
|---|---|---|
| 1 | **Waveshare ESP32-S3-Touch-LCD-4.3** | The 800 × 480 version, *not* the 4.3B. From Waveshare's store or Amazon. |
| 1 | **USB-C to USB-C cable, right-angle** | 15–30 cm, with a 90° plug at least at the board end. USB 2.0 is enough; it only carries power. |
| 1 | **Kailh Choc V2 key switch** (PG1353) | Brown (tactile) is what the case was made for. The printed keycap fits its MX-style cross stem. Buy a couple; they're cheap. |
| 1 | **1 kΩ resistor**, 1/4 W | Any through-hole resistor. |
| 1 | **Hook-up wire**, about 26–28 AWG | Two short lengths (~10 cm each), plus a little heat-shrink. Soldered to the board's sensor header and the switch. |
| 2 | **1/4"-20 button-head screw, 3/8" long** | Into the PYXIS side plate. Use 5/16" if your plate's holes are shallower. |
| 4 | **M2 × 5 mm button-head screw** | Self-tapping / thread-forming into the case's pilot holes. |
| ~30 cm | **1 mm foam tape** | Single-sided, about 5 mm wide, for the glass border. |
| ~60 g | **PETG or ASA filament** | Black for the case. Optional small amounts of white and red for the printed lettering and key (multi-colour printer). |

Tools: a 3D printer, a soldering iron, hex keys for the screws (5/32" for the 1/4"-20 button heads,
1.3 mm for the M2), and a USB-C cable to connect the board to a computer once for flashing.

## How it connects

1. The camera powers the board through the USB-C cable, the same way it powers a PYXIS Monitor.
   The firmware disconnects the board's USB data lines, so the camera just sees something drawing power.
2. The board scans for the camera's Bluetooth service and connects.
3. **First time only:** the camera shows a 6-digit PIN and the board shows a keypad. Enter it and
   the pairing is stored on both sides, so later connections need no input.
4. The camera sends its current settings and then every change as it happens.

**Camera setup:** Setup menu → Bluetooth **on**.

**Why not control over the USB-C cable?** Tested with `usb-probe/` on the PYXIS 12K (7 Nov 2026):
the camera acts as **USB host** on that cable (DEVICE test: bus resets and frames from it), and it
never presents itself as a USB device to the board (HOST test: nothing seen). USB-C gives the host
role to the side that supplies power, and the board's port is wired as a powered device (5.1 kΩ on
CC). The camera does offer camera control over PTP when a Mac is the host. So it only answers as a
device when the other end is host *and* supplies power, and then the camera can't power the board.
Getting round that needs extra hardware: a USB-PD controller to swap roles, or rewiring the board's
CC resistors and powering the board some other way.

## Physical REC button

A key switch with a big keycap, wired to the board's **sensor header** — no extra board, two wires.
Press to start, press to stop (or hold to stop: Settings › Recording). A second press within 0.4 s is
ignored, and it works whether editing is locked or not. It isn't lit; the screen's red frame is the
tally.

**Parts**

| Part | Notes |
|---|---|
| Kailh Choc **Brown** (V1 PG1350 or V2 PG1353) | Tactile, 3 mm travel. "RGB/SMD" in a listing only means the housing passes light; there's no LED inside. V2 takes standard MX-stem keycaps; V1 needs Choc-V1-stem caps. |
| Keycap, about 25–30 mm | Any big MX-stem cap (V2), or 3D-print one. A slightly concave top is easy to find by feel. |
| JST **PH 2.0 mm** lead for the sensor header | Check the header's pin count on your board. |
| Mount | Print a 1.2–1.5 mm plate with a **14 × 14 mm** cut-out (standard Choc plate hole); the switch clips in. A raised collar around the cap stops accidental presses. |

**Wiring**

The sensor header is **J6, 3 pins** (Waveshare schematic, "Sensor AD"):

| J6 pin | Signal | Use |
|---|---|---|
| 1 | 3V3 | **leave unconnected** |
| 2 (middle) | GND | switch |
| 3 | AD = **GPIO6** (direct, no parts in between) | switch, ideally through 1 kΩ |

```
J6 pin 3 (AD, GPIO6) ── 1 kΩ ── switch ── J6 pin 2 (GND)        J6 pin 1 (3V3): not used
```

GND is the middle pin, so the only mistake possible is using the wrong outer pin. Wired to 3V3 by
mistake, the switch would short 3V3 to GND on every press. The 1 kΩ resistor keeps that harmless
(about 3 mA, and the button just won't work), and it doesn't affect normal use: against the internal
pull-up of about 45 kΩ, the pin still reads well under 0.1 V when pressed. Before fitting, check with
a multimeter (board unplugged, continuity mode) that the middle pin beeps to the USB-C shell (GND).
A voltmeter can't tell the two outer pins apart, because AD also sits at 3.3 V through the pull-up,
so find pin 3 from the pin-1 mark on the board's silkscreen, and let the resistor cover any doubt.

The firmware turns on GPIO6's internal pull-up and takes an interrupt on the falling edge. It checks
the pin is still low 1.5 ms later (to ignore a spike on the wire) and acts at once, then ignores
contact bounce until the switch has read released for 30 ms (`main/recbutton.c`). For a lead longer than about
30 cm, an extra 10 kΩ from AD (pin 3) to 3V3 (pin 1) makes it less sensitive to interference.

## Enclosure

FYXIS, a printable case for the PYXIS side plate with a recessed REC key and multi-colour inlays, is in `enclosure/`
(OpenSCAD model, ready-to-print STLs, hardware list and assembly steps in `enclosure/README.md`).
