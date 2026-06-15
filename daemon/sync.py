from __future__ import annotations

import json
import logging
from datetime import datetime, timezone
from typing import TYPE_CHECKING

from aiomqtt import Client

from config import TOPIC_SYNC_ACK, WORK_TRACKING_MODES
from db import (
    append_sleep_movement,
    end_session_at,
    get_active_session,
    get_last_event_id,
    set_last_event_id,
    start_session_at,
    upsert_sleep_night,
)

if TYPE_CHECKING:
    from hub import HubDaemon

logger = logging.getLogger(__name__)


def _event_id(event: dict) -> int | None:
    eid = event.get("id") or event.get("eid")
    return int(eid) if eid else None


def _event_ts(event: dict) -> float:
    ts = event.get("ts")
    if ts:
        return float(ts)
    return datetime.now(timezone.utc).timestamp()


def _night_date(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d")


async def should_skip_event(event_id: int | None) -> bool:
    if not event_id:
        return False
    last = await get_last_event_id()
    return event_id <= last


async def publish_ack(client: Client, ack_id: int) -> None:
    await client.publish(TOPIC_SYNC_ACK, json.dumps({"ack_id": ack_id}), qos=1)


async def apply_event(daemon: HubDaemon, event: dict, *, client: Client | None = None) -> bool:
    """Apply one device event. Returns True if applied (not duplicate)."""
    eid = _event_id(event)
    if await should_skip_event(eid):
        return False

    etype = event.get("type")
    mode = event.get("mode", daemon.mode)
    data = event.get("data") or {}
    ts = _event_ts(event)

    if etype == "mode":
        new_mode = data.get("mode") or mode
        if new_mode in {"work", "sleep", "media"}:
            await daemon.set_mode(new_mode, replay_ts=ts)
    elif etype == "presence" and mode in WORK_TRACKING_MODES:
        present = bool(data.get("present"))
        session = await get_active_session()
        if present and not session:
            await start_session_at("work", ts)
            daemon.work.present = True
            daemon.work.last_standup_at = datetime.fromtimestamp(ts, tz=timezone.utc)
        elif not present and session:
            await end_session_at(session["id"], ts)
            daemon.work.present = False
        else:
            daemon.work.present = present
    elif etype == "button":
        await _apply_button(daemon, mode, data, ts)
    elif etype == "gesture" and mode == "media":
        await daemon.media.on_gesture(data)
    elif etype == "sleep_start":
        await upsert_sleep_night(sleep_start=ts, night_date=_night_date(ts))
        daemon.sleep.asleep = True
        daemon.sleep.sleep_start = ts
    elif etype == "sleep_end":
        await upsert_sleep_night(sleep_end=ts, night_date=_night_date(ts))
        daemon.sleep.asleep = False
        daemon.sleep.sleep_end = ts
    elif etype == "sleep_movement":
        night = _night_date(ts)
        movements = await append_sleep_movement(night, ts)
        daemon.sleep.movements = movements
    else:
        logger.debug("Unknown event type: %s", etype)
        return False

    if eid:
        await set_last_event_id(eid)
        if client:
            await publish_ack(client, eid)
    return True


async def _apply_button(daemon: HubDaemon, mode: str, data: dict, ts: float) -> None:
    btn_id = data.get("id")
    event = data.get("event")

    if mode == "sleep":
        if btn_id == 1 and event == "press":
            await daemon.sleep.manual_sleep_start(replay_ts=ts)
        elif btn_id == 2 and event == "press":
            await daemon.sleep.manual_wake(replay_ts=ts)
            await daemon.set_mode("media", replay_ts=ts)
        return

    if mode in WORK_TRACKING_MODES and btn_id == 1:
        session = await get_active_session()
        if event == "long":
            if session:
                await end_session_at(session["id"], ts)
            await start_session_at("work", ts)
            daemon.work.last_standup_at = datetime.fromtimestamp(ts, tz=timezone.utc)
            daemon.work.clear_reminder()
            return
        if event == "press":
            daemon.work.clear_reminder()


async def apply_sync_batch(daemon: HubDaemon, payload: dict, client: Client) -> int:
    events = sorted(payload.get("events") or [], key=lambda e: _event_id(e) or 0)
    for event in events:
        await apply_event(daemon, event, client=None)
    max_id = await get_last_event_id()
    if max_id:
        await publish_ack(client, max_id)
    logger.info("Sync batch: %d events, ack through %d", len(events), max_id)
    return max_id


async def apply_live_event(daemon: HubDaemon, event: dict, client: Client | None = None) -> bool:
    return await apply_event(daemon, event, client=client)
