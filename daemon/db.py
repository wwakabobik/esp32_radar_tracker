import json
from datetime import datetime, timezone

import aiosqlite

from config import DB_PATH, DEFAULT_DISPLAY_LAYOUT, DEFAULT_SETTINGS


SCHEMA = """
CREATE TABLE IF NOT EXISTS radar_samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts REAL NOT NULL,
    dist INTEGER,
    s_energy INTEGER,
    m_energy INTEGER,
    s_dist INTEGER,
    m_dist INTEGER,
    presence INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at REAL NOT NULL,
    ended_at REAL,
    mode TEXT NOT NULL DEFAULT 'work',
    paused INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS sleep_nights (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    night_date TEXT NOT NULL,
    sleep_start REAL,
    sleep_end REAL,
    quality REAL,
    movements INTEGER DEFAULT 0,
    movement_times TEXT NOT NULL DEFAULT '[]',
    radar_samples TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS display_layout (
    slot INTEGER PRIMARY KEY,
    widget TEXT NOT NULL
);
"""


async def init_db() -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.executescript(SCHEMA)
        try:
            await db.execute("ALTER TABLE display_layout ADD COLUMN font TEXT NOT NULL DEFAULT 'medium'")
        except aiosqlite.Error:
            pass
        for col in ("movement_times", "radar_samples"):
            try:
                await db.execute(
                    f"ALTER TABLE sleep_nights ADD COLUMN {col} TEXT NOT NULL DEFAULT '[]'"
                )
            except aiosqlite.Error:
                pass
        cursor = await db.execute("SELECT COUNT(*) FROM display_layout")
        count = (await cursor.fetchone())[0]
        if count == 0:
            for item in DEFAULT_DISPLAY_LAYOUT:
                await db.execute(
                    "INSERT INTO display_layout(slot, widget, font) VALUES (?, ?, ?)",
                    (item["slot"], item["widget"], item.get("font", "medium")),
                )
        for key, value in DEFAULT_SETTINGS.items():
            await db.execute(
                "INSERT OR IGNORE INTO settings(key, value) VALUES (?, ?)",
                (key, value),
            )
        await db.commit()


