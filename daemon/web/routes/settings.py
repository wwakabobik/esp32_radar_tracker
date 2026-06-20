from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from config import (
    DEFAULT_SETTINGS,
    DISCOVERY_PORT,
    MQTT_HOST,
    MQTT_PORT,
    MORNING_SUMMARY_HOUR,
    OTA_PORT,
    STANDUP_INTERVAL_MIN,
    TELEGRAM_CHAT_ID,
    TELEGRAM_TOKEN,
)
from db import get_all_settings, set_settings
from device_config import publish_device_config
from netutil import get_lan_ip, hub_lan_ip

router = APIRouter(prefix="/api/settings", tags=["settings"])


class SettingsUpdate(BaseModel):
    standup_min: int = Field(default=120, ge=30, le=480)
    standup_enabled: bool = True
    telegram_standup_message: str = Field(default="", max_length=500)
    morning_summary_enabled: bool = True
    morning_summary_hour: int = Field(default=8, ge=0, le=23)
    work_presence_min_cm: int = Field(default=20, ge=0, le=600)
    work_presence_max_cm: int = Field(default=400, ge=50, le=600)
    work_presence_type: str = "both"
    sleep_breath_max_cm: int = Field(default=120, ge=30, le=300)
    sleep_energy_min: int = Field(default=5, ge=1, le=100)
    radar_gate_work: int = Field(default=6, ge=1, le=8)
    radar_gate_sleep: int = Field(default=8, ge=1, le=8)
    media_backend: str = "spotify"
    ai_enabled: bool = True
    ai_record_mode: bool = False
    ai_confidence_min: int = Field(default=60, ge=0, le=100)
    ai_fatigue_minutes: int = Field(default=45, ge=5, le=240)
    ai_fallback_heuristics: bool = True


@router.get("/")
async def get_settings(request: Request) -> dict:
    stored = await get_all_settings()
    return {
        "mqtt_host": MQTT_HOST,
        "mqtt_port": MQTT_PORT,
        "lan_ip": get_lan_ip(),
        "discovery_port": DISCOVERY_PORT,
        "web_port": request.app.state.web_port,
        "ota_url": f"http://{hub_lan_ip()}:{OTA_PORT}/firmware.bin",
        "mode": request.app.state.daemon.mode,
        "standup_min": int(stored.get("standup_min", STANDUP_INTERVAL_MIN)),
        "standup_enabled": stored.get("standup_enabled", "1") == "1",
        "telegram_standup_message": stored.get(
            "telegram_standup_message",
            DEFAULT_SETTINGS["telegram_standup_message"],
        ),
        "morning_summary_enabled": stored.get("morning_summary_enabled", "1") == "1",
        "morning_summary_hour": int(stored.get("morning_summary_hour", MORNING_SUMMARY_HOUR)),
        "telegram_configured": bool(TELEGRAM_TOKEN and TELEGRAM_CHAT_ID),
        "work_presence_min_cm": int(stored.get("work_presence_min_cm", 20)),
        "work_presence_max_cm": int(stored.get("work_presence_max_cm", 400)),
        "work_presence_type": stored.get("work_presence_type", "both"),
        "sleep_breath_max_cm": int(stored.get("sleep_breath_max_cm", 120)),
        "sleep_energy_min": int(stored.get("sleep_energy_min", 5)),
        "radar_gate_work": int(stored.get("radar_gate_work", 6)),
        "radar_gate_sleep": int(stored.get("radar_gate_sleep", 8)),
        "media_backend": stored.get("media_backend", "spotify"),
        "ai_enabled": stored.get("ai_enabled", "1") == "1",
        "ai_record_mode": stored.get("ai_record_mode", "0") == "1",
        "ai_confidence_min": int(stored.get("ai_confidence_min", 60)),
        "ai_fatigue_minutes": int(stored.get("ai_fatigue_minutes", 45)),
        "ai_fallback_heuristics": stored.get("ai_fallback_heuristics", "1") == "1",
        "defaults": DEFAULT_SETTINGS,
    }


@router.put("/")
async def update_settings(body: SettingsUpdate) -> dict:
    if body.work_presence_type not in {"both", "moving", "stationary"}:
        body.work_presence_type = "both"
    if body.media_backend not in {"spotify", "system_keys"}:
        body.media_backend = "spotify"
    values = {
        "standup_min": str(body.standup_min),
        "standup_enabled": "1" if body.standup_enabled else "0",
        "telegram_standup_message": body.telegram_standup_message.strip()
        or DEFAULT_SETTINGS["telegram_standup_message"],
        "morning_summary_enabled": "1" if body.morning_summary_enabled else "0",
        "morning_summary_hour": str(body.morning_summary_hour),
        "work_presence_min_cm": str(body.work_presence_min_cm),
        "work_presence_max_cm": str(body.work_presence_max_cm),
        "work_presence_type": body.work_presence_type,
        "sleep_breath_max_cm": str(body.sleep_breath_max_cm),
        "sleep_energy_min": str(body.sleep_energy_min),
        "radar_gate_work": str(body.radar_gate_work),
        "radar_gate_sleep": str(body.radar_gate_sleep),
        "media_backend": body.media_backend,
        "ai_enabled": "1" if body.ai_enabled else "0",
        "ai_record_mode": "1" if body.ai_record_mode else "0",
        "ai_confidence_min": str(body.ai_confidence_min),
        "ai_fatigue_minutes": str(body.ai_fatigue_minutes),
        "ai_fallback_heuristics": "1" if body.ai_fallback_heuristics else "0",
    }
    await set_settings(values)
    await publish_device_config()
    return {"ok": True}
