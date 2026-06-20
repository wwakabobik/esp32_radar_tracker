from __future__ import annotations

import json

from aiomqtt import Client

from config import MQTT_HOST, MQTT_PORT, TOPIC_CONFIG
from db import get_all_settings


async def build_device_config() -> dict:
    s = await get_all_settings()
    return {
        "work_presence_min_cm": int(s.get("work_presence_min_cm", 20)),
        "work_presence_max_cm": int(s.get("work_presence_max_cm", 400)),
        "work_presence_type": s.get("work_presence_type", "both"),
        "sleep_breath_max_cm": int(s.get("sleep_breath_max_cm", 120)),
        "sleep_energy_min": int(s.get("sleep_energy_min", 5)),
        "radar_gate_work": int(s.get("radar_gate_work", 6)),
        "radar_gate_sleep": int(s.get("radar_gate_sleep", 8)),
        "sleep_display_mode": s.get("sleep_display_mode", "off"),
        "gesture_zone_min_cm": int(s.get("gesture_zone_min_cm", 12)),
        "gesture_zone_max_cm": int(s.get("gesture_zone_max_cm", 28)),
        "gesture_hold_ms": int(s.get("gesture_hold_ms", 400)),
        "gesture_debounce_ms": int(
            s.get("gesture_debounce_ms") or s.get("gesture_swipe_debounce_ms") or 2500
        ),
        "gesture_debug": s.get("gesture_debug", "0") == "1",
        "ai_enabled": s.get("ai_enabled", "1") == "1",
        "ai_record_mode": s.get("ai_record_mode", "0") == "1",
        "ai_confidence_min": int(s.get("ai_confidence_min", 60)),
        "ai_fatigue_minutes": int(s.get("ai_fatigue_minutes", 45)),
        "ai_fallback_heuristics": s.get("ai_fallback_heuristics", "1") == "1",
    }


async def publish_device_config(client: Client | None = None) -> None:
    payload = json.dumps(await build_device_config())
    if client:
        await client.publish(TOPIC_CONFIG, payload, qos=1, retain=True)
        return
    async with Client(MQTT_HOST, MQTT_PORT) as c:
        await c.publish(TOPIC_CONFIG, payload, qos=1, retain=True)