async def insert_radar_sample(payload: dict) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            """
            INSERT INTO radar_samples(ts, dist, s_energy, m_energy, s_dist, m_dist, presence)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                payload.get("ts", datetime.now(timezone.utc).timestamp()),
                payload.get("dist"),
                payload.get("s_energy"),
                payload.get("m_energy"),
                payload.get("s_dist"),
                payload.get("m_dist"),
                1 if payload.get("presence") else 0,
            ),
        )
        await db.commit()


async def get_setting(key: str, default: str | None = None) -> str | None:
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute("SELECT value FROM settings WHERE key = ?", (key,))
        row = await cursor.fetchone()
        return row[0] if row else default


async def set_setting(key: str, value: str) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "INSERT INTO settings(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            (key, value),
        )
        await db.commit()


async def get_display_layout() -> list[dict]:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT slot, widget, COALESCE(font, 'medium') AS font FROM display_layout ORDER BY slot"
        )
        rows = await cursor.fetchall()
        return [{"slot": row["slot"], "widget": row["widget"], "font": row["font"]} for row in rows]


async def set_display_layout(layout: list[dict]) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("DELETE FROM display_layout")
        for item in layout:
            await db.execute(
                "INSERT INTO display_layout(slot, widget, font) VALUES (?, ?, ?)",
                (item["slot"], item["widget"], item.get("font", "medium")),
            )
        await db.commit()


async def get_all_settings() -> dict[str, str]:
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute("SELECT key, value FROM settings")
        rows = await cursor.fetchall()
    result = dict(DEFAULT_SETTINGS)
    result.update({k: v for k, v in rows})
    return result


async def set_settings(values: dict[str, str]) -> None:
    for key, value in values.items():
        await set_setting(key, str(value))


async def get_active_session() -> dict | None:
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1"
        )
        row = await cursor.fetchone()
        return dict(row) if row else None


async def start_session(mode: str = "work") -> int:
    now = datetime.now(timezone.utc).timestamp()
    return await start_session_at(mode, now)


async def start_session_at(mode: str, started_at: float) -> int:
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "INSERT INTO sessions(started_at, mode, paused) VALUES (?, ?, 0)",
            (started_at, mode),
        )
        await db.commit()
        return cursor.lastrowid


async def end_session(session_id: int) -> None:
    now = datetime.now(timezone.utc).timestamp()
    await end_session_at(session_id, now)


async def end_session_at(session_id: int, ended_at: float) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE sessions SET ended_at = ? WHERE id = ?",
            (ended_at, session_id),
        )
        await db.commit()


async def get_last_event_id() -> int:
    raw = await get_setting("last_event_id", "0")
    return int(raw or 0)


async def set_last_event_id(event_id: int) -> None:
    await set_setting("last_event_id", str(event_id))


def _parse_json_list(raw: str | None) -> list:
    if not raw:
        return []
    try:
        data = json.loads(raw)
        return data if isinstance(data, list) else []
    except (json.JSONDecodeError, TypeError):
        return []


async def append_sleep_movement(night_date: str, ts: float) -> int:
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "SELECT id, movement_times, movements FROM sleep_nights WHERE night_date = ? ORDER BY id DESC LIMIT 1",
            (night_date,),
        )
        row = await cursor.fetchone()
        if row:
            rid, raw_times, cur_mov = row
            times = _parse_json_list(raw_times)
            times.append(ts)
            movements = int(cur_mov or 0) + 1
            await db.execute(
                "UPDATE sleep_nights SET movement_times = ?, movements = ? WHERE id = ?",
                (json.dumps(times), movements, rid),
            )
        else:
            movements = 1
            await db.execute(
                """
                INSERT INTO sleep_nights(night_date, movements, movement_times)
                VALUES (?, ?, ?)
                """,
                (night_date, movements, json.dumps([ts])),
            )
        await db.commit()
        return movements


async def append_sleep_radar_sample(night_date: str, sample: dict) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "SELECT id, radar_samples FROM sleep_nights WHERE night_date = ? ORDER BY id DESC LIMIT 1",
            (night_date,),
        )
        row = await cursor.fetchone()
        if not row:
            await db.execute(
                "INSERT INTO sleep_nights(night_date, radar_samples) VALUES (?, ?)",
                (night_date, json.dumps([sample])),
            )
            await db.commit()
            return
        rid, raw_samples = row
        samples = _parse_json_list(raw_samples)
        if samples:
            last = samples[-1]
            last_ts = float(last.get("ts", 0))
            if float(sample.get("ts", 0)) - last_ts < 30:
                return
        samples.append(sample)
        if len(samples) > 2000:
            samples = samples[-2000:]
        await db.execute(
            "UPDATE sleep_nights SET radar_samples = ? WHERE id = ?",
            (json.dumps(samples), rid),
        )
        await db.commit()


PHASE_BUCKET_SEC = 300


def _sleep_energy_baseline(radar_samples: list[dict], sleep_start: float) -> float:
    energies: list[int] = []
    for sample in radar_samples:
        ts = float(sample.get("ts", 0))
        if sleep_start <= ts < sleep_start + 1800:
            energies.append(int(sample.get("s_energy", 0)))
    if not energies:
        return 10.0
    energies.sort()
    return float(energies[len(energies) // 2])


def _phase_from_energy(avg_e: float, baseline: float) -> str:
    delta = avg_e - baseline
    if delta <= 2:
        return "deep"
    if delta <= 8:
        return "light"
    return "light"


def compute_sleep_phases(
    sleep_start: float | None,
    sleep_end: float | None,
    movement_times: list[float],
    radar_samples: list[dict],
) -> dict:
    if not sleep_start or not sleep_end or sleep_end <= sleep_start:
        return {
            "segments": [],
            "deep_pct": 0,
            "light_pct": 0,
            "awake_pct": 0,
            "deep_hours": 0,
            "light_hours": 0,
            "awake_hours": 0,
        }

    baseline = _sleep_energy_baseline(radar_samples, sleep_start)

    bucket_starts: list[float] = []
    t = sleep_start
    while t < sleep_end:
        bucket_starts.append(int(t // PHASE_BUCKET_SEC) * PHASE_BUCKET_SEC)
        t += PHASE_BUCKET_SEC

    energy_by_bucket: dict[float, list[int]] = {b: [] for b in bucket_starts}
    movement_buckets: set[float] = set()
    for m in movement_times:
        ts = float(m)
        if sleep_start <= ts <= sleep_end:
            b = int(ts // PHASE_BUCKET_SEC) * PHASE_BUCKET_SEC
            movement_buckets.add(b)
            movement_buckets.add(b - PHASE_BUCKET_SEC)
            movement_buckets.add(b + PHASE_BUCKET_SEC)

    for sample in radar_samples:
        ts = float(sample.get("ts", 0))
        if sleep_start <= ts <= sleep_end:
            b = int(ts // PHASE_BUCKET_SEC) * PHASE_BUCKET_SEC
            if b in energy_by_bucket:
                energy_by_bucket[b].append(int(sample.get("s_energy", 0)))

    segments: list[dict] = []
    counts = {"deep": 0.0, "light": 0.0, "awake": 0.0}
    for bucket_start in bucket_starts:
        bucket_end = min(bucket_start + PHASE_BUCKET_SEC, sleep_end)
        duration = bucket_end - bucket_start
        if bucket_start in movement_buckets:
            phase = "awake"
        else:
            energies = energy_by_bucket.get(bucket_start, [])
            if energies:
                avg_e = sum(energies) / len(energies)
                phase = _phase_from_energy(avg_e, baseline)
            else:
                phase = "light"
        counts[phase] += duration
        segments.append({"start": bucket_start, "end": bucket_end, "phase": phase})

    total = sum(counts.values()) or 1.0
    return {
        "segments": segments,
        "deep_pct": round(100 * counts["deep"] / total),
        "light_pct": round(100 * counts["light"] / total),
        "awake_pct": round(100 * counts["awake"] / total),
        "deep_hours": round(counts["deep"] / 3600, 2),
        "light_hours": round(counts["light"] / 3600, 2),
        "awake_hours": round(counts["awake"] / 3600, 2),
    }


async def get_today_work_seconds() -> int:
    start_of_day = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            """
            SELECT started_at, ended_at FROM sessions
            WHERE mode = 'work' AND started_at >= ?
            """,
            (start_of_day,),
        )
        rows = await cursor.fetchall()
    now = datetime.now().timestamp()
    total = 0
    for started_at, ended_at in rows:
        total += int((ended_at or now) - started_at)
    return total


async def get_week_work_seconds() -> list[dict]:
    """Last 7 days (oldest first): date ISO, seconds."""
    from datetime import timedelta

    today = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    result = []
    async with aiosqlite.connect(DB_PATH) as db:
        for offset in range(6, -1, -1):
            day_start = (today - timedelta(days=offset)).timestamp()
            day_end = day_start + 86400
            cursor = await db.execute(
                """
                SELECT started_at, ended_at FROM sessions
                WHERE mode = 'work' AND started_at >= ? AND started_at < ?
                """,
                (day_start, day_end),
            )
            rows = await cursor.fetchall()
            total = 0
            now = datetime.now().timestamp()
            for started_at, ended_at in rows:
                end = ended_at or min(now, day_end)
                if end <= started_at:
                    continue
                total += int(end - started_at)
            result.append(
                {
                    "date": (today - timedelta(days=offset)).strftime("%Y-%m-%d"),
                    "seconds": total,
                }
            )
    return result


def _day_bounds(day: datetime) -> tuple[float, float]:
    start = day.replace(hour=0, minute=0, second=0, microsecond=0)
    return start.timestamp(), start.timestamp() + 86400


def _intervals_from_samples(
    rows: list[tuple[float, int]],
    *,
    gap_threshold: float = 120.0,
    extend_end: float | None = None,
) -> list[dict]:
    if not rows:
        return []
    intervals: list[dict] = []
    cur_start, cur_pres = rows[0][0], bool(rows[0][1])
    last_ts = rows[0][0]
    for ts, pres in rows[1:]:
        pres = bool(pres)
        if ts - last_ts > gap_threshold:
            end = last_ts
            if extend_end and cur_pres and end < extend_end:
                end = extend_end
            intervals.append({"start": cur_start, "end": end, "present": cur_pres})
            cur_start, cur_pres = ts, pres
        elif pres != cur_pres:
            intervals.append({"start": cur_start, "end": ts, "present": cur_pres})
            cur_start, cur_pres = ts, pres
        last_ts = ts
    end = last_ts
    if extend_end and cur_pres and end < extend_end:
        end = extend_end
    intervals.append({"start": cur_start, "end": end, "present": cur_pres})
    return intervals


async def get_presence_timeline(day_start: float, day_end: float) -> list[dict]:
    """Presence/absence intervals from radar samples for one calendar day."""
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "SELECT ts, presence FROM radar_samples WHERE ts >= ? AND ts < ? ORDER BY ts",
            (day_start, day_end),
        )
        rows = await cursor.fetchall()
    now = datetime.now().timestamp()
    extend = min(now, day_end) if day_start <= now < day_end else None
    return _intervals_from_samples(rows, extend_end=extend)


async def get_presence_timeline_week() -> list[dict]:
    """Last 7 days: date + presence intervals (seconds since midnight local)."""
    from datetime import timedelta

    today = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    result = []
    for offset in range(6, -1, -1):
        day = today - timedelta(days=offset)
        day_start, day_end = _day_bounds(day)
        intervals = await get_presence_timeline(day_start, day_end)
        present_sec = sum(int(i["end"] - i["start"]) for i in intervals if i["present"])
        result.append(
            {
                "date": day.strftime("%Y-%m-%d"),
                "day_start": day_start,
                "intervals": intervals,
                "present_seconds": present_sec,
            }
        )
    return result


def _estimate_sleep_quality(
    movements: int,
    duration_hours: float,
    phases: dict | None = None,
) -> float:
    if duration_hours <= 0:
        return 0.0
    rate = movements / max(duration_hours, 0.5)
    movement_score = max(0.0, min(100.0, 100.0 - rate * 12.0))
    if phases:
        deep_bonus = phases.get("deep_pct", 0)
        awake_penalty = phases.get("awake_pct", 0) * 0.5
        return round(max(0.0, min(100.0, movement_score * 0.4 + deep_bonus * 0.6 - awake_penalty)), 1)
    return round(movement_score, 1)


async def get_sleep_week() -> list[dict]:
    """Last 7 nights (oldest first) with duration, movements, estimated quality."""
    from datetime import timedelta

    today = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    result = []
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        for offset in range(6, -1, -1):
            day = today - timedelta(days=offset)
            nd = day.strftime("%Y-%m-%d")
            cursor = await db.execute(
                "SELECT * FROM sleep_nights WHERE night_date = ? ORDER BY id DESC LIMIT 1",
                (nd,),
            )
            row = await cursor.fetchone()
            if not row or not row["sleep_start"]:
                result.append(
                    {
                        "date": nd,
                        "hours": 0,
                        "movements": 0,
                        "quality": 0,
                        "phases": {"deep_pct": 0, "light_pct": 0, "awake_pct": 0},
                    }
                )
                continue
            start = row["sleep_start"]
            end = row["sleep_end"] or datetime.now().timestamp()
            hours = max(0.0, (end - start) / 3600.0)
            movements = int(row["movements"] or 0)
            movement_times = [float(t) for t in _parse_json_list(row["movement_times"] if "movement_times" in row.keys() else "[]")]
            radar_samples = _parse_json_list(row["radar_samples"] if "radar_samples" in row.keys() else "[]")
            phases = compute_sleep_phases(start, end, movement_times, radar_samples)
            quality = row["quality"]
            if quality is None:
                quality = _estimate_sleep_quality(movements, hours, phases)
            result.append(
                {
                    "date": nd,
                    "sleep_start": start,
                    "sleep_end": row["sleep_end"],
                    "hours": round(hours, 2),
                    "movements": movements,
                    "quality": float(quality),
                    "phases": {
                        "deep_pct": phases["deep_pct"],
                        "light_pct": phases["light_pct"],
                        "awake_pct": phases["awake_pct"],
                        "deep_hours": phases["deep_hours"],
                        "light_hours": phases["light_hours"],
                        "awake_hours": phases["awake_hours"],
                    },
                    "phase_segments": phases["segments"] if row["sleep_end"] else [],
                }
            )
    return result


def _night_date(ts: float | None = None) -> str:
    when = datetime.fromtimestamp(ts or datetime.now().timestamp())
    return when.strftime("%Y-%m-%d")


async def upsert_sleep_night(
    *,
    sleep_start: float | None = None,
    sleep_end: float | None = None,
    movements: int | None = None,
    night_date: str | None = None,
) -> None:
    nd = night_date or _night_date(sleep_start)
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "SELECT id, sleep_start, sleep_end, movements FROM sleep_nights WHERE night_date = ?",
            (nd,),
        )
        row = await cursor.fetchone()
        if row:
            rid, cur_start, cur_end, cur_mov = row
            await db.execute(
                """
                UPDATE sleep_nights
                SET sleep_start = COALESCE(?, sleep_start),
                    sleep_end = COALESCE(?, sleep_end),
                    movements = COALESCE(?, movements)
                WHERE id = ?
                """,
                (sleep_start, sleep_end, movements, rid),
            )
        else:
            await db.execute(
                """
                INSERT INTO sleep_nights(night_date, sleep_start, sleep_end, movements)
                VALUES (?, ?, ?, ?)
                """,
                (nd, sleep_start, sleep_end, movements or 0),
            )
        await db.commit()


async def get_sleep_night(night_date: str | None = None) -> dict | None:
    nd = night_date or _night_date()
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM sleep_nights WHERE night_date = ? ORDER BY id DESC LIMIT 1",
            (nd,),
        )
        row = await cursor.fetchone()
        return dict(row) if row else None
