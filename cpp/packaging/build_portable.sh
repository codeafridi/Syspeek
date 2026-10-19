#!/usr/bin/env bash
#
# build_portable.sh — build a self-contained (bundled-Qt) portable tarball.
#
# Produces dist/syspeek-portable.tar.gz, which runs on any glibc-based
# Linux with glibc >= 2.34 WITHOUT Qt installed (Qt 6, ICU and the needed
# system libraries are bundled). Verification matrix is in the README.
#
# The build runs inside a rockylinux:9 container (the oldest mainstream base
# that still packages Qt 6, giving the widest glibc floor). Requires Docker.
#
# Usage:
#   bash cpp/packaging/build_portable.sh [output_dir]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/../.." && pwd)"
out_dir="${1:-$repo_root/dist}"
mkdir -p "$out_dir"
tarball="$out_dir/syspeek-portable.tar.gz"

if ! command -v docker >/dev/null 2>&1; then
  echo "error: docker is required to build the portable bundle" >&2
  exit 1
fi

echo ">> building portable bundle (${BASE_IMAGE:-rockylinux:9} base, Qt 6 from EPEL)" >&2
echo ">> output: $tarball" >&2

# Everything except the final tar stream goes to stderr so stdout stays clean.
docker run --rm -v "$repo_root":/src:ro "${BASE_IMAGE:-rockylinux:9}" bash -lc '
set -euo pipefail
{
  echo ">> installing build dependencies"
  dnf -y install dnf-plugins-core epel-release >/dev/null 2>&1
  dnf config-manager --set-enabled crb >/dev/null 2>&1
  dnf -y install gcc-c++ cmake qt6-qtbase-devel \
    mesa-libGL-devel mesa-libEGL-devel glib2-devel libxkbcommon-devel \
    dbus-devel fontconfig-devel freetype-devel libX11-devel libxcb-devel \
    libicu-devel libglvnd-glx libglvnd-egl libglvnd-opengl >/dev/null 2>&1

  echo ">> building"
  rm -rf /w
  cmake -S /src/cpp -B /w -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build /w -j"$(nproc)" >/dev/null

  echo ">> bundling Qt and dependencies"
  rm -rf /outapp && mkdir -p /outapp/lib /outapp/plugins/platforms
  cp /w/syspeek /outapp/
  # M58a: stamp the bundle version (install.sh logs it; `syspeek vers`
  # prints it) straight from the CMake project version.
  version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' /w/CMakeCache.txt 2>/dev/null || true)"
  [ -n "$version" ] || version="unknown"
  printf '%s\n' "$version" > /outapp/VERSION
  QTL="$(dirname "$(ldd /w/syspeek | awk "/libQt6Core/{print \$3}")")"
  cp -aL "$QTL"/libQt6*.so* /outapp/lib/
  for p in "$(dirname "$QTL")"/../plugins/platforms /usr/lib64/qt6/plugins/platforms; do
    [ -d "$p" ] && cp -a "$p"/libqoffscreen.so "$p"/libqxcb.so /outapp/plugins/platforms/ 2>/dev/null || true
  done

  # Bundle every non-core shared library (dereferencing symlinks) until closure.
  core="libc\.so|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|libstdc\+\+|ld-linux|libresolv|libnsl"
  for _ in 1 2 3 4 5; do
    for t in /outapp/syspeek /outapp/lib/*.so* /outapp/plugins/platforms/*.so; do
      ldd "$t" 2>/dev/null | awk "{print \$3}"
    done | grep "^/" | sort -u | grep -vE "$core" | while read -r l; do
      b="$(basename "$l")"; [ -e "/outapp/lib/$b" ] || cp -L "$l" "/outapp/lib/$b" 2>/dev/null || true
    done
  done

  printf "%s\n" \
    "#!/bin/sh" \
    "HERE=\$(CDPATH= cd -- \"\$(dirname -- \"\$0\")\" && pwd -P)" \
    "export LD_LIBRARY_PATH=\"\$HERE/lib:\$LD_LIBRARY_PATH\"" \
    "export QT_PLUGIN_PATH=\"\$HERE/plugins\"" \
    "exec \"\$HERE/syspeek\" \"\$@\"" > /outapp/run.sh
  chmod +x /outapp/run.sh
  cp /src/README.md /outapp/README.md 2>/dev/null || true

  # Desktop focus bridges so install.sh can set them up automatically.
  mkdir -p /outapp/bridge/gnome /outapp/bridge/kwin
  cp -r /src/gnome_extension/syspeek-focus@codeafridi /outapp/bridge/gnome/ 2>/dev/null || true
  cp -r /src/cpp/kwin_script/syspeek-focus /outapp/bridge/kwin/ 2>/dev/null || true

  echo ">> package ready ($(du -sh /outapp | cut -f1))"
} >&2

tar czf - -C /outapp .
' > "$tarball"

echo ">> done: $(du -h "$tarball" | cut -f1) -> $tarball" >&2
echo ">> install:  mkdir -p ~/.local/opt/syspeek && tar xzf $tarball -C ~/.local/opt/syspeek && ~/.local/opt/syspeek/run.sh" >&2
