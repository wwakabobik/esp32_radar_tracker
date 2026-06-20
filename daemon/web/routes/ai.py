from __future__ import annotations

from datetime import datetime, timezone

from fastapi import APIRouter, Request

from db import count_fatigue_events_today, get_ai_timeline

router = APIRouter(prefix="/api/ai", tags=["ai"])


@router.get("/today")
async def ai_today(request: Request) -> dict:
    daemon = request.app.state.daemon
    start = datetime.now(timezone.utc).replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    timeline = await get_ai_timeline(start)
    fatigue_count = await count_fatigue_events_today()
    return {
        "current_state": daemon.last_ai_state.get("state") or daemon.work.ai_state,
        "confidence": daemon.last_ai_state.get("confidence"),
        "fatigue_events_today": fatigue_count,
        "timeline": timeline[-200:],
    }


@router.get("/timeline")
async def ai_timeline(hours: int = 24) -> dict:
    since = datetime.now(timezone.utc).timestamp() - hours * 3600
    return {"entries": await get_ai_timeline(since)}
