# Presence Hub — Implementation Decisions

**Why the stack looks the way it does:** reverse-engineering workflow, gestures, Spotify, buttons, OTA, MQTT, and what failed before it worked.

Companion doc: [`EDGE_AI.md`](EDGE_AI.md) (TinyML ops).

---

## 1. How we reverse-engineered the stock device

We did not start from a schematic. We started from a **suspicious desk gadget** and treated it like a forensic specimen.

### Step-by-step (June 2025)

| Step | Tool / artifact | What we learned |
|------|-----------------|-----------------|
| **1. Identify USB** | `scripts/service/detect_device.sh` | CP2102 serial, chip ESP32-D0WD-V3 |
| **2. Preserve flash** | `scripts/service/dump_firmware.sh` → `dumps/firmware_*.bin` | Full 4 MB image + partition table; stock restore path |
| **3. Strings & domains** | `strings dump.bin` | `api.spt.nikait.co`, `/v1/track`, `Authorization:`, Wi‑Fi SSIDs in NVS |
| **4. Live serial** | `scripts/service/monitor_serial.sh` | Boot: Wi‑Fi SSID, firmware **v1.0.7**, name **Living Room SPT**, sometimes **`Radar fail`** while HTTP still ran |
| **5. HTTP probes** | `curl` against OpenAPI | **No auth** on track/tracker endpoints; MAC = identity |
| **6. GPIO guesswork** | Boot pin map printed in our firmware; later **GPIO probe** | Radar UART **16/17**, I2C **21/22**, buttons **18/5** (not documented on PCB silkscreen) |

**Why dump first:** stock firmware is the only ground truth when marketing docs lie. We kept `dumps/firmware_20260605_135023.bin` so the device can always be restored to factory SPT behavior (`README.md` → “Restore stock SPT firmware”).

**Why serial mattered:** LD2410 init failures on stock FW proved the cloud dashboard could show “presence” while the radar was blind—we carried that lesson into honest logging (`hub/debug/*`, sensor log UI).

---

## 2. Architecture: why MQTT on a Mac, not a cloud API

| Stock SPT | Presence Hub |
|-----------|--------------|
| Device → `api.spt.nikait.co` | Device → **Mosquitto on Mac** (`hub/*` topics) |
| Employer dashboard (`deckp.com`) | **FastAPI + SQLite** on localhost |
| Always-on vendor infra | **Your laptop** when you want smart features |

**Why MQTT specifically:**

- Pub/sub fits **radar streams + display push + config** without inventing REST on the ESP32.
- Arduino `PubSubClient` is small; HTTPS + JSON APIs on device were already abused by stock FW.
- Topics are **easy to sniff** during debug (`mosquitto_sub`, sensor log page)—transparency by design.

**Trade-off we accept:** if the Mac sleeps, sessions/standup/Telegram pause; the ESP32 still journals to **LittleFS** and syncs later (`hub/sync/events`). That asymmetry is intentional: **no fake cloud sync**.

---

## 3. UDP discovery — why not hard-code the Mac IP?

Early builds used a fixed LAN IP. That broke every time DHCP reassigned the Mac.

**Flow today:**

1. ESP32 broadcasts `PHUB_DISCOVER` on UDP `:18832`.
2. Mac daemon replies JSON: `{mqtt_host, mqtt_port, ota_host, ota_port}`.
3. ESP32 caches in NVS (`Preferences` ns `phub`).
4. On **every MQTT reconnect**, live UDP discovery runs **before** cache.
5. After a failed connect, cache is cleared and discovery runs again.
6. Mac also **broadcasts hub JSON every 45s** so devices can pick up a new IP passively.

**Autonomous / offline:** when MQTT drops, `hubOnline_` clears immediately, the main loop keeps radar/buttons/display on a local clock, and events buffer to LittleFS. Full UDP discovery is **rate-limited** (45s) so the network task never blocks in a discover loop while the Mac is away; passive beacon listen runs every 3s.

**Code:** `firmware/src/discovery.cpp`, `daemon/discovery.py`.

**Past bug (fixed):** `ensureMqtt()` reused NVS cache without rediscovering; broadcast used only `255.255.255.255`. **Regression (fixed in 0.7.5):** `networkTick()` called boot discovery every 25ms when the endpoint was invalid, blocking the network task.

---

## 4. OTA — why bother if USB flash works?

USB flash (`./scripts/flash_firmware.sh`) is fine for **bring-up**. OTA exists because the device lives **on the desk**, USB cable does not.

| Reason | Detail |
|--------|--------|
| **Iteration speed** | `pio run` on Mac → daemon serves `firmware/.pio/build/esp32dev/firmware.bin` on `:18081` |
| **Same trust model** | OTA URL comes from **discovery** (LAN IP), not a vendor S3 bucket |
| **Triggers** | Web dashboard button, Telegram `/update`, MQTT `hub/ota/trigger` |
| **Implementation** | ESP32 `HTTPUpdate` (`firmware/src/ota.cpp`); aiohttp file server (`daemon/ota_server.py`) |

