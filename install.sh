#!/bin/sh
# Syspeek installer (Linux x86-64, macOS, Windows).
#
# One-line install — the SAME command on every OS:
#   curl -fsSL https://raw.githubusercontent.com/codeafridi/Syspeek/main/install.sh | sh
#   (or, when curl is absent:  wget -qO- ...   — see README)
# Everything the installer downloads after bootstrap goes through a
# fallback ladder: native curl -> wget -> snap curl -> busybox wget ->
# distro package-manager rescue -> manual instructions.
#
# Linux x86-64 (glibc >= 2.34): downloads a self-contained build (Qt 6 bundled
# — nothing else to install) into ~/.local/opt/syspeek, adds a `syspeek`
# command, starts it at login, and installs the desktop focus bridge
# (GNOME extension / KWin script) when one is needed.
#
# macOS: downloads the latest DMG, mounts it, copies Syspeek.app into
# /Applications (falls back to ~/Applications without admin rights),
# installs the LaunchAgent autostart and the `syspeek` command in
# /usr/local/bin, and clears the quarantine attribute so the unsigned
# app starts like any other.
#
# Windows (run inside Git Bash / MSYS2): downloads the portable zip into
# %LOCALAPPDATA%\Syspeek, adds a `syspeek` command through the WindowsApps
# PATH directory, sets HKCU Run autostart via the app's own --autostart
# verb, and starts the HUD.
#
# Options:
#   --no-autostart   install without enabling start-at-login
#   --no-bridge      skip installing the Linux desktop focus bridge
#   --uninstall      remove the app, command, service, autostart and bridge
#   -h, --help       show this help
#
# Environment overrides:
#   SYSPEEK_BUNDLE_URL  override the Linux portable-bundle URL
#   SYSPEEK_DMG_URL     override the macOS DMG URL
#   SYSPEEK_WIN_URL     override the Windows portable-zip URL
#   SYSPEEK_TEST_TAG    pin the resolved release tag (packaging CI only)
#
set -eu

REPO="codeafridi/Syspeek"
RAW="https://raw.githubusercontent.com/${REPO}/main"
BUNDLE_URL="${SYSPEEK_BUNDLE_URL:-https://github.com/${REPO}/releases/latest/download/syspeek-portable.tar.gz}"

HOME_DIR="${HOME:-/root}"
APP_DIR="${SYSPEEK_DIR:-$HOME_DIR/.local/opt/syspeek}"
BIN_DIR="${XDG_BINDIR:-$HOME_DIR/.local/bin}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME_DIR/.config}"
UNIT_DIR="$CONFIG_HOME/systemd/user"
UNIT_FILE="$UNIT_DIR/syspeek.service"
AUTOSTART_DIR="$CONFIG_HOME/autostart"
AUTOSTART_FILE="$AUTOSTART_DIR/syspeek.desktop"
WRAPPER="$BIN_DIR/syspeek"

GNOME_UUID="syspeek-focus@codeafridi"
KDE_ID="syspeek-focus"

MAC_LABEL="dev.syspeek.hud"
MAC_PLIST="${HOME_DIR}/Library/LaunchAgents/${MAC_LABEL}.plist"
MAC_APP_DEFAULT="/Applications/Syspeek.app"
MAC_CLI="/usr/local/bin/syspeek"

autostart=1
bridge=1
mode=install

for arg in "$@"; do
  case "$arg" in
    --uninstall)    mode=uninstall ;;
    --no-autostart) autostart=0 ;;
    --no-bridge)    bridge=0 ;;
    -h|--help)      sed -n '2,34p' "$0" 2>/dev/null || true; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

