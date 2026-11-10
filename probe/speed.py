"""Send a full 4x6 bitmap without PRINT to time the link, then clear it."""
import asyncio, sys, time
from bleak import BleakClient
ADDR = __import__("os").environ["D450_ADDR"]; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
ROW, H = 812 // 8, 1218
job = (b"\r\nSIZE 100 mm,150 mm\r\nCLS\r\n" + f"BITMAP 0,0,{ROW},{H},0,".encode()
       + b"\xff" * (ROW * H) + b"\r\nCLS\r\n")
async def main():
    pace = float(sys.argv[1]) if len(sys.argv) > 1 else 0
    async with BleakClient(ADDR, timeout=20) as c:
        await c._backend._acquire_mtu()
        errs = []
        await c.start_notify(RX, lambda ch, d: errs.append(bytes(d)))
        chunk = c.mtu_size - 3
        t = time.monotonic()
        for i in range(0, len(job), chunk):
            await c.write_gatt_char(TX, job[i:i+chunk], response=(pace == 0))
            if pace: await asyncio.sleep(pace)
        dt = time.monotonic() - t
        await asyncio.sleep(2)
        print(f"pace {pace}: {len(job)} B in {dt:.1f}s = {len(job)/dt:.0f} B/s, notes {errs}")
asyncio.run(main())
