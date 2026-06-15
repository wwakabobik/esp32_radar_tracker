#!/usr/bin/env bash
# Detect connected ESP32 USB serial port

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
ESPTOOL="$VENV_DIR/bin/esptool"

find_port() {
    local ports
    ports=$(ls /dev/cu.* 2>/dev/null | grep -i -E "usb|uart|ch34|cp21|ftdi|wchusb" || true)
    if [[ -z "$ports" ]]; then
        echo "ERROR: No USB serial device found. Connect ESP32 and try again." >&2
        exit 1
    fi
    echo "$ports" | head -1
}

PORT=$(find_port)
echo "Detected port: $PORT"

echo ""
echo "--- ESP32 chip info ---"
"$ESPTOOL" --port "$PORT" chip_id 2>&1 || true

echo ""
echo "--- Flash info ---"
"$ESPTOOL" --port "$PORT" flash_id 2>&1 || true
