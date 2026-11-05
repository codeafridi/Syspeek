#!/usr/bin/env bash
#
# publish-aur.sh — push one of the AUR packages (crowsnest / crowsnest-bin).
#
# Usage: bash cpp/packaging/arch/publish-aur.sh <crowsnest|crowsnest-bin>
#
# Requires once: an AUR account, an SSH key registered on aur.archlinux.org,
# and `base-devel` (for updpkgsums/makepkg --printsrcinfo).
#
set -euo pipefail

pkg="${1:-}"
case "$pkg" in
  crowsnest|crowsnest-bin) ;;
  *) echo "usage: $0 <crowsnest|crowsnest-bin>" >&2; exit 2 ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$pkg" = "crowsnest" ]; then
  src="$here/PKGBUILD"
else
  src="$here/crowsnest-bin/PKGBUILD"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM

echo ">> cloning aur/$pkg"
git clone "ssh://aur@aur.archlinux.org/$pkg.git" "$tmp/$pkg"
cp "$src" "$tmp/$pkg/PKGBUILD"
cd "$tmp/$pkg"
echo ">> refreshing checksums"
updpkgsums
echo ">> verifying build files"
makepkg --printsrcinfo > .SRCINFO
echo ">> review the diff, then:"
echo "   cd $tmp/$pkg && git add PKGBUILD .SRCINFO && git commit -m 'update' && git push"
echo "   (left the clone in place for you to inspect)"
trap - EXIT INT TERM
