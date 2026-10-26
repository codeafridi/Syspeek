<div align="center">

# Syspeek

### A local-first HUD that follows the app you're focused on — and a 27-view Datadog-style console for everything your kernel, containers and apps are doing.

[![Platform: Linux](https://img.shields.io/badge/platform-Linux-informational?style=flat-square&logo=linux&logoColor=white)](#compatibility)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](#install)
[![Qt 6](https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt&logoColor=white)](#install)
[![License: MIT](https://img.shields.io/badge/license-MIT-f4c430?style=flat-square)](LICENSE)

[Install](#install) · [Compatibility](#compatibility) · [Use](#use) · [GNOME / KDE setup](#gnome--kde-setup) · [Docker & Kubernetes](#docker--kubernetes) · [Troubleshooting](#troubleshooting)

<sub>Repository: <strong>Syspeek</strong></sub>

<br />

<img src="docs/hud.png" alt="Syspeek HUD showing the focused app's name, PID, CPU and RSS plus Docker and Kubernetes badges" width="430" />

</div>

---

No more digging through `top`. Syspeek follows the window you're using and shows
that app's CPU and RAM in a small always-on-top panel, plus a full metrics
console behind it. Everything is collected **locally, unprivileged, with no
telemetry** — nothing leaves your machine unless you point a scraper at it.

**Highlights**

- **Per-app focus tracking** — CPU, RSS, threads, open FDs, context switches,
  runtime/instrumentation detection (what language it's written in).
- **TCP diagnostics (Linux)** — per-connection `rtt`/`cwnd`/retransmit counters
  via netlink, kernel receive-buffer drops, active-loss markers, latency chart.
- **Kernel & limits (Linux)** — FD-table pressure, somaxconn, congestion
  control, inotify/pid limits, scheduler run-queue state, CPU steal %.
- **cgroup memory pressure** — peak memory, OOM events/kills, pid limits for
  the focused app's unit.
- **27-page console** — Overview, Processes, Memory, Network, Network Path,
  Containers, Kubernetes, Disks, Host, Latency, Metrics (DogStatsD), Logs,
  Errors, Software, Dependencies, Endpoints, Instrumentation, Synthetic checks,
  Traces (OTLP), Profiles, Databases (PostgreSQL), Security, Settings, Cost,
  Incidents, SLO, Alerts.
- **Alerts & incidents** — local thresholds (CPU, memory, FD-table, steal,
  failed services, journal errors) with desktop notifications and an incident
  timeline.
- **Docker & Kubernetes** — containers, problem pods, node capacity, cost
  model, deprecated-API detection.

## Install

The **same one line works on all three platforms**:

```bash
curl -fsSL https://raw.githubusercontent.com/codeafridi/Syspeek/main/install.sh | sh
# No curl? (Base systems of most distros ship wget; Lubuntu minimal does not ship curl.)
wget -qO- https://raw.githubusercontent.com/codeafridi/Syspeek/main/install.sh | sh
```

The installer handles the rest of its own downloads with a fallback chain:
native `curl` → `wget` → snap `curl` → `busybox wget` → one distro
package-manager rescue (`apt/dnf/yum/zypper/pacman/apk`) → clear manual
instructions. Snap-packaged curl (which cannot write files on some setups) is
only tried after the native tools.

- **Linux (x86-64, glibc ≥ 2.34)** — downloads a self-contained build (Qt 6
  bundled — nothing else to install), puts it in `~/.local/opt/syspeek`, adds a
  `syspeek` command, starts it at login, and installs the desktop focus bridge
  when one is needed (see [Compatibility](#compatibility)).
- **macOS** — downloads the latest DMG, copies `Syspeek.app` into
  `/Applications` (falls back to `~/Applications` without admin rights),
  installs LaunchAgent autostart + the `syspeek` command in
  `/usr/local/bin`, and reports the Gatekeeper steps for the unsigned build.
- **Windows** — PowerShell native bootstrap into `%LOCALAPPDATA%\Syspeek`,
  `syspeek` shim through the WindowsApps PATH directory, HKCU Run
  autostart, then the HUD starts:

  ```powershell
  irm https://raw.githubusercontent.com/codeafridi/Syspeek/main/cpp/packaging/windows/install.ps1 | iex
  ```

  Or from Git Bash with the same `install.sh` as macOS/Linux. Or run
  the MSI from the [releases](../../releases) page instead.

Control it (Linux verbs below; macOS uses the launchctl verb set, Windows
autostart/status are `--autostart=on|off|status` — run the installer with
`-h` to print the per-OS cheat sheet):

| Command | What it does |
| --- | --- |
| `syspeek run` | run in the foreground |
| `syspeek stop` | stop it now |
| `syspeek disable` | stop it and don't start at login |
| `syspeek enable` | start it and start at login |
| `syspeek status` | is it running? |
| `syspeek update` | upgrade to the latest version |
| `syspeek uninstall` | remove everything |

<details>
<summary><strong>Build from source instead</strong></summary>

Install your distro's Qt 6, then build:

| Distribution | Command |
| --- | --- |
| Ubuntu / Debian / Mint / Kali | `sudo apt install build-essential cmake qt6-base-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake qt6-qtbase-devel` |
| RHEL 9 / CentOS Stream 9 / Rocky / Alma | `sudo dnf install epel-release && sudo dnf config-manager --set-enabled crb && sudo dnf install gcc-c++ cmake qt6-qtbase-devel` |
| Arch / CachyOS / Manjaro | `sudo pacman -S --needed base-devel cmake qt6-base` |
| openSUSE Tumbleweed | `sudo zypper install gcc-c++ cmake qt6-base-devel` |
| Alpine | `sudo apk add build-base cmake qt6-qtbase-dev` |

> **Gotchas:** on older Qt 6.2 (Ubuntu 22.04-era) also add `libgl-dev`; the RHEL 9
> family gets Qt 6 from **EPEL** (command above).

```bash
git clone https://github.com/codeafridi/Syspeek.git
cd Syspeek
cmake -S cpp -B cpp/build
cmake --build cpp/build -j"$(nproc)"
bash cpp/install.sh              # add --no-systemd, or --uninstall to remove
```

</details>

<details>
<summary><strong>Manually download the portable bundle</strong></summary>

The one-liner above uses this; do it by hand if you prefer. Needs Docker only to
build:

```bash
git clone https://github.com/codeafridi/Syspeek.git
cd Syspeek
bash cpp/packaging/build_portable.sh          # writes dist/syspeek-portable.tar.gz
mkdir -p ~/.local/opt/syspeek
tar xzf dist/syspeek-portable.tar.gz -C ~/.local/opt/syspeek
~/.local/opt/syspeek/run.sh             # run it
```

</details>

<details>
<summary><strong>Install as an AppImage</strong></summary>

One file, no install step. Runs on any glibc ≥ 2.34 x86-64 distro, Qt bundled.
It self-updates via delta (a `.zsync` file ships alongside on the release page —
use [AppImageUpdate](https://github.com/AppImageCommunity/AppImageUpdate)):

```bash
# Needs curl (or wget + manual URL editing). Resolves the current versioned name:
tag=$(curl -fsSI -o /dev/null -w '%{url_effective}' https://github.com/codeafridi/Syspeek/releases/latest | sed 's#.*/tag/##')
curl -fLO "https://github.com/codeafridi/Syspeek/releases/download/${tag}/Syspeek-${tag#v}-x86_64.AppImage"
chmod +x Syspeek-*-x86_64.AppImage
./Syspeek-*-x86_64.AppImage
```

Asset names carry the version, so there is no stable `releases/latest/download`
URL for the AppImage — grab it from the
[release page](https://github.com/codeafridi/Syspeek/releases/latest) if you
don't want the short script above. The one-liner installer is the better route:
same bundle, plus the `syspeek` command and start-at-login.

The AppImage has no `syspeek` command and no start-at-login; it just runs. Use
the one-liner above if you want those.

</details>

<details>
<summary><strong>Install the .deb (Debian / Ubuntu / Mint / Kali)</strong></summary>

Native package, verified install/remove on Ubuntu 26.04. Grab the `.deb` plus
`SHA256SUMS` from the
[latest release](https://github.com/codeafridi/Syspeek/releases/latest),
verify, then install (`apt` resolves the Qt dependencies automatically):

```bash
sha256sum -c SHA256SUMS --ignore-missing
sudo apt install ./syspeek_0.6.0_amd64.deb
```

This installs `/usr/bin/syspeek`, a user unit
(`/usr/lib/systemd/user/syspeek.service`), a launcher, and docs.
Enable start-at-login with `systemctl --user enable --now syspeek.service`.
Remove with `sudo apt remove syspeek`.

</details>

> On **GNOME** or **KDE** you also need a one-time focus bridge — see
> [GNOME / KDE setup](#gnome--kde-setup). Everything else works out of the box.

## Compatibility

Tested on real containers (build + all tests + launch). "Qt 6 pkg" = built against
the distro's own Qt; "Portable" = the bundled one-liner install, with no Qt present.

| Distribution | Qt 6 pkg | Portable | Notes |
| --- | :---: | :---: | --- |
| Ubuntu 26.04 / 24.04 | ✅ | ✅ | |
| Ubuntu 22.04 | ✅ | ✅ | add `libgl-dev` |
| Debian 13 / 12 | ✅ | ✅ | |
| Kali Rolling | ✅ | ✅ | |
| Fedora 42 / 44 | ✅ | ✅ | |
| RHEL 9 / Rocky 9 / Alma 9 / CentOS Stream 9 | ✅ | ✅ | Qt 6 via **EPEL** |
| openSUSE Tumbleweed | ✅ | ✅ | |
| openSUSE Leap 15.6 | ➖ | ✅ | no Qt 6 in base repos |
| Arch / CachyOS / Manjaro | ✅ | ✅ | |
| Amazon Linux 2023 | ➖ | ✅ | no Qt 6 in base repos |
| Alpine (musl) | ✅ | ➖ | build with apk (musl) |
| Ubuntu 20.04 / Debian 11 | ➖ | ➖ | glibc too old for the bundle; no Qt 6 pkg |
| RHEL 8 / Rocky 8 / Alma 8 | ➖ | ➖ | no Qt 6 in EPEL 8 |

**Portable bundle floor:** glibc ≥ 2.34 (distros released ~2021 onward) and the
x86-64 architecture. Untested but expected to work: Gentoo, Void, NixOS,
SteamOS/Bazzite (glibc ≥ 2.34).

### macOS (beta, 0.7+)

Same HUD with native collectors (libproc / mach / IOKit / OSLogStore /
launchd / Homebrew). See [docs/macos.md](docs/macos.md) for the DMG
install, Gatekeeper first-run steps, `install_macos.sh` service
management and the supported-feature matrix.

### Windows (beta, 0.8+)

Same HUD with native collectors (GetSystemTimes / Toolhelp32 /
NtQuerySystemInformation / PDH / SCM / EventLog / registry). See
[docs/windows.md](docs/windows.md) for the MSI install, the portable
zip, SmartScreen first-run steps, autostart and the supported-feature
matrix.

## Use

- **Drag** the panel anywhere (grab it, including the buttons).
- **`MORE` / `LESS`** — open/close the diagnosis panel.
- **`HIDE` / `SHOW`** — collapse to a single line or expand.
- **Click `docker:` or `k8s:`** — expand that badge's details.
- **`Ctrl+C`** in the terminal — quit.

## GNOME / KDE setup

Focus detection works on GNOME, KDE, Sway, niri, and X11. On **GNOME** and **KDE**
the focused app is shared through a small bridge — the installer sets it up
automatically when it detects your desktop, then you just **log out and back in**
once so the shell loads it.

If you installed by hand instead, add the bridge yourself:

**GNOME** (then log out and back in):

```bash
bash install_gnome_extension.sh
```

**KDE Plasma (Wayland):**

```bash
bash cpp/install_kde_focus.sh
```

On **Sway / niri / Hyprland / X11**, install the matching tool (`swaymsg` /
`niri` / `hyprctl` / `xdotool`) and you're done. Other Wayland compositors run
fine but show `no-focus`. If the app column reads `--`, run
`SYSPEEK_PROBE=1 syspeek` to see which focus provider was detected.

## Docker & Kubernetes

Both are optional. If the tool isn't installed or reachable, the overlay shows a
status instead of failing.

- **Docker** — shows running count and flags unhealthy, restarting, and recently
  exited containers. When the Docker Engine socket is reachable
  (`/var/run/docker.sock`, Docker Desktop's socket on macOS, the named pipe on
  Windows — override with `[docker] socket=`), the Containers page also reads
  per-container depth straight from the Engine API: CPU %, memory usage vs
  limit, network I/O, and process count — the same numbers `docker stats`
  prints, without spawning the CLI. Without the socket the page falls back to
  the plain `docker ps` fields.
- **Kubernetes** — flags problem pods (`CrashLoopBackOff`, image-pull failures,
  `OOMKilled`) and deployments with too few ready replicas, using your current
  `kubectl` context.

## Advanced (optional)

Thresholds and polling intervals are configurable in
`~/.config/syspeek/config.ini`; sensible defaults are used if it's absent.

**Prometheus endpoint** — Syspeek exposes its live snapshot at
`http://127.0.0.1:9107/metrics` (localhost only, on by default) in the
Prometheus text exposition format — point your scraper or Grafana agent config
at it (nothing leaves your machine unless you do). Disable with
`[metrics] enabled=false` or move it with `port=`.

**Alert webhooks** — off by default. Point `[webhook]` at your own endpoint to
POST fired/resolved alert events out of the HUD (nothing is sent until you set
`enabled=true` and a `url`):

```ini
[webhook]
enabled = true
url = https://alerts.example.com/syspeek
format = generic        ; generic | slack
timeout_ms = 5000
```

`format=generic` sends the Prometheus Alertmanager webhook **v4** schema
(`version`, `groupKey`, `truncatedAlerts`, `status`, `alerts[]` with
`labels`/`annotations`/`startsAt`/`endsAt`/`fingerprint`), so any receiver that
understands Alertmanager webhooks works unchanged. `format=slack` sends a Slack
incoming-webhook message (`text` + `attachments[]` with `color`, `title`,
`fields`, `mrkdwn_in`). One POST per tick per group (firing batch, resolved
batch); alerts that clear between ticks produce a `resolved` event. The Settings
view shows the configured target and the last delivery result.

Handy environment variables:

- `SYSPEEK_PROBE=1 ./syspeek` — print the detected focus provider and
  focused PID, then exit.
- `SYSPEEK_LOG=/tmp/overlay.log` — log status to a file.

## Troubleshooting

**The app column shows `--` / `no-focus`.** Focus detection isn't available yet.

1. Run the probe to see which provider was chosen:

   ```bash
   SYSPEEK_PROBE=1 ./cpp/build/syspeek
   ```

2. Set up the bridge for your desktop (see
   [GNOME / KDE setup](#gnome--kde-setup)) and, on GNOME, **log out and back in**.

**It builds but won't start.** Check the platform plugin:

```bash
QT_DEBUG_PLUGINS=1 ./cpp/build/syspeek 2>&1 | tail
```

## License

[MIT](LICENSE).
