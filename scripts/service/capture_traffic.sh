#!/usr/bin/env bash
# Capture network traffic from ESP32 WiFi activity
# Uses mitmproxy (HTTP/HTTPS MITM) + tshark (raw capture)
# Usage: ./scripts/service/capture_traffic.sh <mode> [options]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
PCAP_DIR="$REPO_ROOT/captures"
MITM_DIR="$REPO_ROOT/mitm_logs"

mkdir -p "$PCAP_DIR" "$MITM_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

MODE="${1:-help}"

case "$MODE" in

  raw)
    IFACE="${2:-en0}"
    FILTER="${3:-}"
    PCAP_FILE="$PCAP_DIR/capture_${TIMESTAMP}.pcap"

    echo "==> Raw capture on interface: $IFACE"
    echo "==> Filter: ${FILTER:-<none>}"
    echo "==> Output: $PCAP_FILE"
    echo "==> Press Ctrl+C to stop"
    echo ""

    if [[ -n "$FILTER" ]]; then
        tshark -i "$IFACE" -w "$PCAP_FILE" -f "$FILTER" -P 2>&1
    else
        tshark -i "$IFACE" -w "$PCAP_FILE" -P 2>&1
    fi

    echo ""
    echo "Capture saved: $PCAP_FILE"
    echo "Open with: open -a Wireshark $PCAP_FILE"
    ;;

  interfaces)
    echo "Available network interfaces:"
    tshark -D 2>&1
    ;;

  live)
    IFACE="${2:-en0}"
    FILTER="${3:-}"
    echo "==> Live decode on: $IFACE  filter: '${FILTER:-<all>}'"
    echo "==> Ctrl+C to stop"
    if [[ -n "$FILTER" ]]; then
        tshark -i "$IFACE" -f "$FILTER" -V -l 2>&1 | grep -E --line-buffered \
            "HTTP|Host:|GET |POST |PUT |DELETE |Content-Type|Authorization|User-Agent|Location|Set-Cookie|MQTT|TLS|DNS|Frame|IP|TCP|UDP" || true
    else
        tshark -i "$IFACE" -V -l 2>&1 | grep -E --line-buffered \
            "HTTP|Host:|GET |POST |PUT |DELETE |Content-Type|Authorization|User-Agent|Location|Set-Cookie|MQTT|TLS|DNS|Frame|IP|TCP|UDP" || true
    fi
    ;;

  mitm)
    MITM_PORT="${2:-8080}"
    FLOW_FILE="$MITM_DIR/flows_${TIMESTAMP}.mitm"

    echo "==> Starting mitmproxy on port $MITM_PORT"
    echo "==> Flow log: $FLOW_FILE"
    echo ""
    echo "HOW TO USE:"
    echo "  1. Connect ESP32 to the same WiFi"
    echo "  2. Set HTTP proxy on the router/AP to: <your_mac_ip>:$MITM_PORT"
    echo "     OR configure ESP32 firmware to use a proxy (if supported)"
    echo "  3. For HTTPS: install mitmproxy CA cert on the device"
    echo "     CA cert location: ~/.mitmproxy/mitmproxy-ca-cert.pem"
    echo ""
    echo "Press Ctrl+C to stop"
    echo ""

    "$VENV_DIR/bin/mitmproxy" \
        --listen-port "$MITM_PORT" \
        --save-stream-file "$FLOW_FILE" \
        --set flow_detail=3 2>&1
    ;;

  mitmdump)
    MITM_PORT="${2:-8080}"
    FLOW_FILE="$MITM_DIR/flows_${TIMESTAMP}.mitm"
    LOG_FILE="$MITM_DIR/traffic_${TIMESTAMP}.log"

    echo "==> Starting mitmdump on port $MITM_PORT"
    echo "==> Flows: $FLOW_FILE"
    echo "==> Log:   $LOG_FILE"
    echo "==> Ctrl+C to stop"
    echo ""

    "$VENV_DIR/bin/mitmdump" \
        --listen-port "$MITM_PORT" \
        --save-stream-file "$FLOW_FILE" \
        -s "$SCRIPT_DIR/mitm_addon.py" 2>&1 | tee "$LOG_FILE"
    ;;

  analyze)
    FILE="${2:-}"
    if [[ -z "$FILE" ]]; then
        FILE=$(ls -t "$PCAP_DIR"/*.pcap 2>/dev/null | head -1 || true)
    fi
    if [[ -z "$FILE" ]]; then
        echo "No pcap file found. Run: $0 raw"
        exit 1
    fi
    echo "==> Analyzing: $FILE"
    echo ""
    echo "--- HTTP requests ---"
    tshark -r "$FILE" -Y "http.request" -T fields \
        -e ip.src -e ip.dst -e http.request.method \
        -e http.host -e http.request.uri \
        -e http.authorization \
        -E separator="  " 2>/dev/null | head -50 || echo "No HTTP requests found"

    echo ""
    echo "--- DNS queries ---"
    tshark -r "$FILE" -Y "dns.flags.response == 0" -T fields \
        -e frame.time_relative -e ip.src -e dns.qry.name \
        -E separator="  " 2>/dev/null | head -30 || echo "No DNS queries found"

    echo ""
    echo "--- TLS SNI (destinations) ---"
    tshark -r "$FILE" -Y "tls.handshake.extensions_server_name" -T fields \
        -e ip.src -e ip.dst -e tls.handshake.extensions_server_name \
        -E separator="  " 2>/dev/null | head -30 || echo "No TLS SNI found"

    echo ""
    echo "--- MQTT (if any) ---"
    tshark -r "$FILE" -Y "mqtt" -T fields \
        -e ip.src -e ip.dst -e mqtt.topic -e mqtt.msg \
        -E separator="  " 2>/dev/null | head -30 || echo "No MQTT found"

    echo ""
    echo "--- All unique endpoints ---"
    tshark -r "$FILE" -T fields -e ip.dst -e tcp.dstport -e udp.dstport \
        -E separator=":" 2>/dev/null | sort -u | grep -v "^$" | head -40 || true
    ;;

  *)
    echo "ESP32 Traffic Capture Tool"
    echo ""
    echo "Usage: $0 <mode> [options]"
    echo ""
    echo "Modes:"
    echo "  interfaces              List available network interfaces"
    echo "  raw [iface] [filter]    Raw pcap capture (default: en0)"
    echo "  live [iface] [filter]   Live human-readable decode"
    echo "  mitm [port]             Interactive MITM proxy (HTTP/HTTPS)"
    echo "  mitmdump [port]         Non-interactive MITM proxy + auto-log"
    echo "  analyze [file.pcap]     Analyze saved capture"
    echo ""
    echo "Examples:"
    echo "  $0 interfaces"
    echo "  $0 raw en0 'host 192.168.1.50'"
    echo "  $0 live en0 'port 80 or port 8883'"
    echo "  $0 mitmdump 8080"
    echo "  $0 analyze captures/capture_20260605_102000.pcap"
    ;;
esac
