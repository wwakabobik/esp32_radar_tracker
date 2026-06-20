#!/usr/bin/env python3
"""Subscribe to hub/radar/raw and append rows to CSV for offline training."""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

GATE_FIELDS = [f"moving_gates_{i}" for i in range(9)] + [f"stationary_gates_{i}" for i in range(9)]

CSV_COLUMNS = [
    "ts",
    "dist",
    "presence_dist",
    "m_dist",
    "s_dist",
    "s_energy",
    "m_energy",
    "gate_dist",
    "m_gate_centroid",
    "moving",
    "presence",
    *GATE_FIELDS,
]


def flatten_payload(data: dict) -> dict:
    row = {key: data.get(key, "") for key in CSV_COLUMNS if key not in GATE_FIELDS}
    row["moving"] = int(bool(data.get("moving")))
    row["presence"] = int(bool(data.get("presence")))
    for prefix, key in (("moving_gates", "moving_gates"), ("stationary_gates", "stationary_gates")):
        gates = data.get(key) or []
        for i in range(9):
            row[f"{prefix}_{i}"] = gates[i] if i < len(gates) else 0
    if not row.get("ts"):
        row["ts"] = time.time()
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect raw radar MQTT samples to CSV")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18830)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("datasets") / f"radar_raw_{int(time.time())}.csv",
    )
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    file_exists = args.output.exists() and args.output.stat().st_size > 0

    with args.output.open("a", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=CSV_COLUMNS)
        if not file_exists:
            writer.writeheader()

        def on_connect(client, _userdata, _flags, rc, _properties=None):
            if rc == 0:
                client.subscribe("hub/radar/raw")
                client.subscribe("hub/radar")
                print(f"Collecting → {args.output}", file=sys.stderr)
            else:
                print(f"MQTT connect failed: {rc}", file=sys.stderr)

        def on_message(_client, _userdata, msg):
            try:
                data = json.loads(msg.payload.decode("utf-8"))
            except json.JSONDecodeError:
                return
            if msg.topic == "hub/radar/raw" or data.get("moving_gates"):
                writer.writerow(flatten_payload(data))
                fh.flush()
                print(f"raw {data.get('ts', '')}", file=sys.stderr)

        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        client.on_connect = on_connect
        client.on_message = on_message
        client.connect(args.host, args.port, 60)
        client.loop_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
