"""Optional Telegram push notifications."""
from __future__ import annotations

import logging

from aiogram import Bot

from config import TELEGRAM_CHAT_ID, TELEGRAM_TOKEN

logger = logging.getLogger(__name__)
_bot: Bot | None = None


def _bot() -> Bot | None:
    global _bot
    if not TELEGRAM_TOKEN or not TELEGRAM_CHAT_ID:
        return None
    if _bot is None:
        _bot = Bot(token=TELEGRAM_TOKEN)
    return _bot


async def send_telegram(text: str) -> bool:
    bot = _bot()
    if not bot:
        return False
    try:
        await bot.send_message(chat_id=TELEGRAM_CHAT_ID, text=text)
        return True
    except Exception:
        logger.exception("Telegram send failed")
        return False
