#!/usr/bin/env python3
"""Build labeled feature CSV from raw radar log + event markers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd

from features import extract_window_features, label_from_events


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("--events-json", type=Path, help="JSON list of {ts, label}")
    parser.add_argument("--output", type=Path, default=Path("datasets/labeled_features.csv"))
    parser.add_argument("--window", type=int, default=16)
    args = parser.parse_args()

    df = pd.read_csv(args.raw_csv)
    if args.events_json and args.events_json.exists():
        events = pd.DataFrame(json.loads(args.events_json.read_text()))
        df = label_from_events(df, events)

    features = extract_window_features(df, window=args.window)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    features.to_csv(args.output, index=False)
    print(f"Wrote {len(features)} rows → {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