Stock SPT pulled OTA from a **public S3 bucket** tied to their release cadence—we mirrored the *convenience*, not the *vendor lock-in*.

**Security note:** HTTP on LAN, no TLS—acceptable on isolated Wi‑Fi; do not port-forward `:18081`.

---

## 5. Buttons — assumed pins, GPIO probe, and debouncing

The carrier board had **no public pinout**. We guessed, then **proved**.

### Discovery workflow

1. **`gpio_probe` mode** — firmware scans candidate GPIOs, publishes `hub/debug/gpio` every change + 5 s heartbeat (`firmware/src/gpio_probe.cpp`).
2. **`scripts/button_gpio_probe.sh`** — `mosquitto_sub` on `hub/debug/gpio`; press button → see which pin toggles.
3. **`button_listen.sh`** — listens for `hub/debug/press` during **button learn** mode.
4. **Confirmed (2026-06-15):** Btn1 = **GPIO 18**, Btn2 = **GPIO 5**, **active LOW** (documented in `firmware/src/pins.h`).

### Runtime behavior (`firmware/src/buttons.cpp`)

- **30 ms debounce** on stable level before edge counts.
- **Short press** on release if no long fired.
- **Long press** at **800 ms** (session reset on btn1 in work mode).
- Pins configurable via MQTT `hub/config` → NVS (`button_config`).

**Why active LOW + internal pull-up:** matches typical “button to GND” wiring on cheap ESP32 boards; `activeLow` flag allows inversion without reflash.

---

## 6. Gestures — physics, failed swipes, and what actually shipped

### What the LD2410 gives you

The HLK-LD2410C is a **single-beam ranger**:

- One distance axis along the radar’s line of sight.
- Per-gate **moving/stationary energy** (enhanced mode).
- **No left/right**, no true 3D hand pose—despite product photos implying Minority Report.

At desk distance the useful near field is often **~12–35 cm**, heavily quantized (~20 cm gate steps in gesture profile).

### What `PLAN_EN.md` wanted vs reality

| Planned gesture | Idea | Outcome |
|-----------------|------|---------|
| Swipe **in** → next | Fast approach | **Hard** — “velocity” collapses to noisy Δdistance |
| Swipe **out** → prev | Recede | **Harder** — same signal, opposite sign, easy to confuse with noise |
| **Hover** → volume | Stable distance maps to level | **Medium** — needs low variance window; fan multipath breaks it |
| **Zone hold** → next | Hand in 12–28 cm for 400 ms | **Reliable** — one bit of geometry + time |

After weeks of tuning, **v1 shipped only zone-hold → `gesture: next`**. README still documents that as the baseline fallback.

### Media mode radar tuning (`firmware/src/radar.cpp`)

When `setGestureProfile(true)`:

- **20 cm gates**, max gate **2** (~60 cm cap)—ignore room-scale clutter.
- **8× poll per loop** (`MEDIA_RADAR_POLLS`) + **200 ms MQTT** publish—higher temporal resolution for hold detection.
- `pickGestureDistance()` prefers close moving/stationary targets over far body reflection.

### Zone-hold state machine (`media_mode.cpp`)

```
enter near zone → start hold timer
hold ≥ holdMs (400) → fire next (if debounce ok)
leave zone → re-arm (zoneArmed = true)
debounce 1200 ms between fires
```

**Why re-arm on exit:** without it, one long dwell triggered multiple skips.

**Why `present` flag is ignored for zone:** LD2410 often reports `present=false` while `dist` is valid in the near field—we key off **distance in range**, not the boolean.

### Debug pipeline