log() { printf '%s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

under_snap() { # <tool> — true when the tool resolves under /snap (sandboxed)
  case "$(readlink -f "$(command -v "$1" 2>/dev/null)" 2>/dev/null)" in
    /snap/*) return 0 ;;
    *)       return 1 ;;
  esac
}

pkg_install_curl() { # best-effort distro rescue; 0 iff curl became usable
  if command -v apt-get >/dev/null 2>&1; then
    run_priv apt-get update -y >/dev/null 2>&1 || true
    run_priv apt-get install -y curl >/dev/null 2>&1
  elif command -v dnf >/dev/null 2>&1; then
    run_priv dnf install -y curl >/dev/null 2>&1
  elif command -v yum >/dev/null 2>&1; then
    run_priv yum install -y curl >/dev/null 2>&1
  elif command -v zypper >/dev/null 2>&1; then
    run_priv zypper --non-interactive install curl >/dev/null 2>&1
  elif command -v pacman >/dev/null 2>&1; then
    run_priv pacman -S --noconfirm curl >/dev/null 2>&1
  elif command -v apk >/dev/null 2>&1; then
    run_priv apk add curl >/dev/null 2>&1
  else
    return 1
  fi
  command -v curl >/dev/null 2>&1 && command curl -fsSL \
    "https://github.com/${REPO}" -o /dev/null >/dev/null 2>&1
}

fetch() { # <url> <dest> — downloader ladder (M58a), works without native curl
  if command -v curl >/dev/null 2>&1 && ! under_snap curl; then
    curl -fsSL --connect-timeout 10 -o "$2" "$1" && return 0
  fi
  if command -v wget >/dev/null 2>&1 && ! under_snap wget; then
    wget -q -T 20 -O "$2" "$1" && return 0
  fi
  if command -v curl >/dev/null 2>&1; then
    log "note: the curl on this machine is a snap (sandbox may block writes)"
    curl -fsSL --connect-timeout 10 -o "$2" "$1" && return 0
    log "snap curl failed - continuing down the ladder"
  fi
  if command -v busybox >/dev/null 2>&1; then
    busybox wget -q -T 20 -O "$2" "$1" && return 0
  fi
  log "no working downloader; trying the distro package manager once more"
  if pkg_install_curl; then
    curl -fsSL --connect-timeout 10 -o "$2" "$1" && return 0
    if command -v wget >/dev/null 2>&1; then
      wget -q -T 20 -O "$2" "$1" && return 0
    fi
  fi
  die "could not download $(basename "$1")
install a downloader manually, then re-run this installer:
  debian/ubuntu:  sudo apt-get install -y curl
  fedora/rhel:    sudo dnf install -y curl
  arch:           sudo pacman -S curl
  alpine:         sudo apk add curl
  or get the bundle directly from https://github.com/${REPO}/releases"
}

run_priv() { # <cmd...> — passwordless sudo when possible, else interactive
  if sudo -n "$@" 2>/dev/null; then
    return 0
  fi
  sudo "$@"
}

have_systemd() {
  command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1
}

desktop_id() {
  printf '%s' "${XDG_CURRENT_DESKTOP:-}${XDG_SESSION_DESKTOP:-}" | tr '[:upper:]' '[:lower:]'
}

latest_version_tag() { # echoes v0.8.0 (releases/latest redirects to /releases/tag/<tag>)
  local loc tag
  if command -v curl >/dev/null 2>&1; then
    loc="$(curl -fsS -o /dev/null -w '%{redirect_url}' "https://github.com/${REPO}/releases/latest" || true)"
  elif command -v wget >/dev/null 2>&1; then
    loc="$(wget -q --max-redirect=0 -O /dev/null -S "https://github.com/${REPO}/releases/latest" 2>&1 | grep -i '^ *Location:' | tr -d '\r' | tail -n 1 | awk '{print $2}')"
  fi
  tag="${loc##*/tag/}"
  [ -n "$tag" ] \
    || die "could not resolve the latest release; open https://github.com/${REPO}/releases"
  log "latest release: $tag"
  printf '%s' "$tag"
}

# ---------------------------------------------------------------- macOS ----

