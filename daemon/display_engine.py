from __future__ import annotations

from datetime import datetime

from config import DISPLAY_FONTS, STANDUP_INTERVAL_MIN
from db import get_display_layout, get_setting

SESSION_FLASH_SECONDS = 10


def _fmt_duration(seconds: int) -> str:
    hours, rem = divmod(max(0, seconds), 3600)
    minutes, secs = divmod(rem, 60)
    if hours:
        return f"{hours}h {minutes:02d}m"
    if minutes:
        return f"{minutes}m"
    return f"{secs}s"


class DisplayEngine:
    WIDGETS = {
        "clock",
        "date",
        "session",
        "today",
        "track",
        "reminder",
        "standup_timer",
        "mode",
        "status",
        "sleep",
    }

    def __init__(self, work, sleep, media) -> None:
        self.work = work
        self.sleep = sleep
        self.media = media
        self._publisher = None
        self._session_flash_until: float = 0.0
        self._session_flash_minute: int | None = None

    def set_publisher(self, publisher) -> None:
        self._publisher = publisher

    def _tick_session_flash(self, now: datetime) -> None:
        minute = now.minute
        if self._session_flash_minute != minute:
            self._session_flash_minute = minute
            self._session_flash_until = now.timestamp() + SESSION_FLASH_SECONDS

    def _in_session_flash(self, now: datetime) -> bool:
        return now.timestamp() < self._session_flash_until

    async def _build_work_widgets(self) -> list[dict]:
        now = datetime.now()

        if self.work.reminder_active:
            standup_min = int(
                await get_setting("standup_min", str(STANDUP_INTERVAL_MIN)) or STANDUP_INTERVAL_MIN
            )
            sec = await self.work.standup_countdown_seconds(standup_min)
            return [
                {"pos": 0, "text": "! Stand up", "font": "large", "center": True},
                {"pos": 1, "text": f"{sec // 60:02d}:{sec % 60:02d}", "font": "large", "center": True},
            ]

        self._tick_session_flash(now)
        if self._in_session_flash(now):
            sess = await self.work.session_seconds()
            return [
                {"pos": 0, "text": f"> {_fmt_duration(sess)}", "font": "large", "center": True},
            ]

        return [
            {"pos": 0, "text": now.strftime("%H:%M"), "font": "xlarge", "center": True},
        ]

    async def _build_media_widgets(self) -> list[dict]:
        now = datetime.now()
        widgets: list[dict] = [
            {"pos": 0, "text": now.strftime("%H:%M"), "font": "xlarge", "center": True},
        ]
        text = self.media.format_track_display()
        if text:
            widgets.append({"pos": 1, "text": text, "font": "medium", "scroll": True})
        return widgets

    async def _line_count(self) -> int:
        raw = await get_setting("display_line_count", "2")
        try:
            count = int(raw or 2)
        except ValueError:
            count = 2
        return max(0, min(2, count))

    def _effective_widget(self, widget: str, mode: str) -> str:
        if mode == "media" and widget == "session":
            return "track"
        return widget

    async def build_payload(self, mode: str) -> dict:
        line_count = await self._line_count()
        if mode == "media":
            await self.media.refresh_track()

        if mode == "work" and line_count > 0:
            widgets = await self._build_work_widgets()
        elif mode == "media" and line_count > 0:
            widgets = await self._build_media_widgets()
        elif line_count > 0:
            layout = await get_display_layout()
            widgets = []
            for item in layout:
                if item["slot"] >= line_count:
                    continue
                widget = self._effective_widget(item["widget"], mode)
                rendered = await self._render_widget(widget, mode)
                if not rendered:
                    continue
                entry = {
                    "pos": item["slot"],
                    "text": rendered["text"],
                    "font": item.get("font", "medium"),
                }
                if rendered.get("scroll"):
                    entry["scroll"] = True
                widgets.append(entry)
        else:
            widgets = []
        brightness = int(await get_setting("display_brightness", "255") or 255)
        sleep_display = await get_setting("sleep_display_mode", "off")
        return {
            "brightness": max(10, min(255, brightness)),
            "sleep_display_mode": sleep_display,
            "line_count": line_count,
            "widgets": widgets,
        }

    async def build_preview(self, layout: list[dict], mode: str, line_count: int | None = None) -> dict:
        if line_count is None:
            line_count = await self._line_count()
        else:
            line_count = max(0, min(2, line_count))
        widgets = []
        if line_count > 0:
            for item in layout:
                if item["slot"] >= line_count:
                    continue
                widget = self._effective_widget(item["widget"], mode)
                rendered = await self._render_widget(widget, mode)
                widgets.append(
                    {
                        "pos": item["slot"],
                        "text": rendered["text"] if rendered else f"({widget})",
                        "font": item.get("font", "medium"),
                        **({"scroll": True} if rendered and rendered.get("scroll") else {}),
                    }
                )
        brightness = int(await get_setting("display_brightness", "255") or 255)
        return {
            "brightness": brightness,
            "widgets": widgets,
            "fonts": list(DISPLAY_FONTS),
            "line_count": line_count,
        }

    async def _render_widget(self, widget: str, mode: str) -> dict | None:
        now = datetime.now()
        if widget == "clock":
            return {"text": now.strftime("%H:%M")}
        if widget == "date":
            return {"text": now.strftime("%d.%m.%Y")}
        if widget == "session":
            return {"text": f"Sess {_fmt_duration(await self.work.session_seconds())}"}
        if widget == "today":
            return {"text": f"Today {_fmt_duration(await self.work.today_seconds())}"}
        if widget == "track":
            text = self.media.format_track_display()
            if not text:
                return None
            return {"text": text, "scroll": True}
        if widget == "reminder":
            return {"text": "Stand up!"} if self.work.reminder_active else None
        if widget == "standup_timer":
            standup_min = int(await get_setting("standup_min", str(STANDUP_INTERVAL_MIN)) or STANDUP_INTERVAL_MIN)
            sec = await self.work.standup_countdown_seconds(standup_min)
            return {"text": f"Stand {sec // 60:02d}:{sec % 60:02d}"}
        if widget == "mode":
            return {"text": mode.upper()}
        if widget == "status":
            return {"text": "Here" if self.work.present else "Away"}
        if widget == "sleep":
            if self.sleep.asleep:
                return {"text": "Sleep Zzz"}
            return {"text": "Awake"}
        return None
