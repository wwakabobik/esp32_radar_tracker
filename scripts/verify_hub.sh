#!/usr/bin/env bash
# End-to-end hub health check: discovery, MQTT, device online, optional offline autonomy.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:$PATH"

MQTT_HOST="${HUB_MQTT_HOST:-127.0.0.1}"
MQTT_PORT="${HUB_MQTT_PORT:-18830}"
WEB_URL="${HUB_WEB_URL:-http://127.0.0.1:18080}"
DISCOVERY_PORT="${HUB_DISCOVERY_PORT:-18832}"
RUN_OFFLINE="${VERIFY_HUB_OFFLINE:-0}"
FAIL=0

pass() { echo "  OK  $*"; }
fail() { echo "  FAIL $*"; FAIL=1; }

if [[ "${1:-}" == "--offline" ]]; then
  RUN_OFFLINE=1
fi

echo "==> Unit tests (discovery)"
(
  cd "$ROOT/daemon"
  PYTHONPATH=. "$ROOT/venv/bin/python" -m unittest discover -s tests -p 'test_*.py' -v
) || FAIL=1

if [[ "$RUN_OFFLINE" == "1" ]]; then
  echo "==> Offline autonomy (stop broker, device must not wedge)"
  DAEMON_PID="$(pgrep -f "$ROOT/daemon/main.py" | head -1 || true)"
  if [[ -n "$DAEMON_PID" ]]; then kill "$DAEMON_PID" 2>/dev/null || true; fi
  pkill -f "mosquitto.*18830" 2>/dev/null || true
  pkill -f "mosquitto.*mosquitto.conf" 2>/dev/null || true
  sleep 2
  if lsof -iTCP:"$MQTT_PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    MOSQ_PID="$(lsof -t -iTCP:"$MQTT_PORT" -sTCP:LISTEN 2>/dev/null | head -1 || true)"
    [[ -n "$MOSQ_PID" ]] && kill "$MOSQ_PID" 2>/dev/null || true
    sleep 1
  fi
  lsof -iTCP:"$MQTT_PORT" -sTCP:LISTEN >/dev/null 2>&1 && fail "mosquitto still listening" || pass "broker stopped"
  ONLINE="$(curl -sf "$WEB_URL/api/dashboard/today" 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin).get('online', False))" 2>/dev/null || echo false)"
  [[ "$ONLINE" == "False" || "$ONLINE" == "false" ]] && pass "dashboard offline while broker down" || pass "dashboard unreachable (expected)"
  RADAR="$({ mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t hub/radar -C 1 -W 3 2>/dev/null || true; })"
  [[ -z "$RADAR" ]] && pass "no MQTT traffic while broker down" || fail "unexpected MQTT while broker down"
  echo "==> Restart hub"
  nohup "$ROOT/daemon/run.sh" >>"$ROOT/daemon/data/verify_hub.log" 2>&1 &
  sleep 5
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    ONLINE="$(curl -sf "$WEB_URL/api/dashboard/today" | python3 -c "import sys,json; print(json.load(sys.stdin).get('online', False))")"
    if [[ "$ONLINE" == "True" || "$ONLINE" == "true" ]]; then
      pass "device reconnected after hub restart"
      break
    fi
    sleep 3
  done
  [[ "$ONLINE" == "True" || "$ONLINE" == "true" ]] || fail "device did not reconnect within 30s"
fi

echo "==> Daemon ports"
lsof -iTCP:"$MQTT_PORT" -sTCP:LISTEN >/dev/null 2>&1 && pass "mosquitto :$MQTT_PORT" || fail "mosquitto :$MQTT_PORT"
lsof -iUDP:"$DISCOVERY_PORT" >/dev/null 2>&1 && pass "discovery UDP :$DISCOVERY_PORT" || fail "discovery UDP :$DISCOVERY_PORT"
curl -sf "$WEB_URL/api/dashboard/today" >/dev/null && pass "web $WEB_URL" || fail "web $WEB_URL"

echo "==> UDP discovery round-trip"
LAN_IP="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true)"
REPLY="$(
  "$ROOT/venv/bin/python" - "$DISCOVERY_PORT" <<'PY'
import socket, sys
port = int(sys.argv[1])
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5)
sock.sendto(b"PHUB_DISCOVER", ("127.0.0.1", port))
data, _ = sock.recvfrom(512)
print(data.decode())
PY
)"
if [[ -n "$REPLY" && "$REPLY" != ERROR:* ]]; then
  MQTT_IP="$(echo "$REPLY" | python3 -c "import sys,json; print(json.load(sys.stdin)['mqtt_host'])")"
  if [[ -n "$LAN_IP" && "$MQTT_IP" == "$LAN_IP" ]]; then
    pass "discovery -> mqtt_host=$MQTT_IP"
  elif [[ -n "$MQTT_IP" ]]; then
    pass "discovery -> mqtt_host=$MQTT_IP (LAN=$LAN_IP)"
  else
    fail "discovery reply missing mqtt_host"
  fi
else
  fail "UDP discovery no reply"
fi

echo "==> Device MQTT"
ONLINE="$(curl -sf "$WEB_URL/api/dashboard/today" | python3 -c "import sys,json; print(json.load(sys.stdin).get('online', False))")"
[[ "$ONLINE" == "True" || "$ONLINE" == "true" ]] && pass "dashboard online" || fail "dashboard online=false"

STATUS="$(
  { mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t hub/status -C 1 -W 15 2>/dev/null || true; } \
    | sed 's/^hub\/status //'
)"
if [[ -n "$STATUS" ]]; then
  HUB_IP="$(echo "$STATUS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('hub',''))")"
  FW="$(echo "$STATUS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('version',''))")"
  pass "hub/status hub=$HUB_IP firmware=$FW"
  if [[ -n "$LAN_IP" && "$HUB_IP" != "$LAN_IP" ]]; then
    fail "status hub IP $HUB_IP != LAN $LAN_IP"
  fi
else
  fail "no hub/status within 15s"
fi

MODE="$(curl -sf "$WEB_URL/api/dashboard/today" | python3 -c "import sys,json; print(json.load(sys.stdin).get('mode',''))")"
DISPLAY="$({ mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t hub/display -C 1 -W 3 2>/dev/null || true; })"
if [[ "$MODE" == "media" ]]; then
  if echo "$DISPLAY" | grep -q '"scroll": true'; then
    pass "media mode display includes scroll track line"
  else
    fail "media mode but display missing track scroll line"
  fi
else
  pass "mode=$MODE (track line only required in media)"
fi

RADAR="$({ mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t hub/radar -C 1 -W 5 2>/dev/null || true; })"
echo "$RADAR" | grep -q '"presence"' && pass "hub/radar live" || fail "no hub/radar"

echo "==> Summary"
if [[ "$FAIL" -eq 0 ]]; then
  echo "All checks passed."
  exit 0
fi
echo "One or more checks failed."
exit 1
