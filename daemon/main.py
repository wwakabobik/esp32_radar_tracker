from __future__ import annotations

import asyncio
import logging

import uvicorn
from dotenv import load_dotenv

from db import init_db
from hub import HubDaemon, mqtt_loop
from ota_server import start_ota_server
from telegram_bot import create_bot_task
from web.app import create_app

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
logger = logging.getLogger("presence-hub")


async def run_web(app) -> None:
    config = uvicorn.Config(app, host=app.state.web_host, port=app.state.web_port, log_level="info")
    server = uvicorn.Server(config)
    await server.serve()


async def main() -> None:
    load_dotenv()
    await init_db()

    daemon = HubDaemon()
    app = create_app(daemon)

    tasks = [
        asyncio.create_task(mqtt_loop(daemon), name="mqtt"),
        asyncio.create_task(daemon.display_loop(), name="display"),
        asyncio.create_task(daemon.standup_loop(), name="standup"),
        asyncio.create_task(start_ota_server(), name="ota"),
        asyncio.create_task(run_web(app), name="web"),
    ]

    bot_task = await create_bot_task(daemon)
    if bot_task:
        tasks.append(bot_task)

    logger.info("Presence Hub daemon started")
    await asyncio.gather(*tasks)


if __name__ == "__main__":
    asyncio.run(main())