mac_install() {
  command -v hdiutil >/dev/null 2>&1 || die "hdiutil is required (macOS only)"
  if [ -n "${SYSPEEK_DMG_URL:-}" ]; then
    dmg_url="$SYSPEEK_DMG_URL"
  elif [ -n "${SYSPEEK_TEST_TAG:-}" ]; then
    dmg_url="https://github.com/${REPO}/releases/download/${SYSPEEK_TEST_TAG}/Syspeek-$(printf '%s' "$SYSPEEK_TEST_TAG" | tr -d v)-macos.dmg"
  else
    tag="$(latest_version_tag)"
    dmg_url="https://github.com/${REPO}/releases/download/${tag}/Syspeek-$(printf '%s' "$tag" | tr -d v)-macos.dmg"
  fi

  tmp="$(mktemp -d "${TMPDIR:-/tmp}/syspeek-mac.XXXXXX")" || die "mktemp failed"
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Syspeek for macOS..."
  fetch "$dmg_url" "$tmp/syspeek.dmg" \
    || die "download failed ($dmg_url). Get the DMG from https://github.com/${REPO}/releases"

  hdiutil attach -nobrowse -readonly -quiet "$tmp/syspeek.dmg" \
    || die "could not mount the DMG"
  mount="/Volumes/Syspeek"
  if [ ! -d "$mount/Syspeek.app" ]; then
    hdiutil detach "$mount" -force -quiet >/dev/null 2>&1 || true
    die "no Syspeek.app inside the DMG"
  fi

  mac_app="$MAC_APP_DEFAULT"
  res_old="$mac_app/Contents/Resources/syspeek"
  res_old_home="${HOME_DIR}/Applications/Syspeek.app/Contents/Resources/syspeek"
  if [ -x "$res_old" ]; then
    "$res_old" disable >/dev/null 2>&1 || true
  elif [ -x "$res_old_home" ]; then
    "$res_old_home" disable >/dev/null 2>&1 || true
  else
    launchctl bootout "gui/$(id -u)/${MAC_LABEL}" >/dev/null 2>&1 || true
  fi

  if [ -w /Applications ]; then
    rm -rf "$MAC_APP_DEFAULT"
    cp -R "$mount/Syspeek.app" "$MAC_APP_DEFAULT"
  elif run_priv rm -rf "$MAC_APP_DEFAULT" \
    && run_priv cp -R "$mount/Syspeek.app" /Applications/; then
    :
  else
    mac_app="${HOME_DIR}/Applications/Syspeek.app"
    mkdir -p "${HOME_DIR}/Applications"
    rm -rf "$mac_app"
    cp -R "$mount/Syspeek.app" "$mac_app"
    log "note: no admin rights for /Applications - installed to ~/Applications"
  fi
  hdiutil detach "$mount" -quiet >/dev/null 2>&1 \
    || hdiutil detach "$mount" -force -quiet >/dev/null 2>&1 || true

  # curl never sets the quarantine attribute; clear it defensively anyway
  # so a re-downloaded DMG places a runnable app.
  xattr -dr com.apple.quarantine "$mac_app" >/dev/null 2>&1 || true
  log "Installed bundle: $mac_app"

  res="$mac_app/Contents/Resources/syspeek"
  mkdir -p "$(dirname "$res")"
  fetch "$RAW/cpp/packaging/install_macos.sh" "$res" \
    || die "could not fetch the macOS verb manager (install_macos.sh)"
  chmod 0755 "$res"

  cat > "$tmp/syspeek-cli" <<EOF
#!/bin/sh
exec "\${SYSPEEK_APP:-$mac_app}/Contents/Resources/syspeek" "\$@"
EOF
  chmod 0755 "$tmp/syspeek-cli"
  mac_cli_dir="$(dirname "$MAC_CLI")"
  mkdir -p "$mac_cli_dir" 2>/dev/null || true
  if [ -w "$mac_cli_dir" ]; then
    cp -f "$tmp/syspeek-cli" "$MAC_CLI"
  elif run_priv /bin/mkdir -p "$mac_cli_dir" \
    && run_priv /usr/bin/install -m 0755 "$tmp/syspeek-cli" "$MAC_CLI"; then
    :
  else
    log "note: could not write $MAC_CLI - the verb manager works directly:"
    log "      \"$res\" {start|stop|restart|status|logs|enable|disable|update|uninstall|path}"
  fi

  if [ "$autostart" -eq 1 ]; then
    "$res" enable
  else
    log "not started - run: syspeek start  (or open Syspeek.app)"
  fi

  cat <<'EOF' >&2
Control it:
  syspeek start       load the LaunchAgent and run the HUD
  syspeek stop        stop the HUD
  syspeek restart     stop + start
  syspeek status      is it running?
  syspeek logs        tail the HUD output
  syspeek update      upgrade to the latest version (re-runs this installer)
  syspeek uninstall   remove everything
EOF
}