- Web **[Gestures page](http://127.0.0.1:18080/static/gestures.html)** — live dist bar, zone overlay, hold countdown.
- MQTT **`hub/debug/gesture`** when `gesture_debug=1` — `in_zone`, `hold_ms`, `hold_left_ms`, `zone_armed`.
- **[Sensor log](http://127.0.0.1:18080/static/sensor-log.html)** — correlate with `hub/gesture` events.

Calibration flows through **Settings → Gestures → Save** → retained `hub/config` → ESP32 NVS.

### feat/tinyml — next / prev / hover (experimental)

Firmware **0.6.0** adds velocity-based classes:

- Feature **f[5] = Δdistance/Δt** (cm/s) over ~32-frame buffer.
- Heuristic: `velocity > +25` → next, `< -25` → prev, `|velocity| < 8` + low dist variance → hover/vol.
- Tiny MLP head + **fallback to zone-hold** if confidence &lt; threshold.

**Why prev still feels broken in practice:**

1. **Symmetric noise** — retreat motion is weaker than approach (hand leaves beam faster).
2. **Multipath** — monitor stand / desk edge creates fake “approach” more often than “retreat”.
3. **Default weights are heuristic**, not trained on your desk—`tools/ml/train_gesture.py` + recording mode needed.
4. **Debounce is global per gesture type** — rapid next→prev can lose.

**Honest UX:** treat **next (zone-hold)** as production; **prev/vol** as “enable after calibration session”.

---

## 7. Spotify and media integration

**Why Mac-only AppleScript path:**

- Personal hub runs on **your Mac** where Spotify desktop already lives.
- No Spotify Web API OAuth circus on a side project.
- `osascript` → `tell application "Spotify" to play next track` works when Spotify is frontmost or background.

**Implementation (`daemon/modes/media.py`):**

| Backend | Mechanism | Track name on OLED |
|---------|-----------|-------------------|
| **spotify** (default) | AppleScript for next/prev/volume + metadata query | Artist — title, position/duration |
| **system_keys** | `key code 124/123` via System Events | Optional `nowplaying-cli` fallback |

**Flow:**

1. ESP32 publishes `hub/gesture` `{type, value}`.
2. Daemon `MediaController.on_gesture()` runs AppleScript (or media key).
3. `display_engine` pushes track widget to `hub/display` ~1 Hz.

**Why not control Spotify from ESP32 directly:** TLS + OAuth + token refresh on 320 KB RAM is absurd; **gesture stays on chip, policy stays on Mac**.

**Failure modes:**

- Spotify not installed → AppleScript fails → fallback media key (if configured).
- No `nowplaying-cli` → OLED shows mode badge only, no track text.

---

## 8. Work / sleep modes — other non-obvious choices

### Work presence

- Distance window **20–400 cm** + optional moving/stationary filter (`radar_config`).
- **Standup reminders on daemon**, not firmware—so you can change interval/message without reflash.
- **Reset standup after absence:** absent ≥4 of last 5 minutes (`work.py`)—stops timer flicker when radar drops briefly.

### Sleep

- **Manual bed/wake only** (btn1/btn2)—auto-sleep from stillness disabled after false positives.
- Phase chart = **5-min buckets of s_energy vs baseline**, not EEG—labeled “estimate” everywhere.

### Display

- **U8g2**, not LVGL—128×64 SSD1306 does not need a scene graph; flash/RAM budget goes to radar + ML.
- **Web layout editor** pushes widgets via MQTT; firmware only renders strings (keeps OLED logic dumb and reliable).

### Offline sync

- Event types: `presence`, `mode`, `button`, `gesture`, `sleep_*`.
- NTP after Wi‑Fi before trusting timestamps.
- Daemon dedup by `eid` on replay.

---

## 9. TinyML placement (why on ESP32, why this shape)

See [`EDGE_AI.md`](EDGE_AI.md) for training commands. Design rationale:

| Decision | Why |
|----------|-----|
| **Inference on ESP32** | Sub-10 ms; Mac stays idle; privacy |
| **Train on Mac offline** | Python + sklearn; no on-chip training fantasy |
| **16 hand-crafted features** | Interpretable; matches what we debug in sensor log |
| **Inline INT8 MLP** | Fits flash (~75% used); no TFLite Micro dependency hell |
| **Three mode-specific heads** | Work/sleep/media need different windows and class sets |
| **Heuristic fallback** | Device usable day-one before you record training CSV |
| **Recording mode → `hub/radar/raw`** | Gate vectors only when explicitly enabled—MQTT budget |

Danila’s Medium “AI on chip” story and our stack differ: we document **weights, limits, and fallback**; marketing hides the zone-hold.

---

## 10. Evolution timeline (firmware versions)

| Version | Milestone |
|---------|-----------|
| **0.1–0.2** | MQTT hub, modes, SQLite, basic OLED |
| **0.3+** | UDP discovery, LittleFS offline sync |
| **0.4** | Near-zone gesture (next only), gesture debug UI |
| **0.5.1** | MQTT payload size fix (radar silently dropped) |
| **0.5.2** | Single-line OLED font scaling |
| **0.6.0** | TinyML, prev/vol/hover (experimental), AI dashboard |

---

## 11. What we would do differently (see also ADVANCED_AI_ROADMAP)

- **1D-CNN on gate tensor** instead of hand features—if TFLite Micro worth the flash tax.
- **Gesture calibration wizard** — record 50 swipes, one-click retrain (planned, not shipped).
- **Linux hub** — replace AppleScript with MPRIS for Spotify.
- **TLS on MQTT** — if guest Wi‑Fi is a concern.

Full “tier 2–5” platform ideas: [`ADVANCED_AI_ROADMAP.md`](ADVANCED_AI_ROADMAP.md).

---

## 12. Quick debug cheat sheet

| Problem | Check |
|---------|-------|
| No MQTT | Discovery? `hub/status` heartbeat? Mac mosquitto `:18830`? |
| Wrong buttons | Enable GPIO probe; run `button_gpio_probe.sh` |
| Gesture fires twice | Increase `gesture_debounce_ms`; leave zone to re-arm |
| Gesture never fires | Gestures page: dist inside zone? Media mode? Debug on? |
| prev/vol dead | TinyML confidence; record training data; fallback is next-only |
| Spotify no-op | Spotify running? Backend in Settings? Try `system_keys` |
| OTA fail | `pio run` built bin? Daemon up? Same LAN? Check serial `OTA failed:` |
| Sleep chart empty | `s_energy` in `hub/radar` (fixed in 0.6.0); daemon running during night? |

---

*Last updated for firmware 0.6.0 / branch `feat/tinyml`.*
