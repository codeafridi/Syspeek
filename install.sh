#!/bin/sh
# Crowsnest installer (Linux x86-64, macOS, Windows).
#
# One-line install — the SAME command on every OS:
#   curl -fsSL https://raw.githubusercontent.com/codeafridi/Crowsnest/main/install.sh | sh
#   (or, when curl is absent:  wget -qO- ...   — see README)
# Everything the installer downloads after bootstrap goes through a
# fallback ladder: native curl -> wget -> snap curl -> busybox wget ->
# distro package-manager rescue -> manual instructions.
#
# Linux x86-64 (glibc >= 2.34): downloads a self-contained build (Qt 6 bundled
# — nothing else to install) into ~/.local/opt/crowsnest, adds a `crowsnest`
# command, starts it at login, and installs the desktop focus bridge
# (GNOME extension / KWin script) when one is needed.
#
# macOS: downloads the latest DMG, mounts it, copies Crowsnest.app into
# /Applications (falls back to ~/Applications without admin rights),
# installs the LaunchAgent autostart and the `crowsnest` command in
# /usr/local/bin, and clears the quarantine attribute so the unsigned
# app starts like any other.
#
# Windows (run inside Git Bash / MSYS2): downloads the portable zip into
# %LOCALAPPDATA%\Crowsnest, adds a `crowsnest` command through the WindowsApps
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
#   CROWSNEST_BUNDLE_URL  override the Linux portable-bundle URL
#   CROWSNEST_DMG_URL     override the macOS DMG URL
#   CROWSNEST_WIN_URL     override the Windows portable-zip URL
#   CROWSNEST_TEST_TAG    pin the resolved release tag (packaging CI only)
#
set -eu

REPO="codeafridi/Crowsnest"
RAW="https://raw.githubusercontent.com/${REPO}/main"
BUNDLE_URL="${CROWSNEST_BUNDLE_URL:-https://github.com/${REPO}/releases/latest/download/crowsnest-portable.tar.gz}"

HOME_DIR="${HOME:-/root}"
APP_DIR="${CROWSNEST_DIR:-$HOME_DIR/.local/opt/crowsnest}"
BIN_DIR="${XDG_BINDIR:-$HOME_DIR/.local/bin}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME_DIR/.config}"
UNIT_DIR="$CONFIG_HOME/systemd/user"
UNIT_FILE="$UNIT_DIR/crowsnest.service"
AUTOSTART_DIR="$CONFIG_HOME/autostart"
AUTOSTART_FILE="$AUTOSTART_DIR/crowsnest.desktop"
WRAPPER="$BIN_DIR/crowsnest"

GNOME_UUID="crowsnest-focus@codeafridi"
KDE_ID="crowsnest-focus"

MAC_LABEL="dev.crowsnest.hud"
MAC_PLIST="${HOME_DIR}/Library/LaunchAgents/${MAC_LABEL}.plist"
MAC_APP_DEFAULT="/Applications/Crowsnest.app"
MAC_CLI="/usr/local/bin/crowsnest"

# The previous brand. Removed on install and uninstall so an upgrade cannot
# leave two autostarts or a stale focus extension writing the old pid file.
LEGACY_GNOME_UUID="syspeek-focus@codeafridi"
LEGACY_KDE_ID="syspeek-focus"
LEGACY_MAC_LABEL="dev.syspeek.hud"
LEGACY_MAC_PLIST="${HOME_DIR}/Library/LaunchAgents/${LEGACY_MAC_LABEL}.plist"
LEGACY_MAC_CLI="/usr/local/bin/syspeek"

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

