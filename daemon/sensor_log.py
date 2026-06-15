from __future__ import annotations

import time
from collections import deque
from typing import Any

MAX_SENSOR_LOG = 2000


class SensorLog:
    def __init__(self, capacity: int = MAX_SENSOR_LOG) -> None:
        self._entries: deque[dict[str, Any]] = deque(maxlen=capacity)
        self._seq = 0

    def append(self, kind: str, mode: str, payload: dict, *, device_ts: float | None = None) -> int:
        self._seq += 1
        entry = {
            "seq": self._seq,
            "hub_ts": time.time(),
            "device_ts": device_ts,
            "kind": kind,
            "mode": mode,
            "payload": payload,
        }
        self._entries.append(entry)
        return self._seq

    def snapshot(self, *, since_seq: int = 0, limit: int = 200, kind: str | None = None) -> list[dict]:
        items = [e for e in self._entries if e["seq"] > since_seq]
        if kind:
            items = [e for e in items if e["kind"] == kind]
        if limit > 0:
            items = items[-limit:]
        return items

    def clear(self) -> None:
        self._entries.clear()

    @property
    def last_seq(self) -> int:
        return self._seq
