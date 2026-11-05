#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UUID="crowsnest-focus@codeafridi"
SOURCE_DIR="$ROOT_DIR/gnome_extension/$UUID"

DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET_DIR="$DATA_DIR/gnome-shell/extensions/$UUID"

if ! command -v gsettings >/dev/null 2>&1; then
  echo "gsettings was not found. Run this from a GNOME desktop session." >&2
  exit 1
fi

mkdir -p "$TARGET_DIR"
install -m 0644 "$SOURCE_DIR/metadata.json" "$TARGET_DIR/metadata.json"
install -m 0644 "$SOURCE_DIR/extension.js" "$TARGET_DIR/extension.js"

enabled_extensions="$(gsettings get org.gnome.shell enabled-extensions)"
if [[ "$enabled_extensions" != *"'$UUID'"* ]]; then
  enabled_extensions="${enabled_extensions#@as }"
  if [[ "$enabled_extensions" == "[]" ]]; then
    enabled_extensions="['$UUID']"
  else
    enabled_extensions="${enabled_extensions%]}"
    enabled_extensions+=" , '$UUID']"
  fi
  gsettings set org.gnome.shell enabled-extensions "$enabled_extensions"
fi

echo "Installed $UUID. GNOME loads newly installed extensions at the next login."