mac_uninstall_standalone() {
  for res_try in \
    "$MAC_APP_DEFAULT/Contents/Resources/syspeek" \
    "${HOME_DIR}/Applications/Syspeek.app/Contents/Resources/syspeek"; do
    if [ -x "$res_try" ]; then
      "$res_try" disable >/dev/null 2>&1 || true
      break
    fi
  done
  launchctl bootout "gui/$(id -u)/${MAC_LABEL}" >/dev/null 2>&1 || true
  rm -f "$MAC_PLIST" 2>/dev/null || true
  if [ -w /Applications ]; then
    rm -rf "$MAC_APP_DEFAULT"
  elif run_priv rm -rf "$MAC_APP_DEFAULT"; then
    :
  else
    log "note: could not remove $MAC_APP_DEFAULT without admin rights - remove it in Finder."
  fi
  if [ -w "$(dirname "$MAC_CLI")" ]; then
    rm -f "$MAC_CLI"
  else
    run_priv rm -f "$MAC_CLI" 2>/dev/null || true
  fi
  log "Syspeek uninstalled (macOS)."
}

# ------------------------------------------------------------- Windows ----

win_install() {
  command -v powershell.exe >/dev/null 2>&1 \
    || die "this Windows flow needs Git Bash/MSYS2 (powershell.exe on PATH)"
  command -v cygpath >/dev/null 2>&1 \
    || die "cygpath is required (Git Bash)"
  appdirWin="${LOCALAPPDATA:-${USERPROFILE}\\AppData\\Local}\\Syspeek"
  appdir="$(cygpath -u "$appdirWin" 2>/dev/null \
    || printf '%s' "$appdirWin" | sed 's|\\|/|g')"

  if [ -n "${SYSPEEK_WIN_URL:-}" ]; then
    zip_url="$SYSPEEK_WIN_URL"
  elif [ -n "${SYSPEEK_TEST_TAG:-}" ]; then
    zip_url="https://github.com/${REPO}/releases/download/${SYSPEEK_TEST_TAG}/Syspeek-$(printf '%s' "$SYSPEEK_TEST_TAG" | tr -d v)-windows-portable.zip"
  else
    tag="$(latest_version_tag)"
    zip_url="https://github.com/${REPO}/releases/download/${tag}/Syspeek-$(printf '%s' "$tag" | tr -d v)-windows-portable.zip"
  fi

  tmp="$(mktemp -d "${TMPDIR:-/tmp}/syspeek-win.XXXXXX")" || die "mktemp failed"
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Syspeek for Windows..."
  fetch "$zip_url" "$tmp/syspeek.zip" \
    || die "download failed ($zip_url). Get the installer from https://github.com/${REPO}/releases"

  taskkill //F //IM syspeek.exe >/dev/null 2>&1 || true
  rm -rf "$appdir"
  mkdir -p "$appdir"
  ZIP_WIN="$(cygpath -w "$tmp/syspeek.zip")" \
  DEST_WIN="$(cygpath -w "$appdir")" \
  MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -Command 'Expand-Archive -Path $env:ZIP_WIN -DestinationPath $env:DEST_WIN -Force' \
    || die "unzip (Expand-Archive) failed"
  [ -f "$appdir/syspeek.exe" ] || die "portable install incomplete (syspeek.exe missing)"

  shimDirUnix="$(dirname "$appdir")/Microsoft/WindowsApps"
  shimDirWin="$(cygpath -w "$shimDirUnix")"
  mkdir -p "$shimDirUnix" 2>/dev/null || true
  printf '@echo off\r\nif "%%~1"=="" (\r\n  start "" /b "%%LOCALAPPDATA%%\\Syspeek\\syspeek.exe"\r\n  exit /b 0\r\n)\r\n"%%LOCALAPPDATA%%\\Syspeek\\syspeek.exe" %%*\r\n' \
    > "$shimDirUnix/syspeek.cmd" 2>/dev/null || true
  printf '#!/bin/sh\nif [ "$#" -eq 0 ]; then\n  cmd.exe //c start "" /b "$LOCALAPPDATA\\Syspeek\\syspeek.exe" >/dev/null 2>&1\n  exit 0\nfi\nexec "$LOCALAPPDATA/Syspeek/syspeek.exe" "$@"\n' \
    > "$shimDirUnix/syspeek" 2>/dev/null || true
  chmod 0755 "$shimDirUnix/syspeek" 2>/dev/null || true
  if [ ! -f "$shimDirUnix/syspeek.cmd" ] && [ ! -f "$shimDirUnix/syspeek" ]; then
    log "note: could not write the 'syspeek' command to $shimDirWin"
    log "      run the HUD directly: $(cygpath -w "$appdir/syspeek.exe")"
  fi

  if [ "$autostart" -eq 1 ]; then
    "$appdir/syspeek.exe" --autostart=on
    EXE_WIN="$(cygpath -w "$appdir/syspeek.exe")" \
    MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -Command 'Start-Process -FilePath $env:EXE_WIN' >/dev/null 2>&1 || true
  else
    log "not started - open a NEW shell and type: syspeek"
  fi

  cat <<'EOF' >&2
Control it:
  syspeek                  launch the HUD and return to the prompt
  syspeek --autostart=on|off|status   login autostart (HKCU Run key)
  syspeek --dump           JSON snapshot of the machine (headless check)
  update                   re-run this installer
  uninstall                remove everything
EOF
  log "note: unsigned build - SmartScreen 'More info / Run anyway' on first GUI launch."
  log "note: 'update' = re-run the install one-liner (the .exe has no self-update verb)."
}

