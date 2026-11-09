# USB probe (test firmware)

Answers one question: can the board talk to the PYXIS over the rear USB-C cable, without Bluetooth?

Build like `../firmware` (copy its `deps/`, `components/` and `partitions.csv` here, and
`font_label_20.c` / `font_mid_36.c` from `../main` into `main/`), or flash the prebuilt
`usb-probe-firmware.bin` at 0x0.

**Power the board from the camera only during the test.** The board's USB-port 5 V is its main 5 V
rail and the UART port feeds that rail, so with the Mac also connected its 5 V would reach the camera.

- DEVICE test (default): board is a USB device. Shows whether the camera acts as a USB host on the
  port (bus resets, start-of-frame packets).
- HOST test: board is the USB host. Lists any USB device the camera presents, and if there is a PTP
  interface, opens a session and reads live properties 0xD006 and 0xD00A.
