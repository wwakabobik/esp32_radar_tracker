from __future__ import annotations

import asyncio
import subprocess


class MediaController:
    def __init__(self) -> None:
        self.current_track = "No track"
        self.last_gesture = ""

    async def on_mode_change(self, mode: str) -> None:
        if mode == "media":
            await self.refresh_track()

    async def on_radar(self, data: dict) -> None:
        return

    async def on_gesture(self, data: dict) -> None:
        gesture = data.get("type")
        value = data.get("value", 0)
        self.last_gesture = f"{gesture}:{value}"
        if gesture == "next":
            await self._osascript('tell application "Music" to next track')
        elif gesture == "prev":
            await self._osascript('tell application "Music" to previous track')
        elif gesture == "vol":
            await self._osascript(f'tell application "Music" to set sound volume to {value}')
        await self.refresh_track()

    async def refresh_track(self) -> None:
        script = 'tell application "Music" to get name of current track & " - " & artist of current track'
        try:
            proc = await asyncio.create_subprocess_exec(
                "osascript", "-e", script,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            stdout, _ = await proc.communicate()
            if proc.returncode == 0 and stdout:
                self.current_track = stdout.decode().strip()[:32]
        except Exception:
            self.current_track = "Music unavailable"

    async def _osascript(self, script: str) -> None:
        proc = await asyncio.create_subprocess_exec(
            "osascript", "-e", script,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        await proc.communicate()
