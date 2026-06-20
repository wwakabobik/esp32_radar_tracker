#!/usr/bin/env python3
"""Export sklearn MLP weights to firmware/src/model_data.h."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from sklearn.neural_network import MLPClassifier

FEATURE_COUNT = 16

MODELS = {
    "work": {"classes": 4, "prefix": "WORK", "base_state": "Vacant"},
    "gesture": {"classes": 4, "prefix": "GESTURE", "base_state": "GestureNone"},
    "sleep": {"classes": 3, "prefix": "SLEEP", "base_state": "SleepAbsent"},
}


def quantize_matrix(matrix: np.ndarray, scale: float = 32.0) -> list[int]:
    clipped = np.clip(np.round(matrix * scale), -127, 127)
    return [int(v) for v in clipped.flatten()]


def export_mlp(name: str, clf: MLPClassifier, prefix: str) -> str:
    weights = clf.coefs_[0]
    bias = clf.intercepts_[0]
    w_q = quantize_matrix(weights.T)
    b_q = quantize_matrix(bias.reshape(1, -1), scale=1.0)
    lines = [
        f"static const int8_t {prefix}_WEIGHTS[{clf.classes_.size * FEATURE_COUNT}] = {{",
        "    " + ", ".join(str(v) for v in w_q),
        "};",
        f"static const int8_t {prefix}_BIAS[{clf.classes_.size}] = {{",
        "    " + ", ".join(str(v) for v in b_q),
        "};",
    ]
    return "\n".join(lines)


def default_header() -> str:
    return """#pragma once

#include <stdint.h>

static constexpr uint8_t TINYML_FEATURE_COUNT = 16;

static constexpr uint8_t TINYML_WORK_CLASSES = 4;
static constexpr uint8_t TINYML_GESTURE_CLASSES = 4;
static constexpr uint8_t TINYML_SLEEP_CLASSES = 3;
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-model", type=Path)
    parser.add_argument("--gesture-model", type=Path)
    parser.add_argument("--sleep-model", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "firmware" / "src" / "model_data.h",
    )
    args = parser.parse_args()

    parts = [default_header()]
    for key, meta in MODELS.items():
        path = getattr(args, f"{key}_model")
        if path and path.exists():
            import joblib

            clf: MLPClassifier = joblib.load(path)
            parts.append(export_mlp(key, clf, meta["prefix"]))
        else:
            parts.append(f"// {meta['prefix']}: using built-in heuristic weights (no model file)")

    parts.append('static constexpr char TINYML_MODEL_VERSION[] = "0.1.0-exported";')
    args.output.write_text("\n\n".join(parts) + "\n")
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
