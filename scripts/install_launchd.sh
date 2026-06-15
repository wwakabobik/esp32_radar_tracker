#!/usr/bin/env bash
# Install Presence Hub daemon as macOS LaunchAgent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLIST_SRC="$ROOT/scripts/com.presence-hub.daemon.plist"
PLIST_DST="$HOME/Library/LaunchAgents/com.presence-hub.daemon.plist"

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
cp "$PLIST_SRC" "$PLIST_DST"
launchctl unload "$PLIST_DST" 2>/dev/null || true
launchctl load "$PLIST_DST"
echo "Installed: $PLIST_DST"
echo "Logs: ~/Library/Logs/presence-hub.log"
