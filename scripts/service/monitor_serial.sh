#!/usr/bin/env bash
# Monitor ESP32 serial output (logs)
# Usage: ./scripts/service/monitor_serial.sh [port] [baud]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
LOG_DIR="$REPO_ROOT/logs"

find_port() {
    local ports
    ports=$(ls /dev/cu.* 2>/dev/null | grep -i -E "usb|uart|ch34|cp21|ftdi|wchusb" || true)
    if [[ -z "$ports" ]]; then
        echo "ERROR: No USB serial device found." >&2
        exit 1
    fi
    echo "$ports" | head -1
}

PORT="${1:-$(find_port)}"
BAUD="${2:-115200}"

mkdir -p "$LOG_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/serial_${TIMESTAMP}.log"

echo "==> Port:  $PORT"
echo "==> Baud:  $BAUD"
echo "==> Log:   $LOG_FILE"
echo "==> Press Ctrl+C to stop"
echo ""

"$VENV_DIR/bin/python" - "$PORT" "$BAUD" "$LOG_FILE" <<'PYEOF'
import datetime
import signal
import sys

port = sys.argv[1]
baud = int(sys.argv[2])
log_file = sys.argv[3]

try:
    import serial
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial", "-q"])
    import serial

RED = "\033[91m"
YELLOW = "\033[93m"
GREEN = "\033[92m"
CYAN = "\033[96m"
RESET = "\033[0m"


def colorize(line):
    lower = line.lower()
    if any(k in lower for k in ["error", "fail", "fault", "panic", "abort", "exception"]):
        return RED + line + RESET
    if any(k in lower for k in ["warn", "warning"]):
        return YELLOW + line + RESET
    if any(k in lower for k in ["http", "https", "mqtt", "tcp", "udp", "connect", "socket"]):
        return CYAN + line + RESET
    if any(k in lower for k in ["wifi", "ip", "ssid", "password", "key", "token", "id", "url", "host"]):
        return GREEN + line + RESET
    return line


def signal_handler(sig, frame):
    print(f"\n\nLog saved to: {log_file}")
    sys.exit(0)


signal.signal(signal.SIGINT, signal_handler)

print(f"Opening {port} at {baud} baud...\n")

with serial.Serial(port, baud, timeout=1) as ser, open(log_file, "wb") as logf:
    while True:
        try:
            raw = ser.readline()
            if not raw:
                continue
            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            try:
                line = raw.decode("utf-8", errors="replace").rstrip()
            except Exception:
                line = repr(raw)
            logf.write(f"[{ts}] {line}\n".encode("utf-8"))
            logf.flush()
            print(f"[{ts}] {colorize(line)}")
        except serial.SerialException as e:
            print(f"Serial error: {e}")
            break
PYEOF