win_uninstall_standalone() {
  appdirWin="${LOCALAPPDATA:-${USERPROFILE}\\AppData\\Local}\\Syspeek"
  appdir="$(cygpath -u "$appdirWin" 2>/dev/null \
    || printf '%s' "$appdirWin" | sed 's|\\|/|g')"
  "$appdir/syspeek.exe" --autostart=off 2>/dev/null || true
  taskkill //F //IM syspeek.exe >/dev/null 2>&1 || true
  rm -rf "$appdir"
  shimDirUnix="$(dirname "$appdir")/Microsoft/WindowsApps"
  rm -f "$shimDirUnix/syspeek.cmd" "$shimDirUnix/syspeek" 2>/dev/null || true
  log "Syspeek uninstalled (Windows)."
}

# --------------------------------------------------------------- Linux ----

write_wrapper() {
  mkdir -p "$BIN_DIR"
  cat > "$WRAPPER" <<EOF
#!/bin/sh
set -eu
APP="\${SYSPEEK_DIR:-\$HOME/.local/opt/syspeek}"
UNIT="syspeek.service"
cmd="\${1:-run}"
[ "\$#" -gt 0 ] && shift
case "\$cmd" in
  run|gui)   exec "\$APP/run.sh" "\$@" ;;
  start)     systemctl --user start "\$UNIT" ;;
  stop)      systemctl --user stop "\$UNIT" 2>/dev/null || pkill -f "\$APP/syspeek" 2>/dev/null || true ;;
  restart)   systemctl --user restart "\$UNIT" ;;
  status)    systemctl --user status "\$UNIT" --no-pager 2>/dev/null || pgrep -af "\$APP/syspeek" || echo "not running" ; [ -f "\$APP/VERSION" ] && printf 'version: ' && cat "\$APP/VERSION" || true ;;
  vers)      [ -f "\$APP/VERSION" ] && cat "\$APP/VERSION" || echo "unknown" ;;
  logs)      journalctl --user -u "\$UNIT" -f ;;
  enable)    systemctl --user enable --now "\$UNIT" ;;
  disable)   systemctl --user disable --now "\$UNIT" ;;
  update)    t="\$(mktemp)"
             if wget -q -T 20 -O "\$t" ${RAW}/install.sh 2>/dev/null \
                || curl -fsSL --connect-timeout 10 -o "\$t" ${RAW}/install.sh \
                || busybox wget -q -T 20 -O "\$t" ${RAW}/install.sh; then
               sh "\$t"
             else
               echo "syspeek update: no working downloader (wget/curl/busybox)" >&2
             fi
             rm -f "\$t" ;;
  uninstall) t="\$(mktemp)"
             if wget -q -T 20 -O "\$t" ${RAW}/install.sh 2>/dev/null \
                || curl -fsSL --connect-timeout 10 -o "\$t" ${RAW}/install.sh \
                || busybox wget -q -T 20 -O "\$t" ${RAW}/install.sh; then
               sh "\$t" -s -- --uninstall
             else
               echo "syspeek uninstall: no working downloader (wget/curl/busybox)" >&2
             fi
             rm -f "\$t" ;;
  path)      echo "\$APP" ;;
  *)         exec "\$APP/run.sh" "\$cmd" "\$@" ;;
