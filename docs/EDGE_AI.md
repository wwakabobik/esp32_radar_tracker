# Edge AI (TinyML) — Technical Specification

On-device **TinyML** for the Personal Presence Hub: pre-trained INT8-style models run **inference only** on the ESP32. No training on chip, no cloud, no LLM.

## What the AI actually does

| Mode | Classes | Use |
|------|---------|-----|
| **Work** | vacant, active_focus, static_fatigue, env_noise | Desk presence, stretch hints, filter fan/curtain false positives |
| **Media** | none, next, prev, hover | Swipe gestures + volume by hover distance |
| **Sleep** | absent, breathing_stable, restless | Movement detection; Mac computes breath rate from nightly samples |

## Architecture

```
LD2410C → RadarReading (+ gate arrays)
       → FeatureBuffer (32 frames)
       → Feature extraction (16 floats)
       → MLP inference (model_data.h)
       → Mode handlers + MQTT hub/ai/state
```

**Mac daemon** logs high-level states to SQLite. Optional **recording mode** streams `hub/radar/raw` for offline training. The Mac does **not** run real-time inference.

## MQTT topics

| Topic | Payload |
|-------|---------|
| `hub/radar` | Extended: `s_energy`, `m_energy`, `gate_dist`, `ai_state`, `ai_confidence` |
| `hub/ai/state` | `{state, confidence, mode, ts}` on state change |
| `hub/radar/raw` | Full gate arrays (recording mode only) |

## Configuration (Settings → Edge AI)

- **AI enabled** — toggle on-device inference
- **Recording mode** — stream raw gates for dataset collection
- **Confidence min** — below this, fallback heuristics apply
- **Fatigue minutes** — duration in static_fatigue before stretch hint
- **Fallback heuristics** — zone-hold (media) / threshold rules when ML uncertain

## Training pipeline (offline)

```bash
cd tools/ml
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

# 1. Enable recording mode in web Settings
python collect.py --output ../../datasets/raw.csv

# 2. Label windows (or use label.py with events JSON)
python label.py ../../datasets/raw.csv --events-json events.json -o ../../datasets/labeled.csv

# 3. Train per mode
python train_presence.py ../../datasets/labeled.csv --output models/presence_mlp.joblib
python train_gesture.py ../../datasets/labeled.csv --output models/gesture_mlp.joblib
python train_sleep.py ../../datasets/labeled.csv --output models/sleep_mlp.joblib

# 4. Export to firmware
python export_model.py \
  --work-model models/presence_mlp.joblib \
  --gesture-model models/gesture_mlp.joblib \
  --sleep-model models/sleep_mlp.joblib

# 5. Rebuild + OTA flash
cd ../../firmware && pio run
```

Default `firmware/src/model_data.h` ships with heuristic weights until you retrain on your environment.

## Resource budget (ESP32-D0WD)

| Resource | ~Usage |
|----------|--------|
| Flash (models) | 15–40 KB |
| RAM (buffer + inference) | 8–20 KB |
| CPU | &lt;5 ms / inference @ 500 ms period |

## Honest limits

- Not clinical sleep staging or medical advice
- Gestures are 1D distance + energy — no left/right discrimination
- Models improve with **your** recorded data (desk layout, fan noise, hand habits)
