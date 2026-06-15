from __future__ import annotations

from aiohttp import web

from config import FIRMWARE_BIN, OTA_PORT


async def firmware_handler(_: web.Request) -> web.Response:
    if not FIRMWARE_BIN.exists():
        return web.Response(text="Firmware binary not found", status=404)
    return web.FileResponse(path=FIRMWARE_BIN, headers={"Content-Type": "application/octet-stream"})


async def start_ota_server() -> None:
    app = web.Application()
    app.router.add_get("/firmware.bin", firmware_handler)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", OTA_PORT)
    await site.start()
