#!/usr/bin/env bash
# Слушает нажатия: какой GPIO дёрнулся (режим button_learn на устройстве).
set -euo pipefail

HOST="${MQTT_HOST:-127.0.0.1}"
PORT="${MQTT_PORT:-18830}"

echo "Включаю режим прослушивания на ESP32..."
mosquitto_pub -h "$HOST" -p "$PORT" -t hub/config -m '{"button_learn":true,"button_gpio_probe":true}'

echo ""
echo "Жми кнопки на гаджете. Ctrl+C — выход."
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
    ru = 'нажата' if edge == 'falling' else 'отпущена'
    print(f'Кнопка? GPIO {pin} — {ru} (уровень {lvl})')
elif d.get('changed'):
    pins = d.get('pins', {})
    for p in d['changed']:
        print(f'GPIO {p} = {pins.get(str(p), pins.get(p, \"?\"))}')
"
done
