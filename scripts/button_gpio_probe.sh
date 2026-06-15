#!/usr/bin/env bash
# Watch GPIO probe MQTT while pressing device buttons.
# Usage: ./scripts/button_gpio_probe.sh

set -euo pipefail

PORT="${MQTT_PORT:-18830}"
HOST="${MQTT_HOST:-127.0.0.1}"

echo "Listening hub/debug/gpio on ${HOST}:${PORT}"
echo "Press buttons on the device — pins in 'changed' are candidates."
echo ""

mosquitto_sub -h "$HOST" -p "$PORT" -t 'hub/debug/gpio' -v | while read -r topic payload; do
  echo "$payload" | python3 -c '
import json, sys
d = json.load(sys.stdin)
changed = d.get("changed", [])
hb = d.get("heartbeat", False)
if changed:
    pins = d.get("pins", {})
    parts = [f"GPIO {p}={pins.get(str(p), pins.get(p, "?"))}" for p in changed]
    print(">>> CHANGED:", ", ".join(parts))
elif hb:
    b1, b2 = d.get("btn1"), d.get("btn2")
    print(f"... heartbeat (configured btn pins: {b1}/{b2})")
'