esac
EOF
  chmod 0755 "$WRAPPER"
}

install_autostart() {
  if have_systemd; then
    mkdir -p "$UNIT_DIR"
    cat > "$UNIT_FILE" <<EOF
[Unit]
Description=Syspeek HUD
[Service]
ExecStart=$APP_DIR/run.sh
Restart=on-failure
[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user import-environment DISPLAY WAYLAND_DISPLAY \
      XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_RUNTIME_DIR 2>/dev/null || true
    if [ "$autostart" -eq 1 ]; then
      systemctl --user enable --now syspeek.service
    else
      systemctl --user start syspeek.service 2>/dev/null || true
    fi
    log "Installed systemd user service: $UNIT_FILE"
  else
    mkdir -p "$AUTOSTART_DIR"
    cat > "$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Syspeek
Exec=$APP_DIR/run.sh
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    log "Installed XDG autostart entry: $AUTOSTART_FILE"
    if [ "$autostart" -eq 1 ]; then
      ( setsid "$APP_DIR/run.sh" >/dev/null 2>&1 & ) || true
    fi
    log "note: no systemd user session; stop with: pkill -f syspeek"
  fi
}

enable_gnome_extension() { # <uuid>
  command -v gsettings >/dev/null 2>&1 || return 0
  cur="$(gsettings get org.gnome.shell enabled-extensions 2>/dev/null || echo '[]')"
  case "$cur" in
    *"'$1'"*) return 0 ;;
  esac
  case "$cur" in
    ''|'[]'|'@as []') nv="['$1']" ;;
    *) nv="${cur%]}, '$1']" ;;
  esac
  gsettings set org.gnome.shell enabled-extensions "$nv" 2>/dev/null || true
}

disable_gnome_extension() { # <uuid>
  command -v gsettings >/dev/null 2>&1 || return 0
  cur="$(gsettings get org.gnome.shell enabled-extensions 2>/dev/null || echo '[]')"
  nv="$(printf '%s' "$cur" | sed "s/, *'$1'//; s/'$1', *//; s/'$1'//")"
  case "$nv" in
    *'['*']'*) gsettings set org.gnome.shell enabled-extensions "$nv" 2>/dev/null || true ;;
  esac
}

bridge_gnome() {
  src="$APP_DIR/bridge/gnome/$GNOME_UUID"
  dst="$HOME_DIR/.local/share/gnome-shell/extensions/$GNOME_UUID"
  if [ ! -f "$src/metadata.json" ]; then
    log "note: GNOME focus bridge not bundled; focus will show '--' until installed."
    return 0
  fi
  mkdir -p "$dst"
  cp -f "$src/metadata.json" "$src/extension.js" "$dst/" 2>/dev/null || true
  enable_gnome_extension "$GNOME_UUID"
  log "Installed GNOME focus bridge -> $dst"
  log "  GNOME loads new extensions only at login: log out and back in to activate it."
}

bridge_kde() {
  src="$APP_DIR/bridge/kwin/$KDE_ID"
  dst="$HOME_DIR/.local/share/kwin/scripts/$KDE_ID"
  if [ ! -f "$src/metadata.json" ]; then
    log "note: KWin focus bridge not bundled."
    return 0
  fi
  rm -rf "$dst"
  mkdir -p "$dst"
  cp -r "$src/." "$dst/" 2>/dev/null || true
  if command -v kpackagetool6 >/dev/null 2>&1; then
    kpackagetool6 --type=KWin/Script -i "$dst" >/dev/null 2>&1 || true
  elif command -v kpackagetool5 >/dev/null 2>&1; then
    kpackagetool5 --type=KWin/Script -i "$dst" >/dev/null 2>&1 || true
  fi
  if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file kwinrc --group Plugins --key "${KDE_ID}Enabled" true
  elif command -v kwriteconfig5 >/dev/null 2>&1; then
    kwriteconfig5 --file kwinrc --group Plugins --key "${KDE_ID}Enabled" true
  fi
  if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1 || true
  elif command -v qdbus >/dev/null 2>&1; then
    qdbus org.kde.KWin /KWin reconfigure >/dev/null 2>&1 || true
  fi
  log "Installed KDE focus bridge."
}

