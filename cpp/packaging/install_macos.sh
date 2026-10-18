#!/bin/sh
# Syspeek macOS service management (mirrors install.sh on Linux).
#
# Installs run | start | stop | restart | status | logs | uninstall | path
# through launchctl + ~/Library/LaunchAgents (login autostart, no login item
# in System Settings needed).

set -eu

BUNDLE_ID="dev.syspeek.hud"
LABEL="$BUNDLE_ID"
PLIST="${HOME}/Library/LaunchAgents/${BUNDLE_ID}.plist"
LOG_FILE="${HOME}/Library/Logs/Syspeek/syspeek.log"
ERR_FILE="${HOME}/Library/Logs/Syspeek/syspeek.err.log"

BIN_DEFAULT="/Applications/Syspeek.app/Contents/MacOS/syspeek"
BIN="${SYSPEEK_BIN:-$BIN_DEFAULT}"
if [ ! -x "$BIN" ] && [ -x "/Applications/Syspeek.app/Contents/MacOS/Syspeek" ]; then
    # Older bundles capitalised the executable name; tolerate both.
    BIN="/Applications/Syspeek.app/Contents/MacOS/Syspeek"
fi
if [ ! -x "$BIN" ]; then
    BIN="{{BIN_PATH}}"
fi

mkdir -p "$(dirname "$PLIST")" 2>/dev/null || true

usage() {
    cat <<'USAGE'
usage: syspeek.sh {run|start|stop|restart|status|logs|enable|disable|uninstall|path}

  run         run the HUD in the foreground (management mode: no LaunchAgent)
  start       load the LaunchAgent and start the HUD
  stop        stop the HUD (LaunchAgent stays loaded)
  restart     stop + start
  status      show whether the plist is loaded and the process running
  logs        tail the HUD stdout/stderr log
  enable      install the LaunchAgent plist (login autostart)
  disable     remove the LaunchAgent plist
  uninstall   disable + stop + remove plist
  path        print the binary path
USAGE
}

ensure_log_dir() {
    mkdir -p "$(dirname "$LOG_FILE")"
}

write_plist() {
    ensure_log_dir
    cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BIN</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ProcessType</key>
    <string>Interactive</string>
    <key>StandardOutPath</key>
    <string>$LOG_FILE</string>
    <key>StandardErrorPath</key>
    <string>$ERR_FILE</string>
</dict>
</plist>
PLIST
}

cmd="${1:-}"
case "$cmd" in
    run)
        exec "$BIN"
        ;;
    start)
        if [ ! -f "$PLIST" ]; then
            write_plist
        fi
        launchctl bootstrap gui/"$(id -u)" "$PLIST" 2>/dev/null \
            || launchctl load "$PLIST" 2>/dev/null || true
        launchctl kickstart gui/"$(id -u)"/$LABEL 2>/dev/null || true
        echo "started ($LABEL)"
        ;;
    stop)
        launchctl bootout gui/"$(id -u)"/$LABEL 2>/dev/null \
            || launchctl unload "$PLIST" 2>/dev/null || true
        echo "stopped"
        ;;
    restart)
        "$0" stop
        "$0" start
        ;;
    status)
        if [ -f "$PLIST" ]; then
            plistState="plist=installed"
        else
            plistState="plist=missing"
        fi
        if pgrep -f "$BIN" > /dev/null 2>&1; then
            procState="process=running pid=$(pgrep -f "$BIN" | head -1)"
        else
            procState="process=stopped"
        fi
        echo "$plistState $procState"
        ;;
    logs)
        ensure_log_dir
        tail -n 80 -f "$LOG_FILE" "$ERR_FILE" 2>/dev/null
        ;;
    enable)
        write_plist
        launchctl bootstrap gui/"$(id -u)" "$PLIST" 2>/dev/null \
            || launchctl load "$PLIST" 2>/dev/null || true
        echo "enabled at login ($PLIST)"
        ;;
    disable)
        launchctl bootout gui/"$(id -u)"/$LABEL 2>/dev/null \
            || launchctl unload "$PLIST" 2>/dev/null || true
        rm -f "$PLIST"
        echo "disabled"
        ;;
    uninstall)
        "$0" disable
        echo "uninstalled"
        ;;
    path)
        echo "$BIN"
        ;;
    "")
        usage
        ;;
    *)
        usage
        exit 2
        ;;
esac
