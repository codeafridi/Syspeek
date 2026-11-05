#!/bin/sh
# Build the Crowsnest .app bundle and a drag-install DMG on macOS.
#
# Usage: CROWSNEST_BUILD_DIR=cpp/build bash cpp/packaging/build_macos.sh
#   CROWSNEST_BUILD_DIR  (default cpp/build)  cmake build dir to (re)configure
#   CROWSNEST_ARCHS      (default "arm64;x86_64") CMAKE_OSX_ARCHITECTURES
#   CROWSNEST_DMG_DIR    (default $CROWSNEST_BUILD_DIR/packaging)
#   CROWSNEST_SKIP_DEPLOY=1 skips macdeployqt (portable dev-tree run only)
#
# Bundle contract (cpp/packaging/Info.plist.in): APPL, dev.crowsnest.hud,
# LSUIElement=1, LSMinimumSystemVersion 12.0, executable name "crowsnest".

set -eu
cd "$(dirname "$0")/../.."

BUILD_DIR="${CROWSNEST_BUILD_DIR:-cpp/build}"
ARCHS="${CROWSNEST_ARCHS:-arm64;x86_64}"
OUT_DIR="${CROWSNEST_DMG_DIR:-${BUILD_DIR}/packaging}"

VERSION="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\)[[:space:]]*$/\1/p' cpp/CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "cannot parse project version from cpp/CMakeLists.txt" >&2; exit 1; }

APP="${BUILD_DIR}/crowsnest.app"

cmake -S cpp -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

[ -d "$APP" ] || { echo "missing bundle: $APP" >&2; exit 1; }
plutil -lint "${APP}/Contents/Info.plist"

if [ "${CROWSNEST_SKIP_DEPLOY:-0}" != "1" ]; then
    MDQT="$(command -v macdeployqt || true)"
    if [ -z "$MDQT" ] && [ -n "${QT_ROOT_DIR:-}" ]; then
        MDQT="$(find "$QT_ROOT_DIR" -name macdeployqt -type f 2>/dev/null | head -1 || true)"
    fi
    [ -n "$MDQT" ] || { echo "macdeployqt not found (PATH/QT_ROOT_DIR)" >&2; exit 1; }
    "$MDQT" "$APP" -always-overwrite
fi

STAGE="$(mktemp -d "${RUNNER_TEMP:-/tmp}/crowsnest-dmg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/Crowsnest.app"
ln -s /Applications "$STAGE/Applications"

OUT_DIR="${OUT_DIR%/}"
mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/Crowsnest-${VERSION}-macos.dmg"
rm -f "$DMG" "${DMG}.sha256"

# Two-phase create + retry: hdiutil on CI runners can transiently fail
# with "Resource busy" while Spotlight (mds) scans the staging volume.
RW_DMG="$(mktemp "${TMPDIR:-/tmp}/crowsnest-rw.XXXXXX.dmg")"
trap 'rm -rf "$STAGE" "$RW_DMG"' EXIT
attempt=0
while :; do
    attempt=$((attempt + 1))
    if hdiutil create -volname Crowsnest -srcfolder "$STAGE" -ov \
        -format UDRW "$RW_DMG"; then
        break
    fi
    [ "$attempt" -lt 3 ] || { echo "hdiutil create failed" >&2; exit 1; }
    echo "hdiutil create busy, retry $attempt/3..." >&2
    sleep 5
done
attempt=0
while :; do
    attempt=$((attempt + 1))
    if hdiutil convert "$RW_DMG" -format UDZO -o "$DMG"; then
        break
    fi
    [ "$attempt" -lt 3 ] || { echo "hdiutil convert failed" >&2; exit 1; }
    echo "hdiutil convert busy, retry $attempt/3..." >&2
    sleep 5
done
rm -f "$RW_DMG"
shasum -a 256 "$DMG" > "${DMG}.sha256"
echo "built: $DMG"
