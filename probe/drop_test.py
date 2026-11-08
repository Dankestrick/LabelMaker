"""Send the real test bitmap with no PRINT and watch for 'Cmd error' (= dropped bytes)."""
import asyncio, sys, time
from PIL import Image
from bleak import BleakClient
ADDR="00:00:00:00:00:00"; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
img = Image.open("probe/full_test.png"); W, H = img.size
job = (b"\r\nSIZE 100 mm,150 mm\r\nCLS\r\n" + f"BITMAP 0,0,{W//8},{H},0,".encode()
       + img.tobytes() + b"\r\nCLS\r\nGAP 3 mm,0 mm\r\n")
async def main(pace, every):
    async with BleakClient(ADDR, timeout=20) as c:
        await c._backend._acquire_mtu()
        notes = []
        await c.start_notify(RX, lambda ch, d: notes.append(bytes(d)))
        n = c.mtu_size - 3; t = time.monotonic()
        for k, i in enumerate(range(0, len(job), n)):
            confirm = every and (k % every == every - 1)
            await c.write_gatt_char(TX, job[i:i+n], response=bool(confirm))
            if pace: await asyncio.sleep(pace)
        dt = time.monotonic() - t; await asyncio.sleep(3)
        bad = any(b"error" in x for x in notes)
        print(f"pace {pace} confirm every {every}: {dt:.1f}s", "DROPPED" if bad else "clean"); [print("   ", len(x), x[:40], x[-30:]) for x in notes]
pace, every = float(sys.argv[1]), int(sys.argv[2])
asyncio.run(main(pace, every))
