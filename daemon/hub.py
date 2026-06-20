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
    TOPIC_AI_STATE,
    TOPIC_BUTTON,
    TOPIC_DISPLAY,
    TOPIC_GESTURE,
    TOPIC_MODE,
    TOPIC_RADAR,
    TOPIC_RADAR_RAW,
    TOPIC_STATUS,
    TOPIC_SYNC_EVENTS,
    TOPIC_DEBUG_GESTURE,
    WORK_TRACKING_MODES,
)
from db import (
    end_session,
    get_active_session,
    get_setting,
    insert_ai_state,
    insert_radar_sample,
    set_last_event_id,
    start_session,
)
from display_engine import DisplayEngine
from modes.media import MediaController
from modes.sleep import SleepAnalyzer
from modes.work import WorkTracker
from notify import send_telegram
from sensor_log import SensorLog
from sync import apply_live_event, apply_sync_batch, publish_ack, should_skip_event

logger = logging.getLogger(__name__)


class HubDaemon:
    def __init__(self) -> None:
        self.mode = "work"
        self.online = False
        self.last_status: dict = {}
        self.last_radar: dict = {}
        self.last_ai_state: dict = {}
        self.last_gesture_debug: dict = {}
        self.sensor_log = SensorLog()
        self.work = WorkTracker()
        self.sleep = SleepAnalyzer()
        self.media = MediaController()
        self.display = DisplayEngine(self.work, self.sleep, self.media)
        self._publish_display = None
        self._mqtt_client: Client | None = None

    def set_display_publisher(self, publisher) -> None:
        self._publish_display = publisher
        self.display.set_publisher(publisher)

    def set_mqtt_client(self, client: Client | None) -> None:
        self._mqtt_client = client

    def _log_sensor(self, kind: str, data: dict) -> None:
        ts = data.get("ts")
        device_ts = float(ts) if ts is not None else None
        self.sensor_log.append(kind, self.mode, data, device_ts=device_ts)

    async def handle_message(self, topic: str, payload: bytes) -> None:
        text = payload.decode("utf-8", errors="ignore")
        if topic == TOPIC_SYNC_EVENTS:
            data = json.loads(text)
            if self._mqtt_client:
                await apply_sync_batch(self, data, self._mqtt_client)
            return

        if topic == TOPIC_RADAR:
            data = json.loads(text)
            self.last_radar = data
            self._log_sensor("radar", data)
            debug_on = (await get_setting("gesture_debug", "0")) == "1"
            if debug_on and self.mode == "media":
                logger.debug("radar media: dist=%s centroid=%s moving=%s", data.get("dist"), data.get("m_gate_centroid"), data.get("moving"))
            await insert_radar_sample(data)
            if self.mode in WORK_TRACKING_MODES:
                await self.work.on_radar(data)
            elif self.mode == "sleep":
                await self.sleep.on_radar(data)
            if self.mode == "media":
                await self.media.on_radar(data)
            return

        if topic == TOPIC_RADAR_RAW:
            data = json.loads(text)
            self._log_sensor("radar_raw", data)
            return

        if topic == TOPIC_AI_STATE:
            data = json.loads(text)
            self.last_ai_state = data
            self._log_sensor("ai_state", data)
            await insert_ai_state(data)
            if self.mode in WORK_TRACKING_MODES:
                await self.work.on_ai_state(data)
            return

        if topic == TOPIC_BUTTON:
            data = json.loads(text)
            eid = data.get("eid")
            if eid:
                await apply_live_event(
                    self,
                    {"id": eid, "type": "button", "mode": self.mode, "data": data, "ts": data.get("ts")},
                    client=self._mqtt_client,
                )
            else:
                await self._handle_button(data)
            return

        if topic == TOPIC_GESTURE:
            data = json.loads(text)
            self._log_sensor("gesture", data)
            # Live gestures must always run; eid dedup is for sync ack only.
            if self.mode == "media":
                await self.media.on_gesture(data)
            eid = data.get("eid")
            if eid and self._mqtt_client and not await should_skip_event(eid):
                await set_last_event_id(eid)
                await publish_ack(self._mqtt_client, eid)
            return

        if topic == TOPIC_DEBUG_GESTURE:
            try:
                self.last_gesture_debug = json.loads(text)
            except json.JSONDecodeError:
                self.last_gesture_debug = {"raw": text}
            self._log_sensor("gesture_debug", self.last_gesture_debug)
            return

        if topic == TOPIC_MODE:
            try:
                parsed = json.loads(text)
                mode = parsed.get("mode", self.mode)
                eid = parsed.get("eid")
            except json.JSONDecodeError:
                mode = text.strip('"')
                eid = None
            if eid:
                await apply_live_event(
                    self,
                    {"id": eid, "type": "mode", "mode": mode, "data": {"mode": mode}, "ts": None},
                    client=self._mqtt_client,
                )
            else:
                await self.set_mode(mode)
            return

        if topic == TOPIC_STATUS:
            self.online = True
            status = json.loads(text)
            self.last_status = status
            mode = status.get("mode")
            if mode in {"work", "sleep", "media"}:
                if mode != self.mode:
                    await self.set_mode(mode)
                elif mode in WORK_TRACKING_MODES:
                    await self.work.ensure_session()

    async def _handle_button(self, data: dict) -> None:
        btn_id = data.get("id")
        event = data.get("event")

        if self.mode == "sleep":
            if btn_id == 1 and event == "press":
                await self.sleep.manual_sleep_start()
            elif btn_id == 2 and event == "press":
                await self.sleep.manual_wake()
                await self.set_mode("media")
            return

        if self.mode in WORK_TRACKING_MODES and btn_id == 1:
            if event == "long":
                session = await get_active_session()
                if session:
                    await end_session(session["id"])
                await start_session("work")
                self.work.last_standup_at = datetime.now(timezone.utc)
                self.work.clear_reminder()
                return
            if event == "press":
                self.work.clear_reminder()
                return

    async def set_mode(self, mode: str, replay_ts: float | None = None) -> None:
        if mode not in {"work", "sleep", "media"}:
            return
        self.mode = mode
        await self.work.on_mode_change(mode)
        await self.sleep.on_mode_change(mode, replay_ts=replay_ts)
        await self.media.on_mode_change(mode)

    async def display_loop(self) -> None:
        while True:
            if self._publish_display:
                try:
                    payload = await self.display.build_payload(self.mode)
                    await self._publish_display(payload)
                except Exception:
                    logger.debug("Display loop publish failed")
            await asyncio.sleep(1)

    async def standup_loop(self) -> None:
        while True:
            await asyncio.sleep(30)
            if self.mode not in WORK_TRACKING_MODES:
                continue
            from db import get_setting

            if (await get_setting("standup_enabled", "1")) != "1":
                continue

            interval = int(
                await get_setting("standup_min", str(STANDUP_INTERVAL_MIN)) or STANDUP_INTERVAL_MIN
            )
            self.work.maybe_reset_standup_after_absence()
            reminder = await self.work.check_standup(interval)
            if reminder:
                logger.info("Standup reminder triggered")
                msg = await get_setting(
                    "telegram_standup_message",
                    "Time to stand up and walk around — you have been at the desk for a while.",
                )
                await send_telegram(msg or "Time to stand up and walk around.")


async def mqtt_loop(daemon: HubDaemon) -> None:
    while True:
        try:
            async with Client(MQTT_HOST, MQTT_PORT) as client:
                async def publish_display(payload: dict) -> None:
                    try:
                        await client.publish(TOPIC_DISPLAY, json.dumps(payload))
                    except Exception:
                        logger.debug("Display publish skipped (MQTT not ready)")

                daemon.set_display_publisher(publish_display)
                daemon.set_mqtt_client(client)
                for topic in (
                    TOPIC_RADAR,
                    TOPIC_RADAR_RAW,
                    TOPIC_AI_STATE,
                    TOPIC_BUTTON,
                    TOPIC_GESTURE,
                    TOPIC_DEBUG_GESTURE,
                    TOPIC_MODE,
                    TOPIC_STATUS,
                    TOPIC_SYNC_EVENTS,
                ):
                    await client.subscribe(topic)

                async for message in client.messages:
                    await daemon.handle_message(str(message.topic), message.payload)
        except Exception:
            logger.exception("MQTT disconnected, retrying in 3s")
            daemon.set_display_publisher(None)
            daemon.set_mqtt_client(None)
            await asyncio.sleep(3)
