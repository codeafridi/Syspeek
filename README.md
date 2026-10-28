<div align="center">

# Syspeek

### A local-first HUD that follows the app you're focused on — and a 30-view Datadog-style console for everything your kernel, containers and apps are doing.

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
  failed services, journal errors) shown on the Alerts page with an incident
  timeline. No desktop popups, by design — a HUD that steals focus is worse
  than no HUD; point `[webhook]` at your own receiver if you want to be
  paged.
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

## Sensors & battery

The **Sensors** page reads the hardware counters the kernel already exposes, so
you can see the thing that is actually hot before it throttles:

- **hwmon** (`/sys/class/hwmon` on Linux) — CPU package and per-core
  temperatures, NVMe/board/chipset sensors, fan speeds, power and current
  rails, each with its kernel `crit`/`high` limit. Readings at or past a limit
  are tinted amber/red, the busiest chip is sorted first, and the page charts
  the hottest sensor plus total sensor power.
- **Battery and AC** — charge, state (`charging`/`discharging`/`full`), energy
  now vs full vs design capacity (so health is the ratio), power draw, voltage,
  cycle count, temperature and the kernel's time-to-empty. On macOS the same
  numbers come from `IOPSGetPowerSourceDescription` and the `AppleSmartBattery`
  registry node; on Windows from the battery device class
  (`IOCTL_BATTERY_QUERY_INFORMATION`/`_STATUS`), where the reported temperature
  is converted from tenths of a kelvin.

Two monitors are wired to it: **host temperature** (warning, 85 °C by default)
and **battery low** (warning, below 20 %). Both are editable in
Settings → MONITORS like every other rule, and both clear themselves the
moment the reading goes back to normal.

Deliberately honest on the platforms that gate it: per-sensor temperatures on
macOS need the privileged SMC helper and on Windows a kernel driver, so those
two report the battery (which is a normal, permission-free API) and print the
reason on the thermal side instead of showing an empty card. Battery charge,
energy and AC state never need root anywhere.

Prometheus exports the same numbers as `syspeek_sensor_celsius{chip,sensor,kind}`,
`syspeek_sensor_limit_celsius`, `syspeek_sensor_fan_rpm`,
`syspeek_sensor_power_watts`, `syspeek_battery_charge_percent{battery}`,
`syspeek_battery_energy_watt_hours{battery,state="now|full"}`,
`syspeek_battery_power_watts` and `syspeek_ac_online{adapter,online}`.

## Advanced (optional)

Thresholds and polling intervals are configurable in
`~/.config/syspeek/config.ini`; sensible defaults are used if it's absent.

**Prometheus endpoint** — Syspeek exposes its live snapshot at
`http://127.0.0.1:9107/metrics` (localhost only, on by default) in the
Prometheus text exposition format — point your scraper or Grafana agent config
at it (nothing leaves your machine unless you do). Disable with
`[metrics] enabled=false`, move it with `port=`, or bind it to an interface
with `bind=`:

```ini
[metrics]
enabled = true
port    = 9107
bind    = 127.0.0.1
token   =
```

A non-loopback `bind` (e.g. `0.0.0.0` or a LAN address) is **refused unless
`token` is set** — the server falls back to `127.0.0.1` and the HUD log says
why, so a scrape endpoint never lands on the network unauthenticated by
accident. With a token, scrapes must send `Authorization: Bearer <token>`;
anything else gets `401` + `WWW-Authenticate: Bearer` and no samples. The
comparison is constant-time, and the Settings page states the live endpoint
and whether auth is on. Prometheus scrape config:

```yaml
scrape_configs:
  - job_name: syspeek
    authorization:
      credentials: <token>
    static_configs:
      - targets: ["127.0.0.1:9107"]
```

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

**Process / unit / container actions** — on by default, and refused loudly
rather than silently when a permission or an OS does not allow a verb. Every
verb is available from the page that shows the target: the **Processes** page
sends signals (TERM/KILL/HUP, pause/resume), changes the nice value
(`NICE ±5`, relative like `htop`'s priority editor), sets the block-I/O
priority (`IONICE` class/level) and moves `oom_score_adj`; the **Services**
page restarts the selected unit through the supervisor (`systemctl restart`,
`launchctl kickstart -k`, SCM); the **Containers** page starts/stops/restarts
through the local Docker Engine API. TERM and KILL require a second press on
the same row — no modal dialog, and the status line names the target. Kill the
whole feature off with:

```ini
[actions]
enabled = false
```

Actions never run as root and never elevate: a verb your user cannot perform
comes back with the OS reason (`Operation not permitted`, `systemctl` stderr)
on the page. Nothing is sent anywhere — the verbs act on this machine only.

**Overlay behaviour** — the HUD remembers where you put it and can get out of
your way without getting lost:

```ini
[overlay]
click_through = false   ; every click but the CLICK button goes to the window below
sticky        = true    ; follow you across X11 desktops
x = 200                 ; last position (restored on launch when a screen still holds it)
y = 120
width = 820             ; last expanded size
height = 560
```

The footer **CLICK** button toggles click-through on X11 (X Shape `ShapeInput`),
and keeps its own rectangle live inside the input region — so the mode is never
a one-way trip: press it again to make the whole HUD clickable. While the mode
is on, the HUD cannot be dragged (clicks pass through); drag it with
click-through off, or edit `x`/`y` in the ini. A saved rectangle that lands on a
monitor that no longer exists, or is dragged past the last screen edge, is
pulled back onto the screen it overlaps most; that rescue runs on a 1 s watch,
so a hot-unplugged display can never leave an unreachable HUD. `sticky` asks
the window manager to keep the HUD on every desktop (`_NET_WM_STATE_STICKY`
plus the `_NET_WM_DESKTOP` all-desktops request) and the Settings page states
what the wm actually did. Wayland, macOS and Windows state their gate instead
of pretending: the compositor owns input routing, so click-through is X11-only
in this build and the Settings row says so.

**Metric history export** — the HUD keeps the last 1800 samples
(~30 min at the default 1 s cadence) in memory and can write them out:
Settings → DIAGNOSTICS → **EXPORT CSV** / **EXPORT JSON** (files land in
`~/.cache/dev.syspeek/syspeek/syspeek-history-<time>.<ext>`, the path is shown
in the GENERAL card). For headless capture use the recorder:

```sh
syspeek --record=60                 # 60 samples (1 s cadence) to stdout, CSV
syspeek --record=5m --format=csv --out=history.csv
syspeek --record=120 --format=json --out=history.json
```

`--record=<n>` counts samples, `--record=<n>s|m|h` counts wall time.
`--format=csv|json` (default `csv`), `--out=<path|->` (default stdout).
Only the local machine is read; nothing is uploaded.

The CSV dialect is the shipped sysstat `sadf -d` format — a single
`# hostname;interval;timestamp;...` header line, `;` separated fields, one row
per sample, `YYYY-MM-DD HH:MM:SS UTC` timestamps and `-1` for values that do
not apply (warming ticks, gated collectors) — so it loads straight into the
same spreadsheet/analysis tooling you already point at `sar` output:

```
# hostname;interval;timestamp;APP;app_pid;app_cpu_percent;app_rss_kb;host_cpu_percent;...
box;1;2026-10-26 12:59:07 UTC;cursor;36904;179.52;3391572;16.95;0.00;6014808;...
```

The JSON export nests the same way `sadf -j` does (`samples[].timestamp.date/
time/tz`, then `app`, `host`, `disk`, `network`, `tcp` objects) but stays inside
a `{"syspeek": {...}}` envelope with `version`, `platform`, `hostname`,
`intervalMs`; values a collector could not produce are `null`.

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
