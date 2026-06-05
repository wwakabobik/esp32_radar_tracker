#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/daemon"

if ! command -v mosquitto >/dev/null 2>&1; then
  echo "Install mosquitto: brew install mosquitto"
  exit 1
fi

if ! pgrep -f "mosquitto.*18830" >/dev/null 2>&1; then
  mosquitto -c "$ROOT/daemon/mosquitto.conf" -d
  echo "Started mosquitto on 18830"
fi

exec "$ROOT/venv/bin/python" main.py
