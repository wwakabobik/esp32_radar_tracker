#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/daemon"

export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin:$PATH"

MOSQUITTO_BIN="$(command -v mosquitto || true)"
if [[ -z "$MOSQUITTO_BIN" && -x /opt/homebrew/sbin/mosquitto ]]; then
  MOSQUITTO_BIN=/opt/homebrew/sbin/mosquitto
fi
if [[ -z "$MOSQUITTO_BIN" ]]; then
  echo "Install mosquitto: brew install mosquitto" >&2
  exit 1
fi

if ! pgrep -f "mosquitto.*18830" >/dev/null 2>&1; then
  "$MOSQUITTO_BIN" -c "$ROOT/daemon/mosquitto.conf" -d
  echo "Started mosquitto on 18830"
fi

exec "$ROOT/venv/bin/python" main.py
