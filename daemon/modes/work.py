from __future__ import annotations

from datetime import datetime, timezone

from config import WORK_TRACKING_MODES
from db import end_session, get_active_session, get_today_work_seconds, start_session


class WorkTracker:
    def __init__(self) -> None:
        self.present = False
        self.last_standup_at: datetime | None = None
        self.reminder_active = False

    async def on_mode_change(self, mode: str) -> None:
        if mode in WORK_TRACKING_MODES:
            await self.ensure_session()
        elif mode == "sleep":
            session = await get_active_session()
            if session:
                await end_session(session["id"])

    async def ensure_session(self) -> None:
        if not await get_active_session():
            await start_session("work")

    async def on_radar(self, data: dict) -> None:
        self.present = bool(data.get("presence"))

    async def session_seconds(self) -> int:
        session = await get_active_session()
        if not session:
            return 0
        return max(0, int(datetime.now(timezone.utc).timestamp() - session["started_at"]))

    async def today_seconds(self) -> int:
        return await get_today_work_seconds()

    async def check_standup(self, interval_min: int) -> bool:
        if not self.present:
            return False
        if not await get_active_session():
            return False
        now = datetime.now(timezone.utc)
        if self.last_standup_at is None:
            self.last_standup_at = now
            return False
        elapsed = (now - self.last_standup_at).total_seconds() / 60
        if elapsed >= interval_min:
            self.last_standup_at = now
            self.reminder_active = True
            return True
        return False

    async def standup_countdown_seconds(self, interval_min: int) -> int:
        if not self.present or self.last_standup_at is None:
            return interval_min * 60
        elapsed = (datetime.now(timezone.utc) - self.last_standup_at).total_seconds()
        remaining = interval_min * 60 - elapsed
        return max(0, int(remaining))

    def clear_reminder(self) -> None:
        self.reminder_active = False
