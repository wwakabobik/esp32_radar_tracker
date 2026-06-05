from __future__ import annotations

from fastapi import APIRouter, Request

from config import MQTT_HOST, MQTT_PORT, OTA_HOST, OTA_PORT, STANDUP_INTERVAL_MIN
from db import get_setting

router = APIRouter(prefix="/api/settings", tags=["settings"])


@router.get("/")
async def get_settings(request: Request) -> dict:
    standup = await get_setting("standup_min", str(STANDUP_INTERVAL_MIN))
    return {
        "mqtt_host": MQTT_HOST,
        "mqtt_port": MQTT_PORT,
        "web_port": request.app.state.web_port,
        "ota_url": f"http://{OTA_HOST}:{OTA_PORT}/firmware.bin",
        "standup_min": int(standup or STANDUP_INTERVAL_MIN),
        "mode": request.app.state.daemon.mode,
    }
