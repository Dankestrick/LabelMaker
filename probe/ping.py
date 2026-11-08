import asyncio
from bleak import BleakClient
ADDR="00:00:00:00:00:00"; TX="49535343-8841-43f4-a8d4-ecbe34729bb3"; RX="49535343-1e4d-4bd9-ba61-23c647249616"
async def main():
    async with BleakClient(ADDR, timeout=20) as c:
        try:
            await c._backend._acquire_mtu()
        except Exception as e: print("mtu acquire:", e)
        print("mtu", c.mtu_size)
        await c.start_notify(RX, lambda ch,d: print(f"<- {d!r}"))
        for i, data in enumerate([b"\r\n", b"CLS\r\n", b"SELFTEST\r\n"[:0] or b"GAP 3 mm,0 mm\r\n"]):
            for resp in (True, False):
                try:
                    await c.write_gatt_char(TX, data, response=resp); print("ok", data, resp)
                except Exception as e: print("fail", data, resp, e)
        await asyncio.sleep(2)
asyncio.run(main())
