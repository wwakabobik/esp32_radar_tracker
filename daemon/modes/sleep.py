from __future__ import annotations

from datetime import datetime, timezone

from db import (
    append_sleep_movement,
    append_sleep_radar_sample,
    compute_sleep_phases,
    get_setting,
    get_sleep_night,
    get_sleep_week,
    set_setting,
    upsert_sleep_night,
    _parse_json_list,
)


def _night_date(ts: float | None = None) -> str:
    when = datetime.fromtimestamp(ts or datetime.now().timestamp())
    return when.strftime("%Y-%m-%d")


def _fmt_ts(ts: float | None) -> str:
    if not ts:
        return "—"
    return datetime.fromtimestamp(ts).strftime("%H:%M")


def _night_phases(night: dict) -> dict:
    start = night.get("sleep_start")
    end = night.get("sleep_end")
    movement_times = [float(t) for t in _parse_json_list(night.get("movement_times"))]
    radar_samples = _parse_json_list(night.get("radar_samples"))
    return compute_sleep_phases(start, end, movement_times, radar_samples)


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
            # Auto sleep only after explicit "went to bed" (Btn1) — not from stillness alone.
            return

    async def last_night_summary(self) -> dict:
        sleep_start = await get_setting("last_sleep_start")
        sleep_end = await get_setting("last_sleep_end")
        night = await get_sleep_night()
        phases = _night_phases(night) if night else {}
        return {
            "asleep": self.asleep,
            "sleep_start": float(sleep_start) if sleep_start else None,
            "sleep_end": float(sleep_end) if sleep_end else None,
            "movements": int(night.get("movements", self.movements)) if night else self.movements,
            "night_date": night.get("night_date") if night else _night_date(),
            "phases": {
                "calm_pct": phases.get("deep_pct", 0),
                "restless_pct": phases.get("light_pct", 0),
                "awake_pct": phases.get("awake_pct", 0),
            },
        }

    async def format_sleep_summary_text(self) -> str:
        night = await get_sleep_night()
        if not night or not night.get("sleep_start"):
            status = "asleep now" if self.asleep else "no recorded sleep tonight"
            return f"Sleep: {status}."

        start = float(night["sleep_start"])
        end = float(night["sleep_end"]) if night.get("sleep_end") else None
        movements = int(night.get("movements", 0) or self.movements)
        phases = _night_phases(night)

        lines = [
            f"Sleep ({night.get('night_date', _night_date())})",
            f"Asleep now: {'yes' if self.asleep else 'no'}",
            f"Start: {_fmt_ts(start)}",
            f"End: {_fmt_ts(end) if end else '— (in progress)'}",
        ]
        if end and end > start:
            lines.append(f"Duration: {(end - start) / 3600:.1f}h")
        lines.extend(
            [
                f"Movements: {movements}",
                (
                    f"Calm: {phases.get('deep_pct', 0)}% | "
                    f"Restless: {phases.get('light_pct', 0)}% | "
                    f"Awake: {phases.get('awake_pct', 0)}%"
                ),
            ]
        )
        if phases.get("breath_rate_bpm") is not None:
            lines.append(f"Breath rate (est.): {phases['breath_rate_bpm']} bpm")
        lines.append("(Radar estimate — not clinical sleep stages)")
        return "\n".join(lines)

    async def format_sleep_week_text(self) -> str:
        nights = await get_sleep_week()
        lines = ["Last 7 nights:"]
        for night in nights:
            hours = night.get("hours", 0) or 0
            if hours <= 0:
                lines.append(f"{night['date']}: —")
                continue
            phases = night.get("phases") or {}
            lines.append(
                f"{night['date']}: {hours:.1f}h, mov {night.get('movements', 0)}, "
                f"calm {phases.get('deep_pct', 0)}%, restless {phases.get('light_pct', 0)}%"
            )
        lines.append("(Calm/restless from radar motion energy)")
        return "\n".join(lines)

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
