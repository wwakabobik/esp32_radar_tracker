from __future__ import annotations

from fastapi import APIRouter, Request

from db import get_today_work_seconds

router = APIRouter(prefix="/api/dashboard", tags=["dashboard"])


@router.get("/today")
async def today_stats(request: Request) -> dict:
    daemon = request.app.state.daemon
    total = await get_today_work_seconds()
    return {
        "mode": daemon.mode,
        "online": daemon.online,
        "present": daemon.work.present,
        "today_seconds": total,
        "session_seconds": await daemon.work.session_seconds(),
    }
