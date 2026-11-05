#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UUID="crowsnest-focus"
SOURCE_DIR="$ROOT_DIR/kwin_script/$UUID"

DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET_DIR="$DATA_DIR/kwin/scripts/$UUID"

if ! command -v kwriteconfig6 >/dev/null 2>&1 \
   && ! command -v kwriteconfig5 >/dev/null 2>&1; then
  echo "kwriteconfig6/5 not found. Run this from a KDE Plasma session." >&2
  echo "You can still copy the script manually into:" >&2
  echo "  $TARGET_DIR" >&2
fi

mkdir -p "$TARGET_DIR/contents/code"
install -m 0644 "$SOURCE_DIR/metadata.json" "$TARGET_DIR/metadata.json"
install -m 0644 "$SOURCE_DIR/contents/code/main.js" "$TARGET_DIR/contents/code/main.js"

if command -v kwriteconfig6 >/dev/null 2>&1; then
  kwriteconfig6 --file kwinrc --group Plugins --key "${UUID}Enabled" true
elif command -v kwriteconfig5 >/dev/null 2>&1; then
  kwriteconfig5 --file kwinrc --group Plugins --key "${UUID}Enabled" true
fi

if command -v qdbus6 >/dev/null 2>&1; then
  qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure 2>/dev/null || true
elif command -v qdbus >/dev/null 2>&1; then
  qdbus org.kde.KWin /KWin org.kde.KWin.reconfigure 2>/dev/null || true
fi

echo "Installed KWin script '$UUID'."
echo "If it does not activate, enable it in System Settings > Window Management > KWin Scripts."
