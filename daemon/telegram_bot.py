from __future__ import annotations

import asyncio
import json

from aiogram import Bot, Dispatcher
from aiogram.filters import Command
from aiogram.types import Message
from aiomqtt import Client

from config import (
    MQTT_HOST,
    MQTT_PORT,
    OTA_HOST,
    OTA_PORT,
    STANDUP_INTERVAL_MIN,
    TELEGRAM_CHAT_ID,
    TELEGRAM_TOKEN,
    TOPIC_MODE,
    TOPIC_OTA,
)
from db import get_display_layout, get_today_work_seconds, set_setting
from hub import HubDaemon


def _fmt_hours(seconds: int) -> str:
    return f"{seconds // 3600}h {(seconds % 3600) // 60:02d}m"


async def create_bot_task(daemon: HubDaemon):
    if not TELEGRAM_TOKEN:
        return None

    bot = Bot(token=TELEGRAM_TOKEN)
    dp = Dispatcher()

    def allowed(message: Message) -> bool:
        return not TELEGRAM_CHAT_ID or str(message.chat.id) == str(TELEGRAM_CHAT_ID)

    @dp.message(Command("status"))
    async def cmd_status(message: Message) -> None:
        if not allowed(message):
            return
        session_sec = await daemon.work.session_seconds()
        text = (
            f"Mode: {daemon.mode}\n"
            f"Online: {daemon.online}\n"
            f"Present: {daemon.work.present}\n"
            f"Session: {_fmt_hours(session_sec)}"
        )
        await message.answer(text)

    @dp.message(Command("today"))
    async def cmd_today(message: Message) -> None:
        if not allowed(message):
            return
        total = await get_today_work_seconds()
        await message.answer(f"Today at desk: {_fmt_hours(total)}")

    @dp.message(Command("sleep"))
    async def cmd_sleep(message: Message) -> None:
        if not allowed(message):
            return
        summary = await daemon.sleep.last_night_summary()
        await message.answer(json.dumps(summary, indent=2))

    @dp.message(Command("standup"))
    async def cmd_standup(message: Message) -> None:
        if not allowed(message):
            return
        parts = message.text.split()
        minutes = int(parts[1]) if len(parts) > 1 else STANDUP_INTERVAL_MIN
        await set_setting("standup_min", str(minutes))
        await message.answer(f"Standup interval set to {minutes} min")

    @dp.message(Command("mode"))
    async def cmd_mode(message: Message) -> None:
        if not allowed(message):
            return
        parts = message.text.split()
        if len(parts) < 2:
            await message.answer("Usage: /mode work|sleep|media")
            return
        mode = parts[1].lower()
        if mode not in {"work", "sleep", "media"}:
            await message.answer("Invalid mode")
            return
        async with Client(MQTT_HOST, MQTT_PORT) as client:
            await client.publish(TOPIC_MODE, mode, qos=1)
        await daemon.set_mode(mode)
        await message.answer(f"Mode set to {mode}")

    @dp.message(Command("update"))
    async def cmd_update(message: Message) -> None:
        if not allowed(message):
            return
        url = f"http://{OTA_HOST}:{OTA_PORT}/firmware.bin"
        payload = json.dumps({"url": url})
        async with Client(MQTT_HOST, MQTT_PORT) as client:
            await client.publish(TOPIC_OTA, payload, qos=1)
        await message.answer(f"OTA triggered: {url}")

    @dp.message(Command("settings"))
    async def cmd_settings(message: Message) -> None:
        if not allowed(message):
            return
        layout = await get_display_layout()
        await message.answer(json.dumps({"layout": layout, "standup_min": STANDUP_INTERVAL_MIN}, indent=2))

    async def polling() -> None:
        await dp.start_polling(bot)

    return asyncio.create_task(polling(), name="telegram")
