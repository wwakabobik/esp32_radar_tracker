#!/usr/bin/env python3
"""Train gesture classifier from labeled CSV."""

from __future__ import annotations

import argparse
from pathlib import Path

import joblib
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import LabelEncoder

from features import FEATURE_COLUMNS

GESTURE_LABELS = ["none", "next", "prev", "hover"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("models/gesture_mlp.joblib"))
    args = parser.parse_args()

    df = pd.read_csv(args.csv)
    if "label" not in df.columns:
        raise SystemExit("CSV must contain a 'label' column (none/next/prev/hover)")

    X = df[FEATURE_COLUMNS].fillna(0).values
    y = LabelEncoder().fit_transform(df["label"])
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

    clf = MLPClassifier(hidden_layer_sizes=(24,), max_iter=500, random_state=42)
    clf.fit(X_train, y_train)
    score = clf.score(X_test, y_test)
    print(f"Gesture model accuracy: {score:.2%}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(clf, args.output)
    print(f"Saved {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