# Where downloads land. This must be REAL DISK: /tmp is a tmpfs on most
# desktops, and a 40 MB bundle written into a tmpfs that is nearly full dies
# with ENOSPC halfway through - which is exactly the failure a user reported as
# "could not download". A snap-confined curl additionally gets a private /tmp
# the main mount does not share.
work_dir() { # -> prints a writable directory with room for the bundle
  need_kb=131072   # the bundle plus the extracted tree, with slack
  for base in "${CROWSNEST_TMPDIR:-}" "${XDG_CACHE_HOME:-$HOME/.cache}" "$HOME" "${TMPDIR:-}" /tmp; do
    [ -n "$base" ] || continue
    dir="$base/crowsnest-install"
    mkdir -p "$dir" 2>/dev/null || continue
    [ -w "$dir" ] || continue
    avail="$(df -Pk "$dir" 2>/dev/null | awk 'NR==2 { print $4 }')"
    case "$avail" in
      ''|*[!0-9]*) printf '%s\n' "$dir"; return 0 ;;
    esac
    if [ "$avail" -ge "$need_kb" ]; then
      printf '%s\n' "$dir"
      return 0
    fi
  done
  # Nothing had the room we asked for; use the first writable candidate and let
  # the download report the truth instead of guessing here.
  for base in "${CROWSNEST_TMPDIR:-}" "${XDG_CACHE_HOME:-$HOME/.cache}" "$HOME" "${TMPDIR:-}" /tmp; do
    [ -n "$base" ] || continue
    dir="$base/crowsnest-install"
    mkdir -p "$dir" 2>/dev/null && [ -w "$dir" ] && { printf '%s\n' "$dir"; return 0; }
  done
  return 1
}

# A write failure is not a missing downloader. Classify it so the installer
# stops recommending "apt-get install curl" to someone whose disk is full (and
# stops asking for sudo to re-attempt a download that cannot succeed).
write_failure() { # <stderr-file> <dest-dir> — 0 when the cause was the write
  [ -f "$1" ] || return 1
  if grep -qiE 'no space left|disk quota|short write|failure writing|cannot write to|write error' "$1"; then
    dir="${2:-.}"
    avail="$(df -h "$dir" 2>/dev/null | awk 'NR==2 { print $4 " free of " $2 }')"
    die "the download could not be WRITTEN to $dir (${avail:-no space reported})
This is a disk-space problem, not a downloader problem: a Crowsnest bundle needs
about 90 MB free while it unpacks. Free some space, or point the installer at a
larger filesystem:
  export XDG_CACHE_HOME=/path/with/space     # or CROWSNEST_TMPDIR=/that/path
  then re-run this installer."
  fi
  return 1
}

fetch() { # <url> <dest> — downloader ladder (M58a), works without native curl
  # Every attempt records its stderr: the ladder must be able to tell "no
  # downloader" from "the disk is full", because the remedy is different and
  # one of them asks for the user's sudo password for nothing.
  errf="$(dirname "$2")/.crowsnest-dl.stderr"
  { : > "$errf"; } 2>/dev/null || errf=/dev/null
  attempted=""

  if command -v curl >/dev/null 2>&1 && ! under_snap curl; then
    attempted="$attempted curl"
    curl -fsSL --connect-timeout 10 -o "$2" "$1" 2>"$errf" && return 0
    write_failure "$errf" "$(dirname "$2")" && return 1
  fi
  if command -v wget >/dev/null 2>&1 && ! under_snap wget; then
    attempted="$attempted wget"
    wget -q -T 20 -O "$2" "$1" 2>"$errf" && return 0
    write_failure "$errf" "$(dirname "$2")" && return 1
  fi
  if command -v curl >/dev/null 2>&1; then
    log "note: the curl on this machine is a snap (sandbox may block writes)"
    attempted="$attempted snap-curl"
    curl -fsSL --connect-timeout 10 -o "$2" "$1" 2>"$errf" && return 0
    log "snap curl failed - continuing down the ladder"
  fi
  if command -v busybox >/dev/null 2>&1; then
    attempted="$attempted busybox"
    busybox wget -q -T 20 -O "$2" "$1" 2>"$errf" && return 0
    write_failure "$errf" "$(dirname "$2")" && return 1
  fi

  # Only ask for sudo when there is no downloader at all - not when one ran and
  # failed to write.
  case "$attempted" in
    "") log "no downloader found; installing one" ;;
    *)  log "every downloader present failed; not retrying through the package manager
   (see the last error below)" ;;
  esac
  if [ -z "$attempted" ] || ! command -v curl >/dev/null 2>&1; then
    if pkg_install_curl; then
      curl -fsSL --connect-timeout 10 -o "$2" "$1" 2>"$errf" && return 0
      write_failure "$errf" "$(dirname "$2")" && return 1
      if command -v wget >/dev/null 2>&1; then
        wget -q -T 20 -O "$2" "$1" 2>"$errf" && return 0
        write_failure "$errf" "$(dirname "$2")" && return 1
      fi
    fi
  fi

  last="$(head -n 1 "$errf" 2>/dev/null | cut -c1-160)"
  [ -n "$last" ] && last="
