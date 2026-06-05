from __future__ import annotations

from datetime import datetime, timezone

from db import get_setting, set_setting


class SleepAnalyzer:
    def __init__(self) -> None:
        self.breathing_stable = 0
        self.asleep = False
        self.sleep_start: float | None = None
        self.sleep_end: float | None = None
        self.movements = 0

    async def on_mode_change(self, mode: str) -> None:
        if mode != "sleep":
            if self.asleep and self.sleep_start:
                self.sleep_end = datetime.now(timezone.utc).timestamp()
                await set_setting("last_sleep_end", str(self.sleep_end))
            self.asleep = False
            self.breathing_stable = 0
        else:
            self.sleep_start = None
            self.sleep_end = None
            self.movements = 0

    async def on_radar(self, data: dict) -> None:
        if not data.get("presence"):
            self.breathing_stable = max(0, self.breathing_stable - 1)
            if self.breathing_stable == 0 and self.asleep:
                self.movements += 1
            return

        s_energy = data.get("s_energy", 0)
        s_dist = data.get("s_dist", 0)
        if s_energy > 5 and 0 < s_dist < 120:
            self.breathing_stable += 1
        else:
            self.breathing_stable = max(0, self.breathing_stable - 1)

        if self.breathing_stable >= 5 and not self.asleep:
            self.asleep = True
            self.sleep_start = datetime.now(timezone.utc).timestamp()
            await set_setting("last_sleep_start", str(self.sleep_start))

    async def last_night_summary(self) -> dict:
        sleep_start = await get_setting("last_sleep_start")
        sleep_end = await get_setting("last_sleep_end")
        return {
            "asleep": self.asleep,
            "sleep_start": float(sleep_start) if sleep_start else None,
            "sleep_end": float(sleep_end) if sleep_end else None,
            "movements": self.movements,
        }
