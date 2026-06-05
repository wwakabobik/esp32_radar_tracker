from __future__ import annotations

from datetime import datetime

from config import STANDUP_INTERVAL_MIN
from db import get_display_layout, get_setting
from modes.media import MediaController
from modes.sleep import SleepAnalyzer
from modes.work import WorkTracker


def _fmt_duration(seconds: int) -> str:
    hours, rem = divmod(max(0, seconds), 3600)
    minutes, _ = divmod(rem, 60)
    if hours:
        return f"{hours}h {minutes:02d}m"
    return f"{minutes}m"


class DisplayEngine:
    WIDGETS = {"clock", "date", "session", "today", "track", "reminder", "standup_timer", "mode", "status"}

    def __init__(self, work: WorkTracker, sleep: SleepAnalyzer, media: MediaController) -> None:
        self.work = work
        self.sleep = sleep
        self.media = media
        self._publisher = None

    def set_publisher(self, publisher) -> None:
        self._publisher = publisher

    async def build_payload(self, mode: str) -> dict:
        layout = await get_display_layout()
        widgets = []
        for item in layout:
            text = await self._render_widget(item["widget"], mode)
            widgets.append({"pos": item["slot"], "text": text})
        return {"widgets": widgets}

    async def _render_widget(self, widget: str, mode: str) -> str:
        now = datetime.now()
        if widget == "clock":
            return now.strftime("%H:%M")
        if widget == "date":
            return now.strftime("%d.%m.%Y")
        if widget == "session":
            return f"Session {_fmt_duration(await self.work.session_seconds())}"
        if widget == "today":
            return f"Today {_fmt_duration(await self.work.today_seconds())}"
        if widget == "track":
            return self.media.current_track[:16]
        if widget == "reminder":
            return "Stand up!" if self.work.reminder_active else ""
        if widget == "standup_timer":
            standup_min = int(await get_setting("standup_min", str(STANDUP_INTERVAL_MIN)) or STANDUP_INTERVAL_MIN)
            sec = await self.work.standup_countdown_seconds(standup_min)
            return f"Stand {sec // 60:02d}:{sec % 60:02d}"
        if widget == "mode":
            return mode.upper()
        if widget == "status":
            return "Present" if self.work.present else "Away"
        return ""
