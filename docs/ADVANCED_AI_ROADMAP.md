# Advanced Edge AI Roadmap — What We Considered Beyond v1 TinyML

This document captures the **more ambitious** architecture discussed during planning (including third-party “ESP-IDF + LVGL + TensorFlow” templates that did not match this repo). It is a **forward-looking design note**, not shipped code.

Use it to explain: *what we built now* vs *what a “pro” edge-AI presence platform could look like*.

---

## Current baseline (shipped in `feat/tinyml`)

| Layer | Choice |
|-------|--------|
| Firmware | PlatformIO + Arduino, cooperative `loop()` |
| Display | U8g2 on SSD1306 |
| Inference | Inline INT8 MLP (~16 features → 3–4 classes per mode) |
| Training | Offline Python (`tools/ml/`), export to `model_data.h` |
| Host | Mac daemon logs state IDs only; no real-time ML |

**Good for:** personal use, low Mac CPU, honest scope, fast iteration.

**Ceiling:** single-user models, no rich UI on device, no multi-room fusion, no automated retraining pipeline.

---

## Tier 2 — Stronger on-device ML (still ESP32)

### Migrate inference runtime

| Option | Flash | RAM | Notes |
|--------|-------|-----|-------|
| **TFLite Micro** | +60–100 KB | 30–50 KB arena | Standard tooling; `tflite-micro` in PlatformIO |
| **emlearn** | Small | Tiny | Random Forest → pure C; great for interpretable gates |
| **ESP-NN** | Optimized | Medium | **Requires ESP-IDF** — full firmware rewrite |

**Recommendation if staying on PlatformIO:** TFLite Micro for 1D-CNN on gate sequences (true time-series model, not hand-crafted features).

### Model architecture upgrades

```
Raw gate tensor [T × 18]  (9 moving + 9 stationary energies over T frames)
        ↓
1D-CNN or tiny Transformer encoder
        ↓
Multi-head output: presence | gesture | sleep micro-state
```

Benefits:

- Learns temporal patterns rules miss (e.g. fan = periodic high-frequency energy)
- Single shared backbone, mode-specific heads → less flash than three separate MLPs

### FreeRTOS inference task

Move `tiny_ml` to **Core 0** pinned task:

- Radar poll + MQTT on loop/core 1
- Inference queue: `FeatureFrame[]` → `AiState`
- Prevents OLED/UI stalls if models grow

### On-device model slots (A/B testing)

- NVS or LittleFS partition for **two model blobs**
- MQTT `hub/model/switch` → hot-swap without full OTA
- Version string in `hub/status`

---

## Tier 3 — ESP-IDF platform (the “Gemini template” path)

What generic advice often assumes—and what **this repo is not**:

| Assumed | Reality here |
|---------|--------------|
| ESP-IDF components + Kconfig | PlatformIO `src/` modules |
| LVGL on OLED | U8g2 (128×64, no touch) |
| FreeRTOS queues everywhere | Single `loop()` |
| ESP-NN / esp-dl | Not integrated |

**If rebuilding on ESP-IDF:**

```
components/
  ld2410/     UART task → queue
  tiny_ml/    inference task ← queue
  gui/        LVGL task ← ai_state queue
  mqtt/       sync task
```

**When it’s worth it:**

- ESP32-S3 + more flash for larger models
- Richer OLED or color display
- Certified product / long maintenance horizon

**Cost:** weeks of migration; re-test discovery, OTA, LittleFS sync, all modes.

---

## Tier 4 — Host-side “ML lab” (Mac hub as trainer, not inferencer)

Keep real-time inference on ESP32; use the daemon for **continuous improvement**:

### Automated dataset pipeline

1. `hub/radar/raw` → append-only Parquet on Mac (when recording)
2. Weak labels from buttons, gestures, mode changes, calendar (“focus block”)
3. Nightly cron: retrain if N new samples &gt; threshold
4. Publish new `model_data.h` artifact → OTA prompt in web UI

### Rich analytics (batch only)

| Feature | Method |
|---------|--------|
| Breath rate trend | FFT / autocorrelation on sleep nights |
| Focus depth | Hidden Markov on `ai_states` timeline |
| Break detection | Change-point on `static_fatigue` segments |
| Weekly report | Telegram + PDF from SQLite |

Mac CPU: seconds per day, not continuous load.

### Federated-style multi-desk (optional, advanced)

- Several ESP32 nodes → one hub
- Per-room models + global “noise profile” merge
- Still **no cloud** — LAN only

---

## Tier 5 — Product-grade “kind assistant” platform

Positioning opposite corporate SPT clouds:

| Capability | Implementation sketch |
|------------|------------------------|
| **Privacy manifest** | Signed firmware; MQTT payload schema versioned; no PII fields |
| **Home Assistant** | MQTT discovery for `ai_state`, occupancy |
| **Open model zoo** | Community-trained gesture packs (CSV → `.h` verified hashes) |
| **Calibration wizard** | Web UI: 5-minute guided recording per mode → one-click train |
| **Explainability** | Log top-3 feature contributions (emlearn or SHAP offline) |
| **Clinical boundary** | Hard-coded disclaimers; no “sleep score” without EEG |

### Hardware upgrades

| Upgrade | Enables |
|---------|---------|
| ESP32-S3 | Vector instructions, more RAM for TFLite |
| Second sensor (PIR or second radar angle) | Reduce fan false positives |
| RGB LED ring | Gesture feedback without reading OLED |

---

## Comparison matrix (for articles / investors — use honestly)

| | Stock SPT cloud gadget | Presence Hub v1 | Presence Hub TinyML | Tier 3–5 roadmap |
|--|------------------------|-----------------|----------------------|------------------|
| Data leaves home | Yes | No | No | No |
| “AI” claim | Opaque cloud | Heuristics | On-device MLP | CNN + auto-retrain |
| Gesture reality | Marketing | Zone-hold | Velocity classes | Trained 1D-CNN |
| Mac required | No | Yes (hub) | Yes (light) | Yes (optional trainer) |
| Engineering effort | Vendor | ~weeks RE + build | +days ML pipeline | +months platform |

---

## Recommended sequencing (if continuing)

1. **Collect real data** — recording mode, 2–3 evenings of gestures + work + sleep  
2. **Retrain MLP** — replace heuristic `model_data.h`  
3. **TFLite Micro 1D-CNN** — if MLP plateaus on fan/noise  
4. **Calibration wizard** in web UI — biggest UX win per engineering hour  
5. **ESP-IDF** — only if display/UI or ESP-NN becomes blocking  

---

## What we explicitly rejected

- **LLM on device or “ask ChatGPT to label radar”** — nonsense for LD2410 timings  
- **Cloud training on your presence** — defeats liberation goal  
- **Camera-based tracking** — privacy regression  
- **Employer dashboard replica** — wrong product ethics  

---

## See also

- Operator manual: [`EDGE_AI.md`](EDGE_AI.md)
- Implementation decisions: [`IMPLEMENTATION.md`](IMPLEMENTATION.md)
