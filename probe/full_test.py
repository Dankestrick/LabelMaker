"""Full 4x6 bitmap print in fast mode: border, fine lines and bars show any dropped data."""
import asyncio, sys, time
from PIL import Image, ImageDraw, ImageFont
from bleak import BleakClient
ADDR="00:00:00:00:00:00"; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
W, H = 808, 1218
img = Image.new("1", (W, H), 1)
d = ImageDraw.Draw(img)
d.rectangle([4, 4, W-5, H-5], outline=0, width=6)
f = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Bold.ttf", 64)
s = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 32)
d.text((40, 40), "Label Maker", font=f, fill=0)
d.text((40, 130), "Full 4x6 bitmap, fast send", font=s, fill=0)
for i, w in enumerate((1, 2, 3, 4)):            # line widths in dots
    y = 220 + i * 90
    d.text((40, y + 20), f"{w} dot", font=s, fill=0)
    for x in range(200, 770, w * 2):
        d.rectangle([x, y, x + w - 1, y + 70], fill=0)
import random; random.seed(7); x = 40
while x < 770:                                    # barcode-like bars
    w = random.choice((2, 4, 6)); d.rectangle([x, 620, x + w - 1, 820], fill=0); x += w + random.choice((2, 4, 6))
for y in range(860, 1180, 40):                    # one-dot rows
    d.line([40, y, W-40, y], fill=0, width=1)
d.text((40, 1120), "If every line is straight, nothing was dropped", font=s, fill=0)
img.save("probe/full_test.png")
raw = img.tobytes()                               # mode 1: 1 bit = white, 0 = black; matches the printer
job = (b"\r\nSIZE 100 mm,150 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 1\r\nDENSITY 10\r\nCLS\r\n"
       + f"BITMAP 0,0,{W//8},{H},0,".encode() + raw + b"\r\nPRINT 1\r\n")
async def main():
    async with BleakClient(ADDR, timeout=20) as c:
        await c._backend._acquire_mtu()
        await c.start_notify(RX, lambda ch, d: print("<-", bytes(d)))
        n = c.mtu_size - 3; t = time.monotonic()
        for i in range(0, len(job), n):
            await c.write_gatt_char(TX, job[i:i+n], response=False); await asyncio.sleep(0.01)
        print(f"sent {len(job)} B in {time.monotonic()-t:.1f}s"); await asyncio.sleep(8)
if "--print" in sys.argv: asyncio.run(main())
