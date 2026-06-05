from pathlib import Path
import os

BASE_DIR = Path(__file__).resolve().parent
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

STANDUP_INTERVAL_MIN = int(os.getenv("HUB_STANDUP_MIN", "45"))
DEVICE_MAC = os.getenv("HUB_DEVICE_MAC", "presence-hub")

TOPIC_RADAR = "hub/radar"
TOPIC_MODE = "hub/mode"
TOPIC_BUTTON = "hub/button"
TOPIC_DISPLAY = "hub/display"
TOPIC_GESTURE = "hub/gesture"
TOPIC_OTA = "hub/ota/trigger"
TOPIC_STATUS = "hub/status"

DEFAULT_DISPLAY_LAYOUT = [
    {"slot": 0, "widget": "clock"},
    {"slot": 1, "widget": "session"},
]