last error: $last"
  die "could not download $(basename "$1")${last}
tried:$attempted
A downloader is present (or could not be installed). If the message above is a
network error, check the URL and your connection; if it mentions writes, see
the space advice above. You can also fetch the bundle yourself from
https://github.com/${REPO}/releases"
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
  remove_legacy_brand
  command -v hdiutil >/dev/null 2>&1 || die "hdiutil is required (macOS only)"
  if [ -n "${CROWSNEST_DMG_URL:-}" ]; then
    dmg_url="$CROWSNEST_DMG_URL"
  elif [ -n "${CROWSNEST_TEST_TAG:-}" ]; then
    dmg_url="https://github.com/${REPO}/releases/download/${CROWSNEST_TEST_TAG}/Crowsnest-$(printf '%s' "$CROWSNEST_TEST_TAG" | tr -d v)-macos.dmg"
  else
    tag="$(latest_version_tag)"
    dmg_url="https://github.com/${REPO}/releases/download/${tag}/Crowsnest-$(printf '%s' "$tag" | tr -d v)-macos.dmg"
  fi

  tmp="$(work_dir)" || die "no writable directory for the download"
  mkdir -p "$tmp" 2>/dev/null || true
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Crowsnest for macOS..."
  fetch "$dmg_url" "$tmp/crowsnest.dmg" \
    || die "download failed ($dmg_url). Get the DMG from https://github.com/${REPO}/releases"

  hdiutil attach -nobrowse -readonly -quiet "$tmp/crowsnest.dmg" \
    || die "could not mount the DMG"
  mount="/Volumes/Crowsnest"
  if [ ! -d "$mount/Crowsnest.app" ]; then
    hdiutil detach "$mount" -force -quiet >/dev/null 2>&1 || true
    die "no Crowsnest.app inside the DMG"
  fi

  mac_app="$MAC_APP_DEFAULT"
  res_old="$mac_app/Contents/Resources/crowsnest"
  res_old_home="${HOME_DIR}/Applications/Crowsnest.app/Contents/Resources/crowsnest"
  if [ -x "$res_old" ]; then
    "$res_old" disable >/dev/null 2>&1 || true
  elif [ -x "$res_old_home" ]; then
    "$res_old_home" disable >/dev/null 2>&1 || true
  else
    launchctl bootout "gui/$(id -u)/${MAC_LABEL}" >/dev/null 2>&1 || true
  fi

  if [ -w /Applications ]; then
    rm -rf "$MAC_APP_DEFAULT"
    cp -R "$mount/Crowsnest.app" "$MAC_APP_DEFAULT"
  elif run_priv rm -rf "$MAC_APP_DEFAULT" \
    && run_priv cp -R "$mount/Crowsnest.app" /Applications/; then
    :
  else
    mac_app="${HOME_DIR}/Applications/Crowsnest.app"
    mkdir -p "${HOME_DIR}/Applications"
    rm -rf "$mac_app"
    cp -R "$mount/Crowsnest.app" "$mac_app"
    log "note: no admin rights for /Applications - installed to ~/Applications"
  fi
  hdiutil detach "$mount" -quiet >/dev/null 2>&1 \
    || hdiutil detach "$mount" -force -quiet >/dev/null 2>&1 || true

  # curl never sets the quarantine attribute; clear it defensively anyway
  # so a re-downloaded DMG places a runnable app.
  xattr -dr com.apple.quarantine "$mac_app" >/dev/null 2>&1 || true
  log "Installed bundle: $mac_app"

  res="$mac_app/Contents/Resources/crowsnest"
  mkdir -p "$(dirname "$res")"
  fetch "$RAW/cpp/packaging/install_macos.sh" "$res" \
    || die "could not fetch the macOS verb manager (install_macos.sh)"
  chmod 0755 "$res"

  cat > "$tmp/crowsnest-cli" <<EOF
