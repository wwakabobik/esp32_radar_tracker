from __future__ import annotations

import asyncio
import json
import logging
from datetime import datetime, timezone

from aiomqtt import Client

from config import (
    MQTT_HOST,
    MQTT_PORT,
    STANDUP_INTERVAL_MIN,
    TOPIC_BUTTON,
    TOPIC_DISPLAY,
    TOPIC_GESTURE,
    TOPIC_MODE,
    TOPIC_OTA,
    TOPIC_RADAR,
    TOPIC_STATUS,
)
from db import get_active_session, insert_radar_sample, start_session, toggle_session_pause
from display_engine import DisplayEngine
from modes.media import MediaController
from modes.sleep import SleepAnalyzer
from modes.work import WorkTracker

logger = logging.getLogger(__name__)


class HubDaemon:
    def __init__(self) -> None:
        self.mode = "work"
        self.online = False
        self.last_status: dict = {}
        self.work = WorkTracker()
        self.sleep = SleepAnalyzer()
        self.media = MediaController()
        self.display = DisplayEngine(self.work, self.sleep, self.media)
        self._publish_display = None

    def set_display_publisher(self, publisher) -> None:
        self._publish_display = publisher
        self.display.set_publisher(publisher)

    async def handle_message(self, topic: str, payload: bytes) -> None:
        text = payload.decode("utf-8", errors="ignore")
        if topic == TOPIC_RADAR:
            data = json.loads(text)
            await insert_radar_sample(data)
            if self.mode == "work":
                await self.work.on_radar(data)
            elif self.mode == "sleep":
                await self.sleep.on_radar(data)
            elif self.mode == "media":
                await self.media.on_radar(data)
        elif topic == TOPIC_BUTTON:
            data = json.loads(text)
            await self._handle_button(data)
        elif topic == TOPIC_GESTURE:
            data = json.loads(text)
            if self.mode == "media":
                await self.media.on_gesture(data)
        elif topic == TOPIC_MODE:
            await self.set_mode(text.strip('"'))
        elif topic == TOPIC_STATUS:
            self.online = True
            self.last_status = json.loads(text)

    async def _handle_button(self, data: dict) -> None:
        btn_id = data.get("id")
        event = data.get("event")
        if btn_id == 1 and event == "press" and self.mode == "work":
            session = await get_active_session()
            if session and not session["paused"]:
                await toggle_session_pause(session["id"], True)
            elif session and session["paused"]:
                await toggle_session_pause(session["id"], False)
            else:
                await start_session("work")

    async def set_mode(self, mode: str) -> None:
        if mode not in {"work", "sleep", "media"}:
            return
        self.mode = mode
        await self.work.on_mode_change(mode)
        await self.sleep.on_mode_change(mode)
        await self.media.on_mode_change(mode)

    async def display_loop(self) -> None:
        while True:
            if self._publish_display:
                payload = await self.display.build_payload(self.mode)
                await self._publish_display(payload)
            await asyncio.sleep(1)

    async def standup_loop(self) -> None:
        while True:
            await asyncio.sleep(30)
            if self.mode != "work":
                continue
            from db import get_setting
            interval = int(await get_setting("standup_min", str(STANDUP_INTERVAL_MIN)) or STANDUP_INTERVAL_MIN)
            reminder = await self.work.check_standup(interval)
            if reminder:
                logger.info("Standup reminder triggered")


async def mqtt_loop(daemon: HubDaemon) -> None:
    while True:
        try:
            async with Client(MQTT_HOST, MQTT_PORT) as client:
                async def publish_display(payload: dict) -> None:
                    await client.publish(TOPIC_DISPLAY, json.dumps(payload))

                daemon.set_display_publisher(publish_display)
                for topic in (TOPIC_RADAR, TOPIC_BUTTON, TOPIC_GESTURE, TOPIC_MODE, TOPIC_STATUS):
                    await client.subscribe(topic)

                async with client.messages() as messages:
                    async for message in messages:
                        await daemon.handle_message(str(message.topic), message.payload)
        except Exception:
            logger.exception("MQTT disconnected, retrying in 3s")
            await asyncio.sleep(3)
