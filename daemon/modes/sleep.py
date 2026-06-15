from __future__ import annotations

from datetime import datetime, timezone

from db import (
    append_sleep_movement,
    append_sleep_radar_sample,
    get_setting,
    get_sleep_night,
    set_setting,
    upsert_sleep_night,
)


def _night_date(ts: float | None = None) -> str:
    when = datetime.fromtimestamp(ts or datetime.now().timestamp())
    return when.strftime("%Y-%m-%d")


class SleepAnalyzer:
    def __init__(self) -> None:
        self.breathing_stable = 0
        self.asleep = False
        self.sleep_start: float | None = None
        self.sleep_end: float | None = None
        self.movements = 0
        self._morning_sent_for: str | None = None

    async def on_mode_change(self, mode: str, replay_ts: float | None = None) -> None:
        ts = replay_ts if replay_ts is not None else datetime.now(timezone.utc).timestamp()
        if mode != "sleep":
            if self.asleep and self.sleep_start:
                self.sleep_end = ts
                await set_setting("last_sleep_end", str(self.sleep_end))
                await upsert_sleep_night(sleep_end=self.sleep_end, movements=self.movements)
            self.asleep = False
            self.breathing_stable = 0
            if mode == "work":
                await self._maybe_morning_summary()
        else:
            self.sleep_start = None
            self.sleep_end = None
            self.movements = 0

    async def manual_sleep_start(self, replay_ts: float | None = None) -> None:
        ts = replay_ts if replay_ts is not None else datetime.now(timezone.utc).timestamp()
        self.asleep = True
        self.sleep_start = ts
        self.sleep_end = None
        await set_setting("last_sleep_start", str(self.sleep_start))
        await upsert_sleep_night(sleep_start=self.sleep_start, movements=self.movements)

    async def manual_wake(self, replay_ts: float | None = None) -> None:
        ts = replay_ts if replay_ts is not None else datetime.now(timezone.utc).timestamp()
        self.asleep = False
        self.sleep_end = ts
        await set_setting("last_sleep_end", str(self.sleep_end))
        await upsert_sleep_night(sleep_end=self.sleep_end, movements=self.movements)

    async def record_movement(self, ts: float | None = None) -> None:
        when = ts if ts is not None else datetime.now(timezone.utc).timestamp()
        night = _night_date(when)
        self.movements = await append_sleep_movement(night, when)

    async def on_radar(self, data: dict) -> None:
        ts = float(data.get("ts") or datetime.now(timezone.utc).timestamp())
        night = _night_date(ts)

        if self.asleep:
            await append_sleep_radar_sample(
                night,
                {
                    "ts": ts,
                    "s_energy": data.get("s_energy", 0),
                    "m_energy": data.get("m_energy", 0),
                    "s_dist": data.get("s_dist", 0),
                    "presence": bool(data.get("presence")),
                },
            )

        if not data.get("presence"):
            self.breathing_stable = max(0, self.breathing_stable - 1)
            if self.breathing_stable == 0 and self.asleep:
                await self.record_movement(ts)
            return

        s_energy = data.get("s_energy", 0)
        s_dist = data.get("s_dist", 0)
        if s_energy > 5 and 0 < s_dist < 120:
            self.breathing_stable += 1
        else:
            self.breathing_stable = max(0, self.breathing_stable - 1)

        if self.breathing_stable >= 5 and not self.asleep:
            # Auto sleep only after explicit "лёг" (Btn1) — not from stillness alone.
            return

    async def last_night_summary(self) -> dict:
        sleep_start = await get_setting("last_sleep_start")
        sleep_end = await get_setting("last_sleep_end")
        night = await get_sleep_night()
        return {
            "asleep": self.asleep,
            "sleep_start": float(sleep_start) if sleep_start else None,
            "sleep_end": float(sleep_end) if sleep_end else None,
            "movements": self.movements,
            "night": night,
        }

    async def _maybe_morning_summary(self) -> None:
        if (await get_setting("morning_summary_enabled", "1")) != "1":
            return
        hour_min = int(await get_setting("morning_summary_hour", "8") or 8)
        now = datetime.now()
        if now.hour < hour_min:
            return
        today = now.strftime("%Y-%m-%d")
        if self._morning_sent_for == today:
            return
        night = await get_sleep_night()
        if not night or not night.get("sleep_end"):
            return
        from notify import send_telegram

        start = night.get("sleep_start")
        end = night.get("sleep_end")
        if start and end and end > start:
            hours = (end - start) / 3600
            text = (
                f"Good morning. Sleep: {hours:.1f}h, "
                f"movements: {night.get('movements', 0)}"
            )
            if await send_telegram(text):
                self._morning_sent_for = today
