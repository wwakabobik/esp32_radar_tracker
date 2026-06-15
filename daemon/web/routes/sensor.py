from __future__ import annotations

from fastapi import APIRouter, Request

router = APIRouter(prefix="/api/sensor", tags=["sensor"])


@router.get("/log")
async def sensor_log(
    request: Request,
    since: int = 0,
    limit: int = 200,
    kind: str | None = None,
) -> dict:
    daemon = request.app.state.daemon
    entries = daemon.sensor_log.snapshot(since_seq=since, limit=min(limit, 500), kind=kind or None)
    return {
        "last_seq": daemon.sensor_log.last_seq,
        "entries": entries,
    }


@router.post("/log/clear")
async def clear_sensor_log(request: Request) -> dict:
    request.app.state.daemon.sensor_log.clear()
    return {"ok": True}
