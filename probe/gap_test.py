"""Log write gaps and when the printer reports an error, to see if a stall ends the bitmap."""
import asyncio, sys, time
from PIL import Image
from bleak import BleakClient
ADDR = __import__("os").environ["D450_ADDR"]; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
img = Image.open("probe/full_test.png"); W, H = img.size
head = b"\r\nSIZE 100 mm,150 mm\r\nCLS\r\n" + f"BITMAP 0,0,{W//8},{H},0,".encode()
job = head + img.tobytes() + b"\r\nCLS\r\n"
async def main(every):
    async with BleakClient(ADDR, timeout=20) as c:
        await c._backend._acquire_mtu()
        t0 = time.monotonic(); log = []; pos = [0]
        await c.start_notify(RX, lambda ch, d: log.append((time.monotonic()-t0, pos[0], len(d), bytes(d[:12]))))
        n = c.mtu_size - 3; last = time.monotonic(); gaps = []
        for k, i in enumerate(range(0, len(job), n)):
            await c.write_gatt_char(TX, job[i:i+n], response=(k % every == every-1))
            now = time.monotonic(); gaps.append((now-last, i)); last = now; pos[0] = i
        await asyncio.sleep(3)
        gaps.sort(reverse=True)
        print(f"every {every}: total {time.monotonic()-t0-3:.1f}s, worst gaps (s, byte):", [(round(g,3), b) for g, b in gaps[:4]])
        for e in log: print("   note at %.2fs after byte %d, len %d %r" % e)
asyncio.run(main(int(sys.argv[1])))
