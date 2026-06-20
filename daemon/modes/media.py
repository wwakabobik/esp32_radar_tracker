from __future__ import annotations

import asyncio
import logging
import shutil
import time

from db import get_setting

logger = logging.getLogger(__name__)

_SPOTIFY_META_SCRIPT = """
tell application "Spotify"
    if not (player state is playing) then
        if current track is missing value then
            return "" & tab & "" & tab & "0" & tab & "0" & tab & "stopped"
        end if
        set t to current track
        return (artist of t) & tab & (name of t) & tab & ((duration of t) / 1000) & tab & (player position) & tab & "paused"
    end if
    set t to current track
    return (artist of t) & tab & (name of t) & tab & ((duration of t) / 1000) & tab & (player position) & tab & "playing"
end tell
""".strip()


def _fmt_mmss(seconds: float) -> str:
    total = max(0, int(seconds))
    minutes, secs = divmod(total, 60)
    return f"{minutes}:{secs:02d}"


class MediaController:
    def __init__(self) -> None:
        self.track_title = ""
        self.track_artist = ""
        self.track_duration_sec = 0
        self.track_position_sec = 0.0
        self.is_playing = False
        self.last_gesture = ""
        self.last_gesture_ts: float | None = None
        self._last_fetch = 0.0
        self._position_at_fetch = 0.0

    def format_track_display(self) -> str:
        title = self.track_title.strip()
        artist = self.track_artist.strip()
        if not title and not artist:
            return ""
        if title and artist:
            base = f"{artist} - {title}"
        else:
            base = title or artist
        if self.is_playing and self.track_duration_sec > 0:
            pos = _fmt_mmss(self.track_position_sec)
            dur = _fmt_mmss(self.track_duration_sec)
            return f"{base}  {pos}/{dur}"
        return base

    async def on_mode_change(self, mode: str) -> None:
        if mode == "media":
            await self.refresh_track(force=True)

    async def on_radar(self, data: dict) -> None:
        return

    async def on_gesture(self, data: dict) -> None:
        gesture = data.get("type")
        value = data.get("value", 0)
        ts = data.get("ts")
        self.last_gesture = f"{gesture}:{value}"
        self.last_gesture_ts = float(ts) if ts is not None else time.time()
        backend = await get_setting("media_backend", "spotify")

        if gesture == "next":
            await self._next_track(backend)
        elif gesture == "prev":
            await self._prev_track(backend)
        elif gesture == "vol":
            await self._set_volume(backend, int(value))
        await self.refresh_track(force=True)

    async def _next_track(self, backend: str) -> None:
        if backend == "spotify":
            await self._osascript(
                'tell application "Spotify" to play next track',
                fallback_key=124,
            )
        else:
            await self._media_key(124)

    async def _prev_track(self, backend: str) -> None:
        if backend == "spotify":
            await self._osascript(
                'tell application "Spotify" to play previous track',
                fallback_key=123,
            )
        else:
            await self._media_key(123)

    async def _set_volume(self, backend: str, value: int) -> None:
        level = max(0, min(100, value))
        if backend == "spotify":
            await self._osascript(f'tell application "Spotify" to set sound volume to {level}')
        else:
            await self._osascript(
                f'tell application "System Events" to set volume output volume to {level}'
            )

    async def refresh_track(self, *, force: bool = False) -> None:
        now = time.monotonic()
        if (
            not force
            and self.is_playing
            and self._last_fetch
            and now - self._last_fetch < 5.0
        ):
            self.track_position_sec = min(
                float(self.track_duration_sec),
                self._position_at_fetch + (now - self._last_fetch),
            )
            return

        backend = await get_setting("media_backend", "spotify")
        if backend == "spotify":
            meta = await self._osascript_output(_SPOTIFY_META_SCRIPT)
            if meta is not None:
                self._apply_meta(meta)
                return
        meta = await self._nowplaying_meta()
        if meta is not None:
            self._apply_meta(meta)

    def _apply_meta(self, raw: str) -> None:
        parts = raw.split("\t")
        while len(parts) < 5:
            parts.append("")
        artist, title, duration_s, position_s, state = parts[:5]
        self.track_artist = artist.strip()
        self.track_title = title.strip()
        try:
            self.track_duration_sec = max(0, int(float(duration_s or 0)))
        except ValueError:
            self.track_duration_sec = 0
        try:
            self.track_position_sec = max(0.0, float(position_s or 0))
        except ValueError:
            self.track_position_sec = 0.0
        self.is_playing = state.strip().lower() == "playing"
        self._last_fetch = time.monotonic()
        self._position_at_fetch = self.track_position_sec

    async def _nowplaying_meta(self) -> str | None:
        if not shutil.which("nowplaying-cli"):
            return None
        try:
            proc = await asyncio.create_subprocess_exec(
                "nowplaying-cli",
                "get",
                "title",
                "artist",
                "duration",
                "elapsed",
                "playing",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            stdout, _ = await proc.communicate()
            if proc.returncode != 0 or not stdout:
                return None
            parts = stdout.decode().strip().splitlines()
            while len(parts) < 5:
                parts.append("")
            title, artist, duration_s, position_s, playing = parts[:5]
            state = "playing" if playing.strip().lower() in {"1", "true", "yes", "playing"} else "paused"
            return f"{artist}\t{title}\t{duration_s}\t{position_s}\t{state}"
        except Exception:
            return None

    async def _osascript(self, script: str, fallback_key: int | None = None) -> None:
        ok = await self._run_osascript(script)
        if not ok and fallback_key is not None:
            await self._media_key(fallback_key)

    async def _osascript_output(self, script: str) -> str | None:
        try:
            proc = await asyncio.create_subprocess_exec(
                "osascript",
                "-e",
                script,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            stdout, _ = await proc.communicate()
            if proc.returncode == 0 and stdout:
                return stdout.decode().strip()
        except Exception:
            logger.debug("osascript failed")
        return None

    async def _run_osascript(self, script: str) -> bool:
        try:
            proc = await asyncio.create_subprocess_exec(
                "osascript",
                "-e",
                script,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
            )
            await proc.communicate()
            return proc.returncode == 0
        except Exception:
            return False

    async def _media_key(self, key_code: int) -> None:
        script = f'tell application "System Events" to key code {key_code} using {{}}'
        await self._run_osascript(script)
