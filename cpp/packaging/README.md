# Crowsnest — build, install, and desktop integration

This directory contains the C++20 / Qt 6 implementation of Crowsnest and
its packaging.

## Build prerequisites

- A C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.16+
- Qt 6 Widgets and Qt 6 DBus development packages
- Linux `/proc` (standard)

Package names (typical):

| Distribution | Packages |
| --- | --- |
| Debian / Ubuntu / Mint | `build-essential cmake qt6-base-dev libqt6dbus6` |
| Fedora | `gcc-c++ cmake qt6-qtbase-devel qt6-qtbase` (DBus via `qt6-qtbase`) |
| Arch / CachyOS | `base-devel cmake qt6-base` |

X11 focus detection additionally needs `xdotool` (X11/XWayland sessions only).
Wayland sessions use compositor integrations (below) instead.

## Build

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
```

## Install / uninstall

```bash
bash cpp/install.sh            # systemd user service if available
bash cpp/install.sh --no-systemd   # XDG autostart only
bash cpp/install.sh --uninstall
```

- Installs the binary to `~/.local/bin/crowsnest` (`XDG_BINDIR` overrides).
- With systemd: writes `~/.config/systemd/user/crowsnest.service` and enables it.
- Without systemd: writes `~/.config/autostart/crowsnest.desktop`.

## Focus detection per desktop

Crowsnest is PID-centric; each desktop supplies the focused PID through its
own channel:

| Session | Mechanism |
| --- | --- |
| GNOME (Wayland/X11) | GNOME Shell extension (`gnome_extension/crowsnest-focus@codeafridi`) |
| KDE Plasma (Wayland) | KWin script + D-Bus (`cpp/kwin_script`, `cpp/install_kde_focus.sh`) |
| Hyprland | `hyprctl` |
| Sway | `swaymsg` |
| X11 / XWayland (Cinnamon, XFCE, MATE, KDE-X11) | `xdotool` |
| Other Wayland compositors | not supported (shows `no-focus`) |

There is no generic Wayland way to obtain the focused PID (the foreign-toplevel
protocols expose only `app_id`/`title`), so compositor-specific adapters are
required.

## Configuration

Optional INI file at `~/.config/crowsnest/config.ini`
(`CROWSNEST_CONFIG` overrides the path). See the project README / feature
report for the full key list. Missing file or invalid values fall back to
defaults.

## Distribution packages

Each format has a submittable source in this tree; the release workflow
(`.github/workflows/release.yml`) builds and tests on every `v*` tag and
uploads the `.deb` + checksums to the GitHub Release.

| Format | Sources | Publish |
| --- | --- | --- |
| AUR source (`crowsnest`) | `arch/PKGBUILD` | `bash cpp/packaging/arch/publish-aur.sh crowsnest` (needs AUR SSH access; refreshes sums + `.SRCINFO` for you) |
| AUR binary (`crowsnest-bin`) | `arch/crowsnest-bin/PKGBUILD` | `bash cpp/packaging/arch/publish-aur.sh crowsnest-bin` (repacks the release portable bundle) |
| Fedora/RHEL COPR | `rpm/crowsnest.spec` + `.copr/Makefile` | COPR project → Build source "Custom" → spec file `crowsnest.spec` |
| openSUSE OBS | `rpm/crowsnest.spec` + `obs/_service` | branch into your OBS project; service builds the tarball from the `v*` tag |
| Ubuntu PPA | `debian/` + `build_ppa.sh` | `bash cpp/packaging/build_ppa.sh ppa:<you>/crowsnest` (needs `devscripts` + GPG) |
| AppImage (self-updating) | `build_appimage.sh` | `bash cpp/packaging/build_appimage.sh`; embeds `gh-releases-zsync` update info (`UPDATE_INFORMATION` override supported) so AppImageUpdate does delta updates |
| Portable tarball | `build_portable.sh` | `bash cpp/packaging/build_portable.sh` |
| Debian/Ubuntu (`.deb`) | `build_deb.sh` | `bash cpp/packaging/build_deb.sh` |

Direct build commands (no distro tooling):

| Format | Artifact | Build command |
| --- | --- | --- |
| Portable tarball | `cpp/packaging/build_portable.sh` | `bash cpp/packaging/build_portable.sh` |
| AppImage | `cpp/packaging/build_appimage.sh` | `bash cpp/packaging/build_appimage.sh` |
| Debian/Ubuntu (`.deb`) | `cpp/packaging/build_deb.sh` | `bash cpp/packaging/build_deb.sh` |
| Fedora/RHEL (`.rpm`) | `cpp/packaging/rpm/crowsnest.spec` | `rpmbuild -ba cpp/packaging/rpm/crowsnest.spec` |
| Arch (PKGBUILD) | `cpp/packaging/arch/PKGBUILD` | `makepkg -si` |

`build_appimage.sh` reuses the portable bundle, so it also needs Docker unless a
bundle already exists in `dist/`; it downloads `appimagetool` on first run.

The `.deb` installs the binary to `/usr/bin/crowsnest`, a system-wide user
unit to `/usr/lib/systemd/user/crowsnest.service`, a launcher `.desktop`, and
docs. Per-user autostart is then a normal user action:

```bash
systemctl --user enable --now crowsnest.service   # systemd
# or add ~/.config/autostart/crowsnest.desktop     (non-systemd / desktop)
```

> What still needs your accounts (can't be done from a shell here): first
> push to AUR (`crowsnest`, `crowsnest-bin`), creating the COPR project, linking
> the OBS package, uploading the PPA key + recipe, and attaching the
> portable/AppImage builds to the GitHub Release. Every command for those
> steps is in the table above.

