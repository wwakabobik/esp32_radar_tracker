from pathlib import Path
import os

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent
load_dotenv(BASE_DIR / ".env")
DATA_DIR = BASE_DIR / "data"
DATA_DIR.mkdir(exist_ok=True)

MQTT_HOST = os.getenv("HUB_MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("HUB_MQTT_PORT", "18830"))
WEB_HOST = os.getenv("HUB_WEB_HOST", "127.0.0.1")
WEB_PORT = int(os.getenv("HUB_WEB_PORT", "18080"))
OTA_HOST = os.getenv("HUB_OTA_HOST", "127.0.0.1")
OTA_PORT = int(os.getenv("HUB_OTA_PORT", "18081"))
DB_PATH = Path(os.getenv("HUB_DB_PATH", str(DATA_DIR / "hub.db")))
FIRMWARE_DIR = Path(os.getenv("HUB_FIRMWARE_DIR", str(BASE_DIR.parent / "firmware" / ".pio" / "build" / "esp32dev")))
FIRMWARE_BIN = FIRMWARE_DIR / "firmware.bin"

TELEGRAM_TOKEN = os.getenv("HUB_TELEGRAM_TOKEN", "")
TELEGRAM_CHAT_ID = os.getenv("HUB_TELEGRAM_CHAT_ID", "")

STANDUP_INTERVAL_MIN = int(os.getenv("HUB_STANDUP_MIN", "120"))
MORNING_SUMMARY_HOUR = int(os.getenv("HUB_MORNING_HOUR", "8"))
DISCOVERY_PORT = int(os.getenv("HUB_DISCOVERY_PORT", "18832"))

WORK_TRACKING_MODES = frozenset({"work", "media"})

TOPIC_RADAR = "hub/radar"
TOPIC_MODE = "hub/mode"
TOPIC_BUTTON = "hub/button"
TOPIC_DISPLAY = "hub/display"
TOPIC_GESTURE = "hub/gesture"
TOPIC_OTA = "hub/ota/trigger"
TOPIC_STATUS = "hub/status"
TOPIC_SYNC_EVENTS = "hub/sync/events"
TOPIC_SYNC_ACK = "hub/sync/ack"
TOPIC_DEBUG_GESTURE = "hub/debug/gesture"

DEFAULT_DISPLAY_LAYOUT = [
    {"slot": 0, "widget": "clock", "font": "large"},
    {"slot": 1, "widget": "session", "font": "medium"},
]

DISPLAY_FONTS = ("small", "medium", "large")

TOPIC_CONFIG = "hub/config"

DEFAULT_SETTINGS = {
    "display_brightness": "255",
    "display_line_count": "2",
    "sleep_display_mode": "off",
    "standup_min": str(STANDUP_INTERVAL_MIN),
    "standup_enabled": "1",
    "telegram_standup_message": "Time to stand up and walk around — you have been at the desk for a while.",
    "morning_summary_enabled": "1",
    "morning_summary_hour": str(MORNING_SUMMARY_HOUR),
    "work_presence_min_cm": "20",
    "work_presence_max_cm": "400",
    "work_presence_type": "both",
    "sleep_breath_max_cm": "120",
    "sleep_energy_min": "5",
    "radar_gate_work": "6",
    "radar_gate_sleep": "8",
    "media_backend": "spotify",
    "gesture_zone_min_cm": "12",
    "gesture_zone_max_cm": "28",
    "gesture_hold_ms": "400",
    "gesture_debounce_ms": "1200",
    "gesture_debug": "0",
}
