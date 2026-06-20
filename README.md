# Personal Presence Hub

ESP32 + HLK-LD2410C — personal presence tracker: work sessions, sleep monitoring, radar gestures. A Python daemon on your Mac handles logging and UI; **on-device TinyML** runs inference on the ESP32. **No SPT cloud.**

Detailed spec: [PLAN_EN.md](PLAN_EN.md) · Edge AI: [docs/EDGE_AI.md](docs/EDGE_AI.md) · **Implementation decisions:** [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)

## Edge AI (TinyML)

Pre-trained models in `firmware/src/model_data.h` classify radar feature windows on the ESP32:

- **Work** — presence, static fatigue (stretch hint), environmental noise filter
- **Media** — swipe next/prev, hover volume (fallback: zone-hold next)
- **Sleep** — breathing vs restless; Mac estimates breath rate from nightly samples

Configure in web **Settings → Edge AI**. Train on your data: `tools/ml/` (see [docs/EDGE_AI.md](docs/EDGE_AI.md)).

## Network

ESP32 and Mac must be on the **same Wi‑Fi**.


| Node        | Config                                                                |
| ----------- | --------------------------------------------------------------------- |
| ESP32 WiFi  | `firmware/secrets.ini` → `WIFI_SSID`, `WIFI_PASS`                     |
| ESP32 → hub | **UDP discovery** — ESP32 broadcasts, Mac replies with its current IP |
| Mosquitto   | `0.0.0.0:18830` (`[daemon/mosquitto.conf](daemon/mosquitto.conf)`)    |
| Discovery   | UDP `:18832` — response includes `mqtt_host` / `ota_host`             |


**You do not need to hard-code the Mac IP.** The router may reassign it; the device re-discovers the hub on the next broadcast. The last address is cached in ESP32 NVS; after three failed MQTT connects the cache is cleared and discovery runs again.

Optional: `HUB_LAN_IP` in `daemon/.env` if automatic LAN IP detection fails on your Mac.

## Offline buffer

ESP32 **always** writes events to LittleFS, even without the Mac:


| Event                                          | Mode  |
| ---------------------------------------------- | ----- |
| `presence` on/off                              | work  |
| `mode`                                         | all   |
| `button`                                       | all   |
| `gesture` next / prev / vol                      | media |
| `sleep_start` / `sleep_end` / `sleep_movement` | sleep |


After Wi‑Fi: NTP → real timestamps. When the Mac is online: UDP discovery → MQTT → batched `hub/sync/events` → daemon restores sessions and sleep in SQLite → `hub/sync/ack` → device buffer cleared.

Mode is stored in NVS (survives ESP32 reboot).

## Hardware


| Component     | Interface | GPIO                            |
| ------------- | --------- | ------------------------------- |
| ESP32-D0WD-V3 | —         | —                               |
| HLK-LD2410C   | UART      | RX=16, TX=17 (likely)           |
| OLED SSD1306  | I2C       | SDA=21, SCL=22, 0x3C (likely)   |
| 2 buttons     | GPIO      | 18 (btn1), 5 (btn2), active LOW |


Pins: `[firmware/src/pins.h](firmware/src/pins.h)`. Boot serial prints the pin map.

## Requirements

```bash
brew install mosquitto platformio
python3 -m venv venv && source venv/bin/activate
pip install -r daemon/requirements.txt
```

## First flash checklist

