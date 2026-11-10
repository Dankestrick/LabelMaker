"""Test print: text, plus two BITMAP blocks to learn the polarity, and a speed check."""
import asyncio, sys, time
from bleak import BleakClient

ADDR = __import__("os").environ["D450_ADDR"]  # your printer's Bluetooth address
TX = "49535343-8841-43f4-a8d4-ecbe34729bb3"
RX = "49535343-1e4d-4bd9-ba61-23c647249616"

W, H = 240, 120              # dots; width must be a multiple of 8
ROW = W // 8

def block(byte):
    return bytes([byte]) * (ROW * H)

job = (b"\r\nSIZE 100 mm,150 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 1\r\nDENSITY 10\r\nCLS\r\n"
       b'TEXT 60,60,"4",0,1,1,"OmaLabel test"\r\n'
       b'TEXT 60,160,"3",0,1,1,"A = bytes 00"\r\n'
       b'TEXT 460,160,"3",0,1,1,"B = bytes FF"\r\n'
       + f"BITMAP 60,220,{ROW},{H},0,".encode() + block(0x00) + b"\r\n"
       + f"BITMAP 460,220,{ROW},{H},0,".encode() + block(0xFF) + b"\r\n"
       + b'TEXT 60,400,"3",0,1,1,"The black box shows which byte means black"\r\n'
       + b"PRINT 1\r\n")

async def main():
    nr = "--noresp" in sys.argv
    async with BleakClient(ADDR, timeout=20) as c:
        try:
            await c._backend._acquire_mtu()
        except Exception as e:
            print("mtu acquire:", e)
        print("mtu", c.mtu_size)
        await c.start_notify(RX, lambda ch, d: print(f"<- {d!r}"))
        chunk = max(20, c.mtu_size - 3)
        t = time.monotonic()
        for i in range(0, len(job), chunk):
            await c.write_gatt_char(TX, job[i:i+chunk], response=not nr)
            if nr:
                await asyncio.sleep(0.005)
        dt = time.monotonic() - t
        print(f"sent {len(job)} bytes in {dt:.2f}s, chunk {chunk}, {len(job)/dt:.0f} B/s")
        await asyncio.sleep(5)

asyncio.run(main())