#!/bin/sh
exec "\${CROWSNEST_APP:-$mac_app}/Contents/Resources/crowsnest" "\$@"
EOF
  chmod 0755 "$tmp/crowsnest-cli"
  mac_cli_dir="$(dirname "$MAC_CLI")"
  mkdir -p "$mac_cli_dir" 2>/dev/null || true
  if [ -w "$mac_cli_dir" ]; then
    cp -f "$tmp/crowsnest-cli" "$MAC_CLI"
  elif run_priv /bin/mkdir -p "$mac_cli_dir" \
    && run_priv /usr/bin/install -m 0755 "$tmp/crowsnest-cli" "$MAC_CLI"; then
    :
  else
    log "note: could not write $MAC_CLI - the verb manager works directly:"
    log "      \"$res\" {start|stop|restart|status|logs|enable|disable|update|uninstall|path}"
  fi

  if [ "$autostart" -eq 1 ]; then
    "$res" enable
  else
    log "not started - run: crowsnest start  (or open Crowsnest.app)"
  fi

  cat <<'EOF' >&2
Control it:
  crowsnest start       load the LaunchAgent and run the HUD
  crowsnest stop        stop the HUD
  crowsnest restart     stop + start
  crowsnest status      is it running?
  crowsnest logs        tail the HUD output
  crowsnest update      upgrade to the latest version (re-runs this installer)
  crowsnest uninstall   remove everything
EOF
}

mac_uninstall_standalone() {
  for res_try in \
    "$MAC_APP_DEFAULT/Contents/Resources/crowsnest" \
    "${HOME_DIR}/Applications/Crowsnest.app/Contents/Resources/crowsnest"; do
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
  log "Crowsnest uninstalled (macOS)."
}

# ------------------------------------------------------------- Windows ----

win_install() {
  command -v powershell.exe >/dev/null 2>&1 \
    || die "this Windows flow needs Git Bash/MSYS2 (powershell.exe on PATH)"
  command -v cygpath >/dev/null 2>&1 \
    || die "cygpath is required (Git Bash)"
  appdirWin="${LOCALAPPDATA:-${USERPROFILE}\\AppData\\Local}\\Crowsnest"
  appdir="$(cygpath -u "$appdirWin" 2>/dev/null \
    || printf '%s' "$appdirWin" | sed 's|\\|/|g')"

  if [ -n "${CROWSNEST_WIN_URL:-}" ]; then
    zip_url="$CROWSNEST_WIN_URL"
  elif [ -n "${CROWSNEST_TEST_TAG:-}" ]; then
    zip_url="https://github.com/${REPO}/releases/download/${CROWSNEST_TEST_TAG}/Crowsnest-$(printf '%s' "$CROWSNEST_TEST_TAG" | tr -d v)-windows-portable.zip"
  else
    tag="$(latest_version_tag)"
    zip_url="https://github.com/${REPO}/releases/download/${tag}/Crowsnest-$(printf '%s' "$tag" | tr -d v)-windows-portable.zip"
  fi

  tmp="$(work_dir)" || die "no writable directory for the download"
  mkdir -p "$tmp" 2>/dev/null || true
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Crowsnest for Windows..."
  fetch "$zip_url" "$tmp/crowsnest.zip" \
    || die "download failed ($zip_url). Get the installer from https://github.com/${REPO}/releases"

  taskkill //F //IM crowsnest.exe >/dev/null 2>&1 || true
  rm -rf "$appdir"
  mkdir -p "$appdir"
  ZIP_WIN="$(cygpath -w "$tmp/crowsnest.zip")" \
  DEST_WIN="$(cygpath -w "$appdir")" \
  MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -Command 'Expand-Archive -Path $env:ZIP_WIN -DestinationPath $env:DEST_WIN -Force' \
    || die "unzip (Expand-Archive) failed"
  [ -f "$appdir/crowsnest.exe" ] || die "portable install incomplete (crowsnest.exe missing)"

  shimDirUnix="$(dirname "$appdir")/Microsoft/WindowsApps"
  shimDirWin="$(cygpath -w "$shimDirUnix")"
  remove_legacy_brand
  mkdir -p "$shimDirUnix" 2>/dev/null || true
  printf '@echo off\r\nif "%%~1"=="" (\r\n  start "" /b "%%LOCALAPPDATA%%\\Crowsnest\\crowsnest.exe"\r\n  exit /b 0\r\n)\r\n"%%LOCALAPPDATA%%\\Crowsnest\\crowsnest.exe" %%*\r\n' \
    > "$shimDirUnix/crowsnest.cmd" 2>/dev/null || true
  printf '#!/bin/sh\nif [ "$#" -eq 0 ]; then\n  cmd.exe //c start "" /b "$LOCALAPPDATA\\Crowsnest\\crowsnest.exe" >/dev/null 2>&1\n  exit 0\nfi\nexec "$LOCALAPPDATA/Crowsnest/crowsnest.exe" "$@"\n' \
    > "$shimDirUnix/crowsnest" 2>/dev/null || true
  chmod 0755 "$shimDirUnix/crowsnest" 2>/dev/null || true
  if [ ! -f "$shimDirUnix/crowsnest.cmd" ] && [ ! -f "$shimDirUnix/crowsnest" ]; then
    log "note: could not write the 'crowsnest' command to $shimDirWin"
    log "      run the HUD directly: $(cygpath -w "$appdir/crowsnest.exe")"
  fi

  if [ "$autostart" -eq 1 ]; then
    "$appdir/crowsnest.exe" --autostart=on
    EXE_WIN="$(cygpath -w "$appdir/crowsnest.exe")" \
    MSYS_NO_PATHCONV=1 powershell.exe -NoProfile -ExecutionPolicy Bypass \
      -Command 'Start-Process -FilePath $env:EXE_WIN' >/dev/null 2>&1 || true
  else
    log "not started - open a NEW shell and type: crowsnest"
  fi

  cat <<'EOF' >&2
Control it:
  crowsnest                  launch the HUD and return to the prompt
  crowsnest --autostart=on|off|status   login autostart (HKCU Run key)
  crowsnest --dump           JSON snapshot of the machine (headless check)
  update                   re-run this installer
  uninstall                remove everything
EOF
  log "note: unsigned build - SmartScreen 'More info / Run anyway' on first GUI launch."
  log "note: 'update' = re-run the install one-liner (the .exe has no self-update verb)."
}

