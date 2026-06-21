from __future__ import annotations

from datetime import datetime


def tz_offset_sec(now: datetime | None = None) -> int:
    when = now or datetime.now()
    offset = when.astimezone().utcoffset()
    return int(offset.total_seconds()) if offset is not None else 0
