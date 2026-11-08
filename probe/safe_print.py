"""Safe print: send the bitmap, check the printer reported no error, then PRINT. Retry once."""
import asyncio, sys, time
from PIL import Image
from bleak import BleakClient
ADDR="00:00:00:00:00:00"; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
img = Image.open(sys.argv[1]).convert("1"); W, H = img.size; assert W % 8 == 0
body = (b"\r\nSIZE 100 mm,150 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 1\r\nDENSITY 10\r\nCLS\r\n"
        + f"BITMAP 0,0,{W//8},{H},0,".encode() + img.tobytes() + b"\r\n")
async def main():
    async with BleakClient(ADDR, timeout=20) as c:
        await c._backend._acquire_mtu()
        notes = []
        await c.start_notify(RX, lambda ch, d: notes.append(bytes(d)))
        n = c.mtu_size - 3
        for attempt in (1, 2):
            notes.clear(); t = time.monotonic()
            for k, i in enumerate(range(0, len(body), n)):
                await c.write_gatt_char(TX, body[i:i+n], response=(k % 2 == 1))
            await c.write_gatt_char(TX, b"\r\n", response=True)
            await asyncio.sleep(1.0)
            if any(b"error" in x for x in notes):
                print(f"attempt {attempt}: transfer damaged, clearing"); await c.write_gatt_char(TX, b"\r\nCLS\r\n", response=True); await asyncio.sleep(0.5); continue
            print(f"attempt {attempt}: clean in {time.monotonic()-t:.1f}s, printing")
            await c.write_gatt_char(TX, b"PRINT 1\r\n", response=True); await asyncio.sleep(6)
            print([x for x in notes]); return
        print("gave up")
asyncio.run(main())
