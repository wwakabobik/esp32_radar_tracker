"""Shared feature extraction for training scripts (mirrors firmware tiny_ml.cpp)."""

from __future__ import annotations

import numpy as np
import pandas as pd

FEATURE_COLUMNS = [f"f{i}" for i in range(16)]


def _window_df(df: pd.DataFrame, window: int = 16) -> list[pd.DataFrame]:
    if len(df) < window:
        return []
    return [df.iloc[i : i + window].copy() for i in range(len(df) - window + 1)]


def extract_features_from_window(w: pd.DataFrame) -> dict[str, float]:
    s = w["s_energy"].astype(float)
    m = w["m_energy"].astype(float)
    dist = w["dist"].astype(float)
    dt = float(w["ts"].iloc[-1] - w["ts"].iloc[0]) if "ts" in w.columns else 1.0
    velocity = (dist.iloc[-1] - dist.iloc[0]) / dt * 1000 if dt > 0 else 0.0

    moving_gate_cols = [c for c in w.columns if c.startswith("moving_gates_")]
    still_gate_cols = [c for c in w.columns if c.startswith("stationary_gates_")]

    def peak_gate(cols: list[str]) -> float:
        if not cols:
            return 0.0
        means = w[cols].mean()
        return float(means.idxmax().split("_")[-1]) if len(means) else 0.0

    def spectral_centroid(cols: list[str]) -> float:
        if not cols:
            return 0.0
        energies = w[cols].mean().values
        total = energies.sum()
        if total <= 0:
            return 0.0
        idx = np.arange(len(energies))
        return float((idx * energies).sum() / total * 25.0)

    mean_s = float(s.mean())
    zcr = float(((s >= mean_s) != (s >= mean_s).shift(1)).sum() / max(len(s) - 1, 1) * 100)

    present_ratio = float(w.get("presence", pd.Series([0])).astype(float).mean() * 100)
    moving_ratio = float(w.get("moving", pd.Series([0])).astype(float).mean() * 100)

    ms = float((m * s).sum())
    denom = float(np.sqrt(m.sum() * s.sum()))
    cross_corr = (ms / denom * 100) if denom > 0 else 0.0

    values = [
        mean_s,
        float(s.var()),
        float(m.mean()),
        float(m.var()),
        float(dist.mean()),
        velocity,
        peak_gate(moving_gate_cols),
        peak_gate(still_gate_cols),
        float(w.get("m_gate_centroid", pd.Series([0])).astype(float).mean()),
        zcr,
        cross_corr,
        present_ratio,
        moving_ratio,
        float(s.max()),
        float(dist.std()),
        spectral_centroid(still_gate_cols),
    ]
    return {f"f{i}": values[i] for i in range(16)}


def extract_window_features(df: pd.DataFrame, window: int = 16) -> pd.DataFrame:
    rows = []
    for w in _window_df(df, window):
        row = extract_features_from_window(w)
        if "label" in w.columns:
            row["label"] = w["label"].mode().iloc[0]
        rows.append(row)
    return pd.DataFrame(rows)


def label_from_events(df: pd.DataFrame, events: pd.DataFrame) -> pd.DataFrame:
    """Weak labels: merge gesture/button timestamps into nearest samples."""
    out = df.copy()
    out["label"] = "none"
    if events.empty:
        return out
    for _, ev in events.iterrows():
        ts = float(ev.get("ts", 0))
        idx = (out["ts"] - ts).abs().idxmin()
        out.at[idx, "label"] = ev.get("label", "none")
    return out
