#!/usr/bin/env bash
#
# Build a Debian binary package for Syspeek.
# Produces: cpp/build/packaging/syspeek_<version>_<arch>.deb
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN="$BUILD_DIR/syspeek"
# Version: explicit env > nearest git tag > CMake project version.
if [ -z "${VERSION:-}" ]; then
  VERSION="$(git -C "$ROOT_DIR/.." describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
fi
if [ -z "${VERSION:-}" ]; then
  VERSION="$(grep -m1 -oP '^\s*VERSION\s+\K[0-9.]+' "$ROOT_DIR/CMakeLists.txt" 2>/dev/null)"
fi
VERSION="${VERSION:-0.6.0}"
ARCH="$(dpkg --print-architecture)"
OUT="$BUILD_DIR/packaging"
STAGE="$OUT/syspeek_${VERSION}_${ARCH}"

if [ ! -x "$BIN" ]; then
  echo "Build the project first:" >&2
  echo "  cmake -S cpp -B cpp/build && cmake --build cpp/build -j" >&2
  exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/lib/systemd/user" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/doc/syspeek"

install -m 0755 "$BIN" "$STAGE/usr/bin/syspeek"

sed 's|@BINARY@|/usr/bin/syspeek|g' \
  "$ROOT_DIR/packaging/syspeek.service.in" \
  > "$STAGE/usr/lib/systemd/user/syspeek.service"

cat > "$STAGE/usr/share/applications/syspeek.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Syspeek
Comment=Compact on-screen focused-process monitor
Exec=/usr/bin/syspeek
Terminal=false
Categories=Utility;System;Monitor;
EOF

install -m 0644 "$ROOT_DIR/packaging/README.md" \
  "$STAGE/usr/share/doc/syspeek/README.md"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: syspeek
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6, libqt6widgets6, libqt6network6, libqt6dbus6
Recommends: qt6-qpa-plugins
Maintainer: Syspeek <noreply@example.com>
Description: Compact always-on-top HUD for focused-process and infra status
 Shows the focused application's PID, CPU, and RSS, plus optional Docker
 and Kubernetes health, in a small always-on-top overlay.
EOF

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$STAGE" \
  "$OUT/syspeek_${VERSION}_${ARCH}.deb"

echo "Built: $OUT/syspeek_${VERSION}_${ARCH}.deb"
