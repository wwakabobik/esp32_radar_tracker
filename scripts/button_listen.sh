#!/usr/bin/env bash
# Listen for button presses: which GPIO toggled (device in button_learn mode).
set -euo pipefail

HOST="${MQTT_HOST:-127.0.0.1}"
PORT="${MQTT_PORT:-18830}"

echo "Enabling listen mode on ESP32..."
mosquitto_pub -h "$HOST" -p "$PORT" -t hub/config -m '{"button_learn":true,"button_gpio_probe":true}'

echo ""
echo "Press buttons on the gadget. Ctrl+C to exit."
echo ""

mosquitto_sub -h "$HOST" -p "$PORT" -t 'hub/debug/#' -v | while read -r _topic payload; do
  echo "$payload" | python3 -c "
import json, sys
raw = sys.stdin.read().strip()
try:
    d = json.loads(raw)
except json.JSONDecodeError:
    print(raw)
    raise SystemExit
if 'pin' in d and 'edge' in d:
    pin = d['pin']
    lvl = d.get('level', '?')
    edge = d.get('edge', '')
    state = 'pressed' if edge == 'falling' else 'released'
    print(f'Button? GPIO {pin} — {state} (level {lvl})')
elif d.get('changed'):
    pins = d.get('pins', {})
    for p in d['changed']:
        print(f'GPIO {p} = {pins.get(str(p), pins.get(p, \"?\"))}')
"
done
