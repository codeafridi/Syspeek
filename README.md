<div align="center">

# Syspeek

### A local-first HUD that follows the app you're focused on — and a 31-view Datadog-style console for everything your kernel, containers and apps are doing.

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

The tree compiles on the oldest Qt 6 the distro channel ships (6.2 on Ubuntu
22.04, 6.4 on 24.04), not just on the newest. That floor is enforced, because
the `.deb` links the *distribution's* Qt rather than a bundled copy:

```bash
bash cpp/packaging/build_deb.sh   # writes build/packaging/syspeek_<ver>_amd64.deb
# or prove it end to end, in a clean container of the target release:
docker run --rm -v "$PWD":/src:ro -v /tmp/deb:/out ubuntu:24.04 \
  bash /src/cpp/packaging/ci/deb_container_build.sh
docker run --rm -v /tmp/deb:/pkg:ro -v "$PWD/cpp/packaging/ci":/ci:ro \
  ubuntu:24.04 bash /ci/deb_container_verify.sh
```

The verify script installs the package in a bare container, checks that every
shared object resolves, and runs `--dump` and `--probe` — the gate for both a
Qt API newer than the floor and an under-declared `Depends:` line.

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

## Cloud instance & cost

The **Cloud** page answers "which instance am I on, and what is this app costing
me?" without touching a control plane:

- **Identity** comes from the link-local instance metadata service, the same
  document the Datadog agent's cloud-provider client reads. AWS (IMDSv2 token
  then `/latest/dynamic/instance-identity/document`), Azure
  (`/metadata/instance/compute?api-version=2021-12-13`), GCP
  (`/computeMetadata/v1/instance/{id,name,machine-type,zone}`) and OCI
  (`opc/v2/instance/...` behind `Bearer Oracle` at `169.254.0.23`) are probed
  in that order — each answers only to its own header, so there are no false
  positives. Instance id, type, region/zone, account/project, private IP, image
  and architecture are shown as reported.
- **Accrued cost** is the hourly rate times uptime **since this boot** — the
  page says "this boot", never "instance age", because a reboot resets host
  uptime. The monthly projection is the standard 730-hour month.
- **App share** splits the instance rate by the CPU the focused app actually
  used: `rate × cpu% / vCPU`. A per-core number is intentional (a fully busy
  8 vCPU box spends the whole rate), and the page states the formula.
  `TOP PROCESSES IN THE FOCUSED APP` ranks the focused app's own process tree,
  not the whole machine.
- **Prices are never invented or bundled.** Syspeek reads a rate file
  (`~/.config/syspeek/cloud-rates.txt`, override with `[cloud] rates=`):

  ```ini
  # imported 2026-10-28
  aws/c7g.2xlarge       = 0.2920
  azure/Standard_D2s_v3 = 0.0960
  ```

  A bare `c7g.2xlarge` works when only one provider lists it. The `# imported`
  stamp is not decoration: past 30 days the page and the Settings row say
  `(56 days old)`, so a stale number is never silently presented as current.
  Import the current on-demand list price for the instance you are on with:

  ```sh
  syspeek --cloud-prices                    # reads the instance type from IMDS
  syspeek --cloud-prices=c7g.2xlarge --region=eu-west-1
  ```

  That shells out to the `aws` CLI you already use
  (`aws pricing get-products --service-code AmazonEC2`, filtered to the shared /
  used-capacity / Linux / no-software SKU) and writes the rate file with a fresh
  import stamp. It is opt-in, never runs on a tick, and Syspeek never reads or
  stores credentials — the CLI uses whatever profile you already configured.
