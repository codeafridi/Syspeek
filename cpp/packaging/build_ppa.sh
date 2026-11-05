#!/usr/bin/env bash
#
# build_ppa.sh — build a signed Debian source package for PPA upload.
#
# Usage:
#   bash cpp/packaging/build_ppa.sh [<ppa: name/version, e.g. ppa:you/crowsnest>]
#
# Without an argument it stops after `debuild -S -sa` and prints the dput
# command. Requires: devscripts, debhelper, and a GPG key for signing.
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/../.." && pwd)"
ppa="${1:-}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM

name=crowsnest
version="$(grep -m1 -oP '^pkgver=\K.*' "$here/arch/PKGBUILD" 2>/dev/null || echo 0.6.0)"
work="$tmp/$name-$version"
git -C "$repo_root" archive --format tar --prefix "$name-$version/" HEAD | tar -x -C "$tmp"
cp -r "$work/cpp/packaging/debian" "$work/debian"

echo ">> building source package in $work"
(
  cd "$work"
  debuild -S -sa
)

echo ">> done: $tmp/${name}_${version}-1_source.changes"
if [ -n "$ppa" ]; then
  dput "$ppa" "$tmp/${name}_${version}-1_source.changes"
else
  echo ">> upload with: dput ppa:<you>/crowsnest $tmp/${name}_${version}-1_source.changes"
  echo ">> (leaving $tmp in place for inspection)"
  trap - EXIT INT TERM
fi