install_focus_bridge() {
  [ "$bridge" -eq 1 ] || return 0
  case "$(desktop_id)" in
    *gnome*) bridge_gnome ;;
    *kde*|*plasma*) bridge_kde ;;
    *sway*)   log "note: focus detection needs 'swaymsg'; install it if you don't have it." ;;
    *niri*)   log "note: focus detection needs 'niri msg'; install it if you don't have it." ;;
    *hyprland*) log "note: focus detection needs 'hyprctl'; install it if you don't have it." ;;
    *xfce*|*x11*|*mate*|*cinnamon*) log "note: on X11, focus detection needs 'xdotool'." ;;
    *) log "note: focus detection on X11 needs 'xdotool'; GNOME/KDE need the desktop bridge." ;;
  esac
}

remove_focus_bridge() {
  if [ -d "$HOME_DIR/.local/share/gnome-shell/extensions/$GNOME_UUID" ]; then
    disable_gnome_extension "$GNOME_UUID"
    rm -rf "$HOME_DIR/.local/share/gnome-shell/extensions/$GNOME_UUID"
  fi
  rm -rf "$HOME_DIR/.local/share/kwin/scripts/$KDE_ID"
}

do_uninstall() {
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now syspeek.service 2>/dev/null || true
  fi
  remove_focus_bridge
  rm -f "$UNIT_FILE" "$AUTOSTART_FILE" "$WRAPPER"
  [ -n "${APP_DIR:-}" ] && [ -d "$APP_DIR" ] && rm -rf "$APP_DIR"
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user daemon-reload 2>/dev/null || true
  fi
  log "Syspeek uninstalled."
}

linux_install() {
  command -v tar >/dev/null 2>&1 || die "tar is required"

  arch="$(uname -m 2>/dev/null || echo unknown)"
  [ "$arch" = "x86_64" ] || log "warning: this bundle is built for x86_64; you are on $arch"

  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Syspeek..."
  fetch "$BUNDLE_URL" "$tmp/bundle.tar.gz" \
    || die "download failed ($BUNDLE_URL). If there is no release yet, build from source (see README)."

  rm -rf "$APP_DIR"
  mkdir -p "$APP_DIR"
  tar xzf "$tmp/bundle.tar.gz" -C "$APP_DIR"
  chmod +x "$APP_DIR/run.sh" "$APP_DIR/syspeek" 2>/dev/null || true
  if [ -f "$APP_DIR/VERSION" ]; then
    log "Version: $(cat "$APP_DIR/VERSION")"
  else
    log "Version: unknown (bundle predates M58 VERSION stamping; fine)"
  fi

  write_wrapper
  install_autostart
  install_focus_bridge

  log ""
  log "Installed to $APP_DIR"
  cat <<'EOF' >&2
Control it:
  syspeek run         run in the foreground
  syspeek stop        stop it now
  syspeek disable     stop it and don't start at login
  syspeek enable      start it and start at login
  syspeek status      is it running?
  syspeek update      upgrade to the latest version
  syspeek uninstall   remove everything
EOF

  case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) log ""; log "note: add $BIN_DIR to your PATH to use the 'syspeek' command." ;;
  esac
}

# Library mode (M58a tests): source install.sh with SYSPEEK_LIB=1 to reuse
# fetch()/run_priv() without executing the main dispatch.
if [ "${SYSPEEK_LIB:-}" = "1" ]; then
  return 0 2>/dev/null || exit 0
fi

kernel_sys="$(uname -s)"
case "$kernel_sys" in
  Linux)                os_flow="linux" ;;
  Darwin)               os_flow="mac" ;;
  MINGW*|MSYS*|CYGWIN*) os_flow="win" ;;
  *) die "unsupported OS: $kernel_sys (Linux / macOS / Windows only)" ;;
esac

if [ "$mode" = "uninstall" ]; then
  case "$os_flow" in
    mac) mac_uninstall_standalone ;;
    win) win_uninstall_standalone ;;
    *)   do_uninstall ;;
  esac
  exit 0
fi

case "$os_flow" in
  mac) mac_install ;;
  win) win_install ;;
  *)   linux_install ;;
esac
