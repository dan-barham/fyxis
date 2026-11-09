#!/usr/bin/env python3
"""
Bluetooth helper for the Mac simulator. Started automatically by bigsix_sim.

stdout -> simulator:  LINK <searching|connecting|online>   RX <hex>   TC <hex>   LOG <text>
stdin  <- simulator:  TX <hex>   (a camera-control packet to write to the camera)
"""

import asyncio
import sys
import threading

from bleak import BleakClient, BleakScanner

SERVICE = "291d567a-6d75-11e6-8b77-86f30ca893d3"
OUT     = "5dd3465f-1aee-4299-8493-d2eca2f8e1bb"   # Outgoing Camera Control (write)
IN      = "b864e140-76a0-416a-bf30-5876504537d9"   # Incoming Camera Control (indicate)
STATUS  = "7fe8691d-95dc-4fc5-8abd-ca74339b51b9"
NAME    = "ffac0c52-c9fb-41a0-b063-cc76282eb89c"
TC      = "6d8f2110-86f1-41bf-9afb-451d87e976c8"   # Timecode (notify)


def out(line: str):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def start_stdin_thread(queue: asyncio.Queue):
    """Read packets from the simulator on a plain thread (works on every macOS/Python)."""
    loop = asyncio.get_running_loop()

    def run():
        try:
            for line in sys.stdin:
                loop.call_soon_threadsafe(queue.put_nowait, line.strip())
        except Exception as e:  # noqa: BLE001
            out(f"LOG input reader stopped: {e!r}")
        loop.call_soon_threadsafe(queue.put_nowait, None)

    threading.Thread(target=run, daemon=True).start()


async def write_packet(client, data: bytes):
    """Write a camera-control packet, reporting exactly what happened."""
    try:
        await asyncio.wait_for(client.write_gatt_char(OUT, data, response=True), timeout=5)
        out(f"LOG write {data.hex()}: camera acknowledged "
            "(if the value doesn't change, the camera ignored it)")
        return
    except asyncio.TimeoutError:
        out(f"LOG write {data.hex()}: no acknowledgement after 5 s, retrying without one")
    except Exception as e:  # noqa: BLE001
        out(f"LOG write {data.hex()}: camera REJECTED it: {e!r}; retrying without acknowledgement")
    try:
        await asyncio.wait_for(client.write_gatt_char(OUT, data, response=False), timeout=5)
        out(f"LOG write {data.hex()}: sent without acknowledgement")
    except Exception as e:  # noqa: BLE001
        out(f"LOG write {data.hex()}: failed again: {e!r}")


async def session(queue: asyncio.Queue):
    out("LINK searching")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE in [u.lower() for u in (adv.service_uuids or [])], timeout=10.0)
    if dev is None:
        out("LOG no camera found yet (camera Bluetooth on? no other app connected?)")
        return
    out(f"LOG found {dev.name}")
    out("LINK connecting")
    gone = asyncio.Event()
    async with BleakClient(dev, timeout=20.0, disconnected_callback=lambda _: gone.set()) as client:
        await client.start_notify(IN, lambda _, d: out("RX " + bytes(d).hex()))
        try:
            await client.start_notify(TC, lambda _, d: out("TC " + bytes(d).hex()))
        except Exception as e:  # noqa: BLE001
            out(f"LOG timecode not available: {e!r}")
        # macOS only pairs when the camera refuses something; this read/write triggers it.
        out("LOG if the camera shows a PIN, type it into the macOS pairing box")
        try:
            await asyncio.wait_for(client.read_gatt_char(STATUS), timeout=60)
            await client.write_gatt_char(NAME, b"Big Six sim", response=True)
        except Exception as e:  # noqa: BLE001
            out(f"LOG pairing step: {e!r}")
        out("LINK online")
        while not queue.empty():          # drop presses made before the link was ready
            queue.get_nowait()
        while not gone.is_set():
            try:
                line = await asyncio.wait_for(queue.get(), timeout=0.5)
            except asyncio.TimeoutError:
                continue
            if line is None:
                return
            if line.startswith("TX "):
                data = bytes.fromhex(line[3:])
                out(f"LOG got {data.hex()} from the screen, writing to camera")
                await write_packet(client, data)
            elif line:
                out(f"LOG ignored input: {line!r}")
    out("LOG disconnected")


async def main():
    queue: asyncio.Queue = asyncio.Queue()
    start_stdin_thread(queue)
    out(f"LOG bridge started (Python {sys.version.split()[0]})")
    while True:
        try:
            await session(queue)
        except Exception as e:  # noqa: BLE001
            out(f"LOG error: {e!r}")
        out("LINK searching")
        await asyncio.sleep(1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
