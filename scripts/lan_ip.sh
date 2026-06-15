#!/usr/bin/env bash
# Print primary LAN IP for MQTT/OTA config (WiFi interface).
set -euo pipefail
for iface in en0 en1; do
  ip=$(ipconfig getifaddr "$iface" 2>/dev/null || true)
  if [[ -n "$ip" ]]; then
    echo "$ip"
    exit 0
  fi
done
echo "ERROR: no LAN IP found on en0/en1" >&2
exit 1
