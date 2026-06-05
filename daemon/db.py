import json
from datetime import datetime, timezone
from pathlib import Path

import aiosqlite

from config import DB_PATH, DEFAULT_DISPLAY_LAYOUT


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
    movements INTEGER DEFAULT 0
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
        cursor = await db.execute("SELECT COUNT(*) FROM display_layout")
        count = (await cursor.fetchone())[0]
        if count == 0:
            for item in DEFAULT_DISPLAY_LAYOUT:
                await db.execute(
                    "INSERT INTO display_layout(slot, widget) VALUES (?, ?)",
                    (item["slot"], item["widget"]),
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
        cursor = await db.execute("SELECT slot, widget FROM display_layout ORDER BY slot")
        rows = await cursor.fetchall()
        return [{"slot": row["slot"], "widget": row["widget"]} for row in rows]


async def set_display_layout(layout: list[dict]) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("DELETE FROM display_layout")
        for item in layout:
            await db.execute(
                "INSERT INTO display_layout(slot, widget) VALUES (?, ?)",
                (item["slot"], item["widget"]),
            )
        await db.commit()


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
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            "INSERT INTO sessions(started_at, mode, paused) VALUES (?, ?, 0)",
            (now, mode),
        )
        await db.commit()
        return cursor.lastrowid


async def end_session(session_id: int) -> None:
    now = datetime.now(timezone.utc).timestamp()
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE sessions SET ended_at = ? WHERE id = ?",
            (now, session_id),
        )
        await db.commit()


async def toggle_session_pause(session_id: int, paused: bool) -> None:
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "UPDATE sessions SET paused = ? WHERE id = ?",
            (1 if paused else 0, session_id),
        )
        await db.commit()


async def get_today_work_seconds() -> int:
    start_of_day = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    async with aiosqlite.connect(DB_PATH) as db:
        cursor = await db.execute(
            """
            SELECT started_at, ended_at, paused FROM sessions
            WHERE mode = 'work' AND started_at >= ?
            """,
            (start_of_day,),
        )
        rows = await cursor.fetchall()
    now = datetime.now().timestamp()
    total = 0
    for started_at, ended_at, paused in rows:
        if paused:
            continue
        total += int((ended_at or now) - started_at)
    return total