win_uninstall_standalone() {
  appdirWin="${LOCALAPPDATA:-${USERPROFILE}\\AppData\\Local}\\Crowsnest"
  appdir="$(cygpath -u "$appdirWin" 2>/dev/null \
    || printf '%s' "$appdirWin" | sed 's|\\|/|g')"
  "$appdir/crowsnest.exe" --autostart=off 2>/dev/null || true
  taskkill //F //IM crowsnest.exe >/dev/null 2>&1 || true
  rm -rf "$appdir"
  shimDirUnix="$(dirname "$appdir")/Microsoft/WindowsApps"
  rm -f "$shimDirUnix/crowsnest.cmd" "$shimDirUnix/crowsnest" 2>/dev/null || true
  log "Crowsnest uninstalled (Windows)."
}

# --------------------------------------------------------------- Linux ----

write_wrapper() {
  mkdir -p "$BIN_DIR"
  cat > "$WRAPPER" <<EOF
#!/bin/sh
set -eu
APP="\${CROWSNEST_DIR:-\$HOME/.local/opt/crowsnest}"
UNIT="crowsnest.service"
cmd="\${1:-run}"
[ "\$#" -gt 0 ] && shift
case "\$cmd" in
  run|gui)   exec "\$APP/run.sh" "\$@" ;;
  start)     systemctl --user start "\$UNIT" ;;
  stop)      systemctl --user stop "\$UNIT" 2>/dev/null || pkill -f "\$APP/crowsnest" 2>/dev/null || true ;;
  restart)   systemctl --user restart "\$UNIT" ;;
  status)    systemctl --user status "\$UNIT" --no-pager 2>/dev/null || pgrep -af "\$APP/crowsnest" || echo "not running" ; [ -f "\$APP/VERSION" ] && printf 'version: ' && cat "\$APP/VERSION" || true ;;
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
               echo "crowsnest update: no working downloader (wget/curl/busybox)" >&2
             fi
             rm -f "\$t" ;;
  uninstall) t="\$(mktemp)"
             if wget -q -T 20 -O "\$t" ${RAW}/install.sh 2>/dev/null \
                || curl -fsSL --connect-timeout 10 -o "\$t" ${RAW}/install.sh \
                || busybox wget -q -T 20 -O "\$t" ${RAW}/install.sh; then
               sh "\$t" -s -- --uninstall
             else
               echo "crowsnest uninstall: no working downloader (wget/curl/busybox)" >&2
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
Description=Crowsnest HUD
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
      systemctl --user enable --now crowsnest.service
    else
      systemctl --user start crowsnest.service 2>/dev/null || true
    fi
    log "Installed systemd user service: $UNIT_FILE"
  else
    mkdir -p "$AUTOSTART_DIR"
    cat > "$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Crowsnest