- **Billed spend** is what the provider actually charged this month, read from
  its cost API — a different number from the estimate above, and labelled as
  such:

  ```sh
  aws ce get-cost-and-usage --time-period Start=<month start>,End=<tomorrow> \
      --granularity DAILY --metrics UnblendedCost \
      --group-by Type=DIMENSION,Key=SERVICE --output json
  aws ce get-cost-forecast --time-period Start=<today>,End=<month end> \
      --metric UNBLENDED_COST --granularity MONTHLY --output json
  az  consumption usage list --top 1000 \
      --filter "properties/usageStart ge '<month start>'" -o json
  ```

  The **BILLED SPEND** card shows month to date, the month-end projection
  (billed + the provider's forecast for the rest of the month), a daily chart
  and the per-service split. The metric is named where it matters: AWS
  `UnblendedCost`, Azure `pretaxCost`. Cost data lags by up to a day and part
  of it can be marked estimated — both are stated on the card rather than
  smoothed over. GCP bills through a BigQuery export (no CLI cost API) and OCI's
  usage API is not wired yet, so both print the gate instead of a number.

  Cost Explorer requests are **billable to your own account**, so this query
  runs every 6 hours and has its own switch:

  ```ini
  [cloud]
  spend            = true    ; the provider cost query (billable, every 6 h)
  spend_timeout_ms = 30000   ; per query; clamped 5000..180000
  ```

- **Instance age and the account inventory** come from the provider's own CLI,
  because only the account knows when this instance launched. The IMDS
  document names the provider, then Syspeek asks that provider's CLI, reusing
  your existing login:

  ```sh
  aws   ec2 describe-instances --region <imds region> --output json \
        --query "Reservations[].Instances[]"
  az    vm list -d --subscription <imds account>
  gcloud compute instances list --project <imds project> --format=json
  oci   compute instance list --compartment-id <imds compartment> \
        --region <imds region> --all --output json
  ```

  The result is the **FLEET INVENTORY** card: this instance first (`*`), then
  the rest of the account with type, state, age and `$/MO`. `AGE` on the tile
  row is the launch time's age; `COST` shows accrual **since launch (upper
  bound)** above **this boot**. `$/MO` sums running rows only — a stopped (or
  deallocated) VM never claims a monthly bill — and rows the rate file cannot
  price are counted as `unpriced` instead of reading `$0`. Long accounts are
  capped at 200 rows with `showing first N of M` in the summary. Azure's
  listing carries no creation time, so its rows honestly show `--` age.

  The listing is polled every 10 minutes in the background (a CLI that hangs is
  killed at the budget), so no tick ever waits on it. It is a real API call, so
  it is separately switchable from the link-local read:

  ```ini
  [cloud]
  enabled              = true   ; the link-local IMDS read
  inventory            = true   ; the account listing through the provider CLI
  inventory_timeout_ms = 25000  ; per listing; clamped 2000..120000
  rates                =        ; empty = ~/.config/syspeek/cloud-rates.txt
  timeout_ms           = 450    ; the whole four-provider sweep; IMDS answers in ms
  ```

  No CLI on `PATH`, no login, or an expired session prints the exact refusal
  and the fix (`aws configure`, `az login`, `gcloud auth login`,
  `oci setup config`) — Syspeek never reads, caches or stores credentials
  itself.

The metadata request goes to a link-local address (`169.254.169.254` /
`169.254.0.23`), which is never routed off the host, and is the single network
read Syspeek performs without you asking. `enabled = false` turns even that off
and the page says so instead of showing empty cards. Off a cloud instance —
or on a host where IMDS is firewalled or IMDSv2 tokens are refused — every card
prints the gate text rather than a blank.

Read-only in the cloud by design: Syspeek identifies the instance, prices it and
lists the account, it never starts, stops or terminates anything.

The Cloud page states its own footprint in a **CLOUD FOOTPRINT** card — every
outbound action, its cadence, the measured cost of the last metadata sweep, and
the guarantee that no credential is read and nothing is changed. The full
breakdown (endpoints, headers, budgets, switches, rate-file format) is in
[docs/cloud.md](docs/cloud.md).

## Advanced (optional)

Thresholds and polling intervals are configurable in
`~/.config/syspeek/config.ini`; sensible defaults are used if it's absent.

**What Syspeek costs** — a HUD that watches a machine should be able to say
what it costs that machine. Settings → GENERAL carries three live rows:

```
self cost   cpu 6.04%  rss 36.0 MB
tick        62 ms last  avg 54 ms  worst 84 ms  over 120 ticks
sections    processes 41 ms  host 12 ms  sockets 4.1 ms
```

`tick` is the time the sampler spends producing one snapshot; `sections` names
the slowest collectors in that tick. It is also a regression gate rather than
decoration: `syspeek --probe` prints `tickMs=` and `selfCpu=`/`selfRssKb=` per
tick plus a `probe cost` summary, and CI fails a probe whose worst tick exceeds
1.5 s — which is how a blocking call on a sampler tick would be caught.

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
