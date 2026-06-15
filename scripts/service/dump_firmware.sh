#!/usr/bin/env bash
# Dump full firmware flash from ESP32
# Usage: ./scripts/service/dump_firmware.sh [port] [output_dir]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
ESPTOOL="$VENV_DIR/bin/esptool"
OUTPUT_DIR="${2:-$REPO_ROOT/dumps}"

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
mkdir -p "$OUTPUT_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
DUMP_FILE="$OUTPUT_DIR/firmware_${TIMESTAMP}.bin"

echo "==> Port:   $PORT"
echo "==> Output: $DUMP_FILE"
echo ""

echo "--- Detecting flash size ---"
FLASH_ID_OUT=$("$ESPTOOL" --port "$PORT" flash_id 2>&1)
echo "$FLASH_ID_OUT"

FLASH_SIZE=$(echo "$FLASH_ID_OUT" | grep -i "Detected flash size" | grep -oE "[0-9]+MB" | head -1 || echo "4MB")
echo ""
echo "Flash size: $FLASH_SIZE"

case "$FLASH_SIZE" in
    1MB)  SIZE_HEX="0x100000" ;;
    2MB)  SIZE_HEX="0x200000" ;;
    4MB)  SIZE_HEX="0x400000" ;;
    8MB)  SIZE_HEX="0x800000" ;;
    16MB) SIZE_HEX="0x1000000" ;;
    *)    SIZE_HEX="0x400000" ; echo "Unknown size, defaulting to 4MB" ;;
esac

echo ""
echo "==> Dumping $FLASH_SIZE ($SIZE_HEX bytes)..."
"$ESPTOOL" --port "$PORT" --baud 921600 read_flash 0x00000 "$SIZE_HEX" "$DUMP_FILE"

echo ""
echo "==> Done! Firmware saved to: $DUMP_FILE"
echo "    Size: $(du -sh "$DUMP_FILE" | cut -f1)"

PART_FILE="$OUTPUT_DIR/partition_table_${TIMESTAMP}.bin"
echo ""
echo "==> Dumping partition table (0x8000, 0xC00 bytes)..."
"$ESPTOOL" --port "$PORT" read_flash 0x8000 0xC00 "$PART_FILE" 2>&1 || true

echo ""
echo "==> Parsing partition table..."
DUMPS_DIR="$OUTPUT_DIR" "$VENV_DIR/bin/python" - <<'PYEOF'
import os
import sys

dumps_dir = os.environ.get("DUMPS_DIR", ".")
files = sorted([f for f in os.listdir(dumps_dir) if f.startswith("partition_table_")])
if not files:
    print("No partition table dumps found")
    sys.exit(0)

part_file = os.path.join(dumps_dir, files[-1])

with open(part_file, "rb") as f:
    data = f.read()

print(f"{'Name':<16} {'Type':<6} {'SubType':<8} {'Offset':<12} {'Size':<12}")
print("-" * 60)

offset = 0
while offset + 32 <= len(data):
    entry = data[offset:offset + 32]
    if entry[0] == 0xAA and entry[1] == 0x50:
        ptype = entry[2]
        subtype = entry[3]
        part_offset = int.from_bytes(entry[4:8], "little")
        part_size = int.from_bytes(entry[8:12], "little")
        name_raw = entry[12:28]
        name = name_raw.split(b"\x00")[0].decode("utf-8", errors="replace")
        type_str = {0: "app", 1: "data"}.get(ptype, f"0x{ptype:02x}")
        print(f"{name:<16} {type_str:<6} 0x{subtype:02x}     0x{part_offset:08x}   0x{part_size:08x}")
    elif entry == b"\xFF" * 32:
        break
    offset += 32
PYEOF
