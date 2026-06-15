#!/usr/bin/env bash
# Flash Presence Hub firmware over USB.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/firmware"

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT=$(ls /dev/cu.* 2>/dev/null | grep -iE "usb|uart|ch34|cp21|ftdi|wchusb|serial" | head -1 || true)
fi
if [[ -z "$PORT" ]]; then
  echo "ERROR: ESP32 not found. Plug in USB and retry, or: $0 /dev/cu.usbserial-XXXX" >&2
  exit 1
fi

echo "==> Port: $PORT"
pio run -t upload --upload-port "$PORT"
echo "==> Done. Monitor: pio device monitor --port $PORT"
