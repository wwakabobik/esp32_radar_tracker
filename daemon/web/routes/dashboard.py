from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone

from aiomqtt import Client
from fastapi import APIRouter, Request

from config import MQTT_HOST, MQTT_PORT, OTA_PORT, TOPIC_OTA
from db import (
    get_presence_timeline_week,
    get_sleep_week,
    get_today_work_seconds,
    get_week_work_seconds,
)
from netutil import hub_lan_ip

router = APIRouter(prefix="/api/dashboard", tags=["dashboard"])


@router.get("/today")
async def today_stats(request: Request) -> dict:
    daemon = request.app.state.daemon
    online = daemon.online
    if daemon.last_status_at is not None:
        if datetime.now(timezone.utc) - daemon.last_status_at > timedelta(seconds=90):
            online = False
    elif not online:
        online = False
    total = await get_today_work_seconds()
    from db import count_fatigue_events_today

    return {
        "mode": daemon.mode,
        "online": online,
        "present": daemon.work.present,
        "fatigue": daemon.work.fatigue,
        "ai_state": daemon.work.ai_state,
        "ai_confidence": daemon.last_ai_state.get("confidence"),
        "fatigue_events_today": await count_fatigue_events_today(),
        "today_seconds": total,
        "session_seconds": await daemon.work.session_seconds(),
    }


@router.get("/week")
async def week_stats() -> dict:
    return {"days": await get_week_work_seconds()}


@router.get("/presence/week")
async def presence_week() -> dict:
    return {"days": await get_presence_timeline_week()}


@router.get("/sleep/week")
async def sleep_week() -> dict:
    return {"nights": await get_sleep_week()}


@router.post("/ota")
async def trigger_ota() -> dict:
    url = f"http://{hub_lan_ip()}:{OTA_PORT}/firmware.bin"
    payload = json.dumps({"url": url})
    async with Client(MQTT_HOST, MQTT_PORT) as client:
        await client.publish(TOPIC_OTA, payload, qos=1)
    return {"ok": True, "url": url}
