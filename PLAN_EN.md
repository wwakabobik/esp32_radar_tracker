# Personal Presence Hub — Plan & Technical Specification

Repurposing the ESP32 + HLK-LD2410C from a corporate surveillance device into a personal smart gadget.

---

## Hardware

| Component | Model | Interface |
|---|---|---|
| MCU | ESP32-D0WD-V3 (240 MHz, dual-core) | — |
| Radar | HLK-LD2410C | UART |
| Display | 0.96" OLED SSD1306 (assumed) | I2C |
| Buttons | 2x GPIO to GND | GPIO |
| Power | USB (CP2102) or standalone | — |

> **Step 0 before development:** photograph the carrier board from above to identify the actual GPIO pins for display, buttons, and radar UART.

---

## Device Modes

### 🖥 Work Mode (primary, daytime)

**Device:**
- Tracks desk presence via LD2410C
- Display shows: current session duration + total for the day
- Button 1 (short press): pause / resume tracking
- Button 1 (long press): reset current session
- Button 2: cycle modes (Work → Sleep → Media → Work)

**Mac daemon:**
- Stores session history in SQLite
- Sends "stand up, take a walk" notification every N minutes of continuous sitting
- Pushes session time and reminders to the display

### 🌙 Sleep Mode (nighttime, device placed by the bed)

**Device:**
- Radar in high-sensitivity mode — detects micro-movements (breathing ~15–18 breaths/min)
- Display turns off (or minimal brightness)
- Button 1: manually mark "went to bed"
- Button 2: manually mark "woke up"

**Mac daemon:**
- Analyzes signal pattern: sleep onset detected by stabilization of breathing rhythm
- Records: time fell asleep, time woke up, number of nocturnal movements
- Sends morning sleep summary to Telegram

### 🎵 Media Mode (gesture-based music control)

**Device:**
- Hand approaches quickly → next track
- Hand moves away quickly → previous track
- Hand hovers at a distance → volume proportional to distance
- Display: mode icon + current track name (sent by daemon)

**Mac daemon:**
- Receives gesture event, executes AppleScript command to music player
- Sends current track name to display

---

## System Architecture

```
┌─────────────────────────────────────┐
│             ESP32                    │
│                                      │
│  HLK-LD2410C → raw radar frames      │
│  Buttons     → mode/event messages   │
│  Display     ← display commands      │
│  WiFi ───────────────────────────►   │
└─────────────────────────────────────┘
                   │
             MQTT (mosquitto)
             localhost:1883
                   │
┌─────────────────────────────────────┐
│           Mac Daemon (Python)        │
│                                      │
│  ┌────────────┐  ┌────────────────┐ │
│  │  Session   │  │  Sleep         │ │
│  │  Tracker   │  │  Analyzer      │ │
│  └────────────┘  └────────────────┘ │
│  ┌────────────┐  ┌────────────────┐ │
│  │  Media     │  │  SQLite DB     │ │
│  │  Control   │  │  (history)     │ │
│  └────────────┘  └────────────────┘ │
│        │               │            │
│  Telegram Bot     Web UI (FastAPI)  │
│                   localhost:8080     │
└─────────────────────────────────────┘
```

---

## MQTT Topics

| Topic | Direction | Payload |
|---|---|---|
| `hub/radar` | ESP32 → daemon | `{dist, s_energy, m_energy, presence, ts}` |
| `hub/mode` | bidirectional | `"work"` / `"sleep"` / `"media"` |
| `hub/button` | ESP32 → daemon | `{id: 1\|2, event: "press"\|"long"}` |
| `hub/display` | daemon → ESP32 | `{line1: "...", line2: "...", icon: "..."}` |
| `hub/gesture` | ESP32 → daemon | `{type: "next"\|"prev"\|"vol", value: 0–100}` |
| `hub/ota/trigger` | daemon → ESP32 | `{url: "http://mac-ip:8080/firmware.bin"}` |
| `hub/status` | ESP32 → daemon | heartbeat every 30 sec |

---

## Telegram Bot Commands

```
/status        — current mode, active session, device online status
/today         — hours at desk today, number of breaks
/week          — daily statistics (text or chart)
/sleep         — last night: fell asleep / woke up / quality
/standup [N]   — reminders every N minutes (default: 45)
/mode <mode>   — switch mode remotely: work / sleep / media
/update        — push new firmware via OTA
/settings      — current configuration
```

---

## Web Interface (localhost:8080)

- **Dashboard:** today — time at desk, sessions, breaks
- **History:** hourly presence chart for the week / month
- **Sleep:** nightly timeline, trends
- **Settings:** reminder threshold, quiet hours, display brightness

---

## OTA (firmware update without USB)

1. New binary is placed in the daemon's folder
2. `/update` command in Telegram or button in web interface
3. Daemon publishes MQTT `hub/ota/trigger` with the URL
4. ESP32 downloads binary from daemon's HTTP server and flashes itself
5. Reboot, confirmation sent to Telegram

---

## Technology Stack

| Layer | Technology |
|---|---|
| Firmware | PlatformIO + Arduino framework (C++) |
| Communication protocol | MQTT |
| MQTT broker | mosquitto (`brew install mosquitto`) |
| Daemon | Python 3.12, asyncio, aiomqtt |
| Telegram | python-telegram-bot |
| Web API | FastAPI |
| Frontend | Plain HTML + Chart.js |
| Database | SQLite |
| OTA HTTP | aiohttp |
| macOS control | AppleScript (subprocess) |

---

## Development Roadmap

### Phase 1 — Foundation (hardware + communication)
- [ ] Identify GPIO pins (display, buttons, UART)
- [ ] Firmware: WiFi + MQTT connection
- [ ] Firmware: LD2410C reading, publish to `hub/radar`
- [ ] Daemon: receive data, write to SQLite

### Phase 2 — Work Mode
- [ ] Firmware: display control
- [ ] Firmware: button handling
- [ ] Firmware: mode switching
- [ ] Daemon: session logic, time tracking
- [ ] Daemon: stand-up reminders

### Phase 3 — Telegram Bot
- [ ] Basic commands: /status, /today, /standup
- [ ] Daemon-initiated notifications

### Phase 4 — Sleep Mode
- [ ] Firmware: high-sensitivity mode for LD2410C
- [ ] Daemon: breathing pattern analysis
- [ ] Daemon: sleep onset / wake detection
- [ ] Telegram: morning summary

### Phase 5 — Media Mode
- [ ] Firmware: gesture detection
- [ ] Daemon: AppleScript player control
- [ ] Daemon: push track name to display

### Phase 6 — Web UI & OTA
- [ ] FastAPI + HTML dashboard
- [ ] OTA mechanism
- [ ] /update command in Telegram

---

## Target Repository Structure

```
esp32_spt/
├── firmware/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── radar.h / radar.cpp
│       ├── display.h / display.cpp
│       ├── buttons.h / buttons.cpp
│       ├── mqtt_client.h / mqtt_client.cpp
│       ├── modes/
│       │   ├── work_mode.cpp
│       │   ├── sleep_mode.cpp
│       │   └── media_mode.cpp
│       └── ota.cpp
├── daemon/
│   ├── main.py
│   ├── config.py
│   ├── db.py
│   ├── modes/
│   │   ├── work.py
│   │   ├── sleep.py
│   │   └── media.py
│   ├── telegram_bot.py
│   ├── web/
│   │   ├── app.py
│   │   └── static/
│   └── ota_server.py
├── PLAN_RU.md
├── PLAN_EN.md
└── README.md
```