1. `cp firmware/secrets.ini.example firmware/secrets.ini` — WiFi only
2. `cp daemon/.env.example daemon/.env` — Telegram tokens (optional)
3. `./daemon/run.sh` — start daemon (mosquitto + web + MQTT)
4. `cd firmware && pio run` — build
5. `./scripts/flash_firmware.sh` — flash over USB
6. `pio device monitor` — expect: `Radar ready`, `WiFi OK`, `MQTT connected`
7. [http://127.0.0.1:18080](http://127.0.0.1:18080) — `Online: yes`

## Quick start

### Daemon

```bash
./daemon/run.sh
```


| Service                           | URL / port                                                                                     |
| --------------------------------- | ---------------------------------------------------------------------------------------------- |
| Web UI                            | [http://127.0.0.1:18080](http://127.0.0.1:18080)                                               |
| Display layout                    | [http://127.0.0.1:18080/static/display.html](http://127.0.0.1:18080/static/display.html)       |
| Settings (radar, Telegram, media) | [http://127.0.0.1:18080/static/settings.html](http://127.0.0.1:18080/static/settings.html)     |
| Gestures (near-zone calibration)  | [http://127.0.0.1:18080/static/gestures.html](http://127.0.0.1:18080/static/gestures.html)     |
| Sensor log (live radar/events)    | [http://127.0.0.1:18080/static/sensor-log.html](http://127.0.0.1:18080/static/sensor-log.html) |
| OTA binary                        | http://LAN-IP:18081/firmware.bin                                                               |
| MQTT (ESP32)                      | LAN-IP:18830                                                                                   |


Login autostart on Mac:

```bash
./scripts/install_launchd.sh
```

### Firmware

```bash
./scripts/flash_firmware.sh          # USB
cd firmware && pio device monitor      # serial logs
```

## Modes


| Mode      | Button 2   | Button 1                                       |
| --------- | ---------- | ---------------------------------------------- |
| **work**  | cycle mode | short: pause; long: reset session              |
| **sleep** | → media    | btn1: went to bed; btn2: woke up + exit sleep  |
| **media** | cycle mode | short: pause; long: reset; + near-zone gesture |


Media is work + gestures: **work log and standup continue**; the session closes only when entering **sleep**. Auto-sleep from stillness is disabled — sleep starts only after btn1 “went to bed”.

## Display (web config)

[http://127.0.0.1:18080/static/display.html](http://127.0.0.1:18080/static/display.html) — up to **3 lines**, widget + font size (large/medium/small), brightness. Save → MQTT → OLED without reflashing.

Widgets: `clock`, `session`, `today`, `track`, `standup_timer`, `reminder`, `mode`, `status`, `sleep`.

## Standup reminders

In **work** or **media**, if you stay present with an active session longer than the configured interval (default **120 min**), the daemon shows a reminder on the display and sends Telegram (if configured). Interval, enable/disable, and message text: **Settings** web page. Not sent in **sleep** mode. The timer resets if you were away from the desk for at least **4 of the last 5 minutes** (reduces false triggers from brief radar flicker). Manual reset: long press btn1.

## Morning sleep summary

Once per day, after **sleep** ends and local time is past the configured hour (default **8:00**), Telegram receives: `Good morning. Sleep: Xh, movements: N`. Enable/disable and hour: **Settings**.

## Sleep placement

LD2410C for sleep watches **0–1.2 m** (configurable in Settings). Nightstand **0.5–1.2 m** from chest; avoid doors and windows. Screen at night: off or minimal (Display page). Data is buffered on ESP32 when offline.

The dashboard sleep chart shows **estimated** calm / restless / awake phases from radar motion energy — not clinical deep/light sleep (no EEG).

## Media — near-zone gesture

The LD2410 is **one-dimensional**: it sees distance along the beam, not true X/Y. In **media** mode, hold your hand in the configured near zone (default **12–28 cm**) for ~400 ms → **next track** (Spotify or system media keys). Leave the zone to re-arm.

Calibration and live radar: [http://127.0.0.1:18080/static/gestures.html](http://127.0.0.1:18080/static/gestures.html) — enable **Debug** for MQTT `hub/debug/gesture`.

Media backend (Spotify vs system keys): **Settings**.

## Telegram

**Credentials** (not editable in the web UI): `daemon/.env`

- `HUB_TELEGRAM_TOKEN`
- `HUB_TELEGRAM_CHAT_ID`

**Web Settings:** standup interval, standup message, morning summary enable/hour.

Bot commands:

```
/status  
/today  
/week  
/sleep
/sleep week
/standup 120
/mode work|sleep|media
/update
/settings
```

Defaults in `.env.example`: `HUB_STANDUP_MIN=120`, `HUB_MORNING_HOUR=8`.

## OTA (over the air)

1. `cd firmware && pio run`
2. Daemon running (OTA URL uses current LAN IP)
3. Trigger: web dashboard button, Telegram `/update`, or MQTT `hub/ota/trigger`
4. ESP32 downloads `http://<LAN-IP>:18081/firmware.bin` and reboots

## Service scripts

Development / diagnostics under `scripts/service/` (English only):


| Script               | Purpose                                                |
| -------------------- | ------------------------------------------------------ |
| `detect_device.sh`   | Find USB serial port, print chip/flash info            |
| `dump_firmware.sh`   | Read full flash → `dumps/`                             |
| `monitor_serial.sh`  | Colorized serial monitor → `logs/`                     |
| `capture_traffic.sh` | tshark / mitmproxy helpers → `captures/`, `mitm_logs/` |
| `mitm_addon.py`      | mitmdump addon for request/response logging            |


```bash
./scripts/service/detect_device.sh
./scripts/service/monitor_serial.sh
./scripts/service/capture_traffic.sh help
```

Requires repo-root `venv/` with `esptool`, `pyserial`, and optionally `mitmproxy` / system `tshark`.

## Restore stock SPT firmware

```bash
./venv/bin/esptool --port /dev/cu.usbserial-0001 write_flash 0x0 dumps/firmware_20260605_135023.bin
```

## Layout

```
esp32_spt/
├── firmware/              # PlatformIO
├── daemon/                # Python asyncio hub
├── scripts/
│   ├── flash_firmware.sh
│   ├── install_launchd.sh
│   └── service/           # detect, dump, serial, traffic tools
├── dumps/                 # firmware backups (gitignored)
├── PLAN_EN.md
└── README.md
```

## MQTT topics


| Topic                              | Direction                      |
| ---------------------------------- | ------------------------------ |
| `hub/radar`                        | ESP32 → daemon                 |
| `hub/mode`                         | bidirectional                  |
| `hub/button`                       | ESP32 → daemon                 |
| `hub/display`                      | daemon → ESP32                 |
| `hub/config`                       | daemon → ESP32                 |
| `hub/gesture`                      | ESP32 → daemon                 |
| `hub/debug/gesture`                | ESP32 → daemon (when debug on) |
| `hub/ota/trigger`                  | daemon → ESP32                 |
| `hub/status`                       | ESP32 → daemon                 |
| `hub/sync/events` / `hub/sync/ack` | offline sync                   |


