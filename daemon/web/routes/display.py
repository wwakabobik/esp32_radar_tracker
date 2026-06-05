from __future__ import annotations

from fastapi import APIRouter, Request
from pydantic import BaseModel

from db import get_display_layout, set_display_layout
from display_engine import DisplayEngine

router = APIRouter(prefix="/api/display", tags=["display"])


class LayoutItem(BaseModel):
    slot: int
    widget: str


class LayoutUpdate(BaseModel):
    layout: list[LayoutItem]


@router.get("/layout")
async def get_layout() -> dict:
    layout = await get_display_layout()
    return {"layout": layout, "widgets": sorted(DisplayEngine.WIDGETS)}


@router.put("/layout")
async def update_layout(body: LayoutUpdate) -> dict:
    layout = [item.model_dump() for item in body.layout]
    await set_display_layout(layout)
    return {"ok": True, "layout": layout}
