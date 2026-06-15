from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel, Field

from config import DISPLAY_FONTS
from db import get_all_settings, get_display_layout, set_display_layout, set_settings
from device_config import publish_device_config
from display_engine import DisplayEngine

router = APIRouter(prefix="/api/display", tags=["display"])


class LayoutItem(BaseModel):
    slot: int
    widget: str
    font: str = "medium"


class LayoutUpdate(BaseModel):
    layout: list[LayoutItem]
    brightness: int = Field(default=255, ge=10, le=255)
    line_count: int = Field(default=2, ge=0, le=2)
    sleep_display_mode: str = "off"


class PreviewRequest(BaseModel):
    layout: list[LayoutItem]
    line_count: int = Field(default=2, ge=0, le=2)


@router.get("/layout")
async def get_layout() -> dict:
    layout = await get_display_layout()
    settings = await get_all_settings()
    return {
        "layout": layout,
        "widgets": sorted(DisplayEngine.WIDGETS),
        "fonts": list(DISPLAY_FONTS),
        "brightness": int(settings.get("display_brightness", 255)),
        "line_count": int(settings.get("display_line_count", 2) or 2),
        "sleep_display_mode": settings.get("sleep_display_mode", "off"),
    }


@router.put("/layout")
async def update_layout(body: LayoutUpdate, request: Request) -> dict:
    layout = [item.model_dump() for item in body.layout]
    await set_display_layout(layout)
    await set_settings(
        {
            "display_brightness": str(body.brightness),
            "display_line_count": str(body.line_count),
            "sleep_display_mode": body.sleep_display_mode,
        }
    )
    await publish_device_config()
    daemon = request.app.state.daemon
    if daemon._publish_display:
        payload = await daemon.display.build_payload(daemon.mode)
        await daemon._publish_display(payload)
    return {"ok": True, "layout": layout}


@router.get("/preview")
async def preview_layout(request: Request) -> dict:
    daemon = request.app.state.daemon
    layout = await get_display_layout()
    line_count = int((await get_all_settings()).get("display_line_count", 2) or 2)
    return await daemon.display.build_preview(layout, daemon.mode, line_count)


@router.post("/preview")
async def preview_custom(request: Request, body: PreviewRequest) -> dict:
    daemon = request.app.state.daemon
    layout = [item.model_dump() for item in body.layout]
    return await daemon.display.build_preview(layout, daemon.mode, body.line_count)
