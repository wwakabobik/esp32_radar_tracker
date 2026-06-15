from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from db import get_all_settings, set_settings
from device_config import publish_device_config

router = APIRouter(prefix="/api/gestures", tags=["gestures"])

GESTURE_DEFAULTS = {
    "gesture_zone_min_cm": 12,
    "gesture_zone_max_cm": 28,
    "gesture_hold_ms": 400,
    "gesture_debounce_ms": 1200,
    "gesture_debug": False,
}


class GestureSettingsUpdate(BaseModel):
    gesture_zone_min_cm: int = Field(default=12, ge=0, le=50)
    gesture_zone_max_cm: int = Field(default=28, ge=3, le=50)
    gesture_hold_ms: int = Field(default=400, ge=100, le=3000)
    gesture_debounce_ms: int = Field(default=1200, ge=300, le=10000)
    gesture_debug: bool = False


def _gesture_from_stored(stored: dict) -> dict:
    debounce = stored.get("gesture_debounce_ms") or stored.get("gesture_swipe_debounce_ms")
    return {
        "gesture_zone_min_cm": int(stored.get("gesture_zone_min_cm", GESTURE_DEFAULTS["gesture_zone_min_cm"])),
        "gesture_zone_max_cm": int(stored.get("gesture_zone_max_cm", GESTURE_DEFAULTS["gesture_zone_max_cm"])),
        "gesture_hold_ms": int(stored.get("gesture_hold_ms", GESTURE_DEFAULTS["gesture_hold_ms"])),
        "gesture_debounce_ms": int(debounce or GESTURE_DEFAULTS["gesture_debounce_ms"]),
        "gesture_debug": stored.get("gesture_debug", "0") == "1",
        "defaults": GESTURE_DEFAULTS,
    }


@router.get("/settings")
async def get_gesture_settings() -> dict:
    stored = await get_all_settings()
    return _gesture_from_stored(stored)


@router.post("/settings")
async def update_gesture_settings(body: GestureSettingsUpdate) -> dict:
    if body.gesture_zone_min_cm >= body.gesture_zone_max_cm:
        return {"ok": False, "error": "zone_min must be less than zone_max"}
    values = {
        "gesture_zone_min_cm": str(body.gesture_zone_min_cm),
        "gesture_zone_max_cm": str(body.gesture_zone_max_cm),
        "gesture_hold_ms": str(body.gesture_hold_ms),
        "gesture_debounce_ms": str(body.gesture_debounce_ms),
        "gesture_debug": "1" if body.gesture_debug else "0",
    }
    await set_settings(values)
    await publish_device_config()
    return {"ok": True}


@router.get("/live")
async def gesture_live(request: Request) -> dict:
    daemon = request.app.state.daemon
    radar = dict(daemon.last_radar) if daemon.last_radar else {}
    debug = dict(daemon.last_gesture_debug) if daemon.last_gesture_debug else {}
    settings = _gesture_from_stored(await get_all_settings())
    dist = radar.get("dist") or radar.get("gesture_dist") or debug.get("dist")
    if dist is not None:
        presence = bool(radar.get("presence")) or int(dist) > 0
    else:
        presence = bool(radar.get("presence")) or bool(debug.get("presence"))
    ts = radar.get("ts") or debug.get("ts")
    in_zone = False
    if dist is not None:
        d = int(dist)
        zmin = settings["gesture_zone_min_cm"]
        zmax = settings["gesture_zone_max_cm"]
        in_zone = d > 0 and zmin <= d <= zmax
    return {
        "mode": daemon.mode,
        "online": daemon.online,
        "last_gesture": daemon.media.last_gesture,
        "last_gesture_ts": daemon.media.last_gesture_ts,
        "dist": dist,
        "presence": presence,
        "in_zone": in_zone,
        "hold_left_ms": debug.get("hold_left_ms"),
        "zone_armed": debug.get("zone_armed"),
        "ts": ts,
        "radar_ts": radar.get("ts"),
        "debug": debug,
    }