Exec=$APP_DIR/run.sh
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    log "Installed XDG autostart entry: $AUTOSTART_FILE"
    if [ "$autostart" -eq 1 ]; then
      ( setsid "$APP_DIR/run.sh" >/dev/null 2>&1 & ) || true
    fi
    log "note: no systemd user session; stop with: pkill -f crowsnest"
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

remove_legacy_brand() {
  # Linux: old systemd unit and XDG autostart would keep launching the old
  # binary (and the old focus bridge would keep writing the old pid file).
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now syspeek.service >/dev/null 2>&1 || true
  fi
  rm -f "$CONFIG_HOME/systemd/user/syspeek.service" \
        "$CONFIG_HOME/autostart/syspeek.desktop" \
        "$BIN_DIR/syspeek" 2>/dev/null || true
  if [ -d "$HOME_DIR/.local/share/gnome-shell/extensions/$LEGACY_GNOME_UUID" ]; then
    disable_gnome_extension "$LEGACY_GNOME_UUID"
    rm -rf "$HOME_DIR/.local/share/gnome-shell/extensions/$LEGACY_GNOME_UUID"
  fi
  rm -rf "$HOME_DIR/.local/share/kwin/scripts/$LEGACY_KDE_ID"

  # macOS: old LaunchAgent and CLI shim.
  launchctl bootout "gui/$(id -u)/${LEGACY_MAC_LABEL}" >/dev/null 2>&1 || true
  rm -f "$LEGACY_MAC_PLIST" 2>/dev/null || true
  if [ -w "$(dirname "$LEGACY_MAC_CLI")" ]; then
    rm -f "$LEGACY_MAC_CLI" 2>/dev/null || true
  fi

  # Windows (Git Bash): old HKCU Run value and shims, and a running old HUD.
  case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*)
      taskkill //F //IM syspeek.exe >/dev/null 2>&1 || true
      reg delete "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run" \
        /v Syspeek /f >/dev/null 2>&1 || true
      if [ -n "${shimDirUnix:-}" ]; then
        rm -f "$shimDirUnix/syspeek.cmd" "$shimDirUnix/syspeek" 2>/dev/null || true
      fi
      ;;
  esac
}

do_uninstall() {
  remove_legacy_brand
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now crowsnest.service 2>/dev/null || true
  fi
  remove_focus_bridge
  rm -f "$UNIT_FILE" "$AUTOSTART_FILE" "$WRAPPER"
  [ -n "${APP_DIR:-}" ] && [ -d "$APP_DIR" ] && rm -rf "$APP_DIR"
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user daemon-reload 2>/dev/null || true
  fi
  log "Crowsnest uninstalled."
}

linux_install() {
  remove_legacy_brand
  command -v tar >/dev/null 2>&1 || die "tar is required"

  arch="$(uname -m 2>/dev/null || echo unknown)"
  [ "$arch" = "x86_64" ] || log "warning: this bundle is built for x86_64; you are on $arch"

  # Real disk, not a tmpfs: see work_dir().
  tmp="$(work_dir)" || die "no writable directory for the download"
  mkdir -p "$tmp" 2>/dev/null || true
  trap 'rm -rf "$tmp"' EXIT INT TERM

  log "Downloading Crowsnest..."
  fetch "$BUNDLE_URL" "$tmp/bundle.tar.gz" \
    || die "download failed ($BUNDLE_URL). If there is no release yet, build from source (see README)."

  rm -rf "$APP_DIR"
  mkdir -p "$APP_DIR"
  tar xzf "$tmp/bundle.tar.gz" -C "$APP_DIR"
  chmod +x "$APP_DIR/run.sh" "$APP_DIR/crowsnest" 2>/dev/null || true
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
  crowsnest run         run in the foreground
  crowsnest stop        stop it now
  crowsnest disable     stop it and don't start at login
  crowsnest enable      start it and start at login
  crowsnest status      is it running?
  crowsnest update      upgrade to the latest version
  crowsnest uninstall   remove everything
EOF

  case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) log ""; log "note: add $BIN_DIR to your PATH to use the 'crowsnest' command." ;;
  esac
}

# Library mode (M58a tests): source install.sh with CROWSNEST_LIB=1 to reuse
# fetch()/run_priv() without executing the main dispatch.
if [ "${CROWSNEST_LIB:-}" = "1" ]; then
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
