#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_SRC="$ROOT_DIR/build/crowsnest"
BINDIR="${XDG_BINDIR:-$HOME/.local/bin}"
BIN_DST="$BINDIR/crowsnest"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
UNIT_DIR="$CONFIG_HOME/systemd/user"
UNIT_FILE="$UNIT_DIR/crowsnest.service"
AUTOSTART_DIR="$CONFIG_HOME/autostart"
AUTOSTART_FILE="$AUTOSTART_DIR/crowsnest.desktop"

use_systemd=1
mode="install"

for arg in "$@"; do
  case "$arg" in
    --uninstall) mode="uninstall" ;;
    --no-systemd) use_systemd=0 ;;
    -h|--help)
      echo "Usage: $0 [--uninstall] [--no-systemd]"
      echo "  XDG_BINDIR, XDG_CONFIG_HOME, HOME affect install locations."
      exit 0
      ;;
    *)
      echo "unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

have_systemd() {
  [ "$use_systemd" -eq 1 ] \
    && command -v systemctl >/dev/null 2>&1 \
    && systemctl --user show-environment >/dev/null 2>&1
}

do_uninstall() {
  if [ "$use_systemd" -eq 1 ] && command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now crowsnest.service 2>/dev/null || true
  fi
  rm -f "$UNIT_FILE" "$AUTOSTART_FILE" "$BIN_DST"
  if [ "$use_systemd" -eq 1 ] && command -v systemctl >/dev/null 2>&1; then
    systemctl --user daemon-reload 2>/dev/null || true
  fi
  echo "Crowsnest uninstalled (binary, service, autostart)."
}

if [ "$mode" = "uninstall" ]; then
  do_uninstall
  exit 0
fi

if [ ! -x "$BIN_SRC" ]; then
  echo "Build the project first:" >&2
  echo "  cmake -S cpp -B cpp/build && cmake --build cpp/build -j" >&2
  exit 1
fi

mkdir -p "$BINDIR"
install -m 0755 "$BIN_SRC" "$BIN_DST"

if have_systemd; then
  mkdir -p "$UNIT_DIR"
  sed "s|@BINARY@|$BIN_DST|g" \
    "$ROOT_DIR/packaging/crowsnest.service.in" > "$UNIT_FILE"
  systemctl --user daemon-reload
  systemctl --user import-environment DISPLAY WAYLAND_DISPLAY \
    XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_RUNTIME_DIR 2>/dev/null || true
  systemctl --user enable --now crowsnest.service
  echo "Installed systemd user service: $UNIT_FILE"
  echo "Started crowsnest.service."
  echo "Status: systemctl --user status crowsnest.service"
else
  mkdir -p "$AUTOSTART_DIR"
  sed "s|@BINARY@|$BIN_DST|g" \
    "$ROOT_DIR/packaging/crowsnest.desktop.in" > "$AUTOSTART_FILE"
  echo "systemd user service not used; installed XDG autostart entry:"
  echo "  $AUTOSTART_FILE"
fi

echo "Binary: $BIN_DST"
echo "Uninstall with: $0 --uninstall"
