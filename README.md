# Personal Presence Hub

ESP32 + HLK-LD2410C превращается в личный гаджет: трекинг присутствия, сон, управление музыкой жестами. Мозг — Python-демон на Mac, связь через MQTT.

Подробное ТЗ: [PLAN_RU.md](PLAN_RU.md) / [PLAN_EN.md](PLAN_EN.md)

## Железо

| Компонент | Интерфейс | GPIO (предположительно) |
|---|---|---|
| ESP32-D0WD-V3 | — | — |
| HLK-LD2410C | UART | RX=16, TX=17 (likely) |
| OLED SSD1306 | I2C | SDA=21, SCL=22, addr 0x3C (likely) |
| 2 кнопки | GPIO | 32, 33 (confirmed) |

Пины задаются в `firmware/src/pins.h`. Если дисплей или радар не заводятся — поправь там или в `platformio.ini`.

## Требования

- macOS
- Python 3.12+
- [PlatformIO](https://platformio.org/) — для прошивки
- Mosquitto: `brew install mosquitto`

## Быстрый старт

### 1. Демон на Mac

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r daemon/requirements.txt

cp daemon/.env.example daemon/.env
# опционально: HUB_TELEGRAM_TOKEN, HUB_TELEGRAM_CHAT_ID

./daemon/run.sh
```

| Сервис | URL / порт |
|---|---|
| Web UI | http://127.0.0.1:18080 |
| Настройка дисплея | http://127.0.0.1:18080/static/display.html |
| OTA | http://127.0.0.1:18081/firmware.bin |
| MQTT | `127.0.0.1:18830` |

### 2. Прошивка ESP32

```bash
cd firmware
# WiFi и IP мака — в platformio.ini или secrets.ini (см. secrets.ini.example)
pio run -t upload
pio device monitor
```

`MQTT_HOST` в `platformio.ini` — IP твоего Mac в локальной сети (не `127.0.0.1`).

## Режимы

| Режим | Кнопка 2 | Что делает |
|---|---|---|
| **work** | по умолчанию | Трекинг за столом, напоминания «встань» |
| **sleep** | переключить | Анализ дыхания у кровати |
| **media** | переключить | Жесты → Music.app |

Кнопка 1 в work: пауза/возобновление сессии.

## Telegram-бот (aiogram)

Команды (нужен `HUB_TELEGRAM_TOKEN` в `.env`):

```
/status   /today   /sleep
/standup 45        — интервал напоминаний (мин)
/mode work|sleep|media
/update             — OTA прошивки
/settings
```

## Display Layout Engine

Через веб-интерфейс настраиваешь, что показывать на каждой строке OLED:

`clock`, `date`, `session`, `today`, `track`, `reminder`, `standup_timer`, `mode`, `status`

Настройки хранятся в SQLite (`daemon/data/hub.db`), демон шлёт готовый текст на ESP32 через MQTT `hub/display`.

## OTA

1. Собери прошивку: `cd firmware && pio run`
2. Бинарник: `firmware/.pio/build/esp32dev/firmware.bin`
3. Запусти демон и отправь `/update` в Telegram или опубликуй в `hub/ota/trigger`

## Структура проекта

```
esp32_spt/
├── firmware/          # PlatformIO, C++
├── daemon/            # Python asyncio демон
│   ├── main.py
│   ├── hub.py
│   ├── display_engine.py
│   ├── telegram_bot.py
│   ├── ota_server.py
│   ├── web/           # FastAPI + static
│   └── run.sh
├── PLAN_RU.md
└── PLAN_EN.md
```

## MQTT-топики

| Топик | Направление |
|---|---|
| `hub/radar` | ESP32 → демон |
| `hub/mode` | двусторонний |
| `hub/button` | ESP32 → демон |
| `hub/display` | демон → ESP32 |
| `hub/gesture` | ESP32 → демон |
| `hub/ota/trigger` | демон → ESP32 |
| `hub/status` | ESP32 → демон (heartbeat) |
