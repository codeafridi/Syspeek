# Syspeek on macOS

Syspeek is a cross-platform HUD: the same panels, alerting and tooling
as the Linux build, with native macOS collectors behind the same
`SystemBackend` seam.

## Install

1. Download `Syspeek-<version>-macos.dmg` from
   [releases](https://github.com/codeafridi/Syspeek/releases/latest).
2. Open the DMG and drag **Syspeek.app** into **Applications**.
3. First run (build is unsigned, Gatekeeper blocks it once):
   - right-click `Syspeek.app` -> **Open** -> **Open**, or
   - `xattr -d com.apple.quarantine /Applications/Syspeek.app`

The app is a `LSUIElement` overlay: no Dock icon, no menu bar entry.
Quit with `syspeek.sh stop` (below) or `pkill syspeek`; login autostart
is managed via launchd, so a plain quit is restarted on next login
unless you disable the agent first.

## Service management (launchd)

`cpp/packaging/install_macos.sh` mirrors the Linux `install.sh` verbs:

```
bash cpp/packaging/install_macos.sh path    # print the managed binary path
bash cpp/packaging/install_macos.sh run     # foreground run (no LaunchAgent)
bash cpp/packaging/install_macos.sh enable  # install ~/Library/LaunchAgents/dev.syspeek.hud.plist
bash cpp/packaging/install_macos.sh start   # load + start
bash cpp/packaging/install_macos.sh status
bash cpp/packaging/install_macos.sh logs    # tail ~/Library/Logs/Syspeek/syspeek.log
bash cpp/packaging/install_macos.sh stop|restart|disable|uninstall
```

Logs: `~/Library/Logs/Syspeek/`.
Config: `~/Library/Application Support/Syspeek/config.conf`.
LaunchAgent: `~/Library/LaunchAgents/dev.syspeek.hud.plist`
(`RunAtLoad` + `KeepAlive`).

## Permissions

None are required. Focus tracking uses `NSWorkspace.frontmostApplication`
(falls back to the on-screen `CGWindowList`), which is permission-free;
the HUD does not read window titles.

## What runs on macOS

| Card | State |
|---|---|
| Overview (CPU/MEM/LOAD), Processes, Threads/FDs, Memory, Network, Disks, Disk I/O, Interfaces | native collectors |
| Sockets / listeners | libproc (`PROC_PIDFDSOCKETINFO`), same-user sockets when unprivileged |
| Host inventory | sysctl (gohai-style), Rosetta-aware |
| Services | `launchctl list` |
| Logs / Errors | unified logging (OSLogStore), feeds the whole parse pipeline |
| Security | SIP / FileVault / Gatekeeper / application firewall posture |
| Software (SBOM) | Homebrew Cellar + `/Applications/*\.app` bundles |
| Runtime (SSI) | procargs2 env + exe path (Datadog languagedetection rules) |
| File integrity (FIM) | portable watcher (FSEvents/kqueue via Qt); `/etc/*` + brew etc defaults |
| TCP HEALTH, CGROUP, PSI, NAMESPACES | gated. TCP per-protocol stats are kernel-redacted for unprivileged callers on macOS (verified: `netstat -s` as user returns zeroed counters while sudo shows real ones); cgroups/PSI/namespaces are Linux-only kernel facilities |
| Network Path | optional tool: `brew install mtr` (same as Linux; card degrades gracefully without it) |
| k8s/Docker/TLS/OTLP/DogStatsD/SLO/Traces/Cost/Incidents/Watchdog | same as Linux |
| Profiles (per-thread CPU) | Linux-only (cross-process mach task ports restricted) |

## Building from source

```
bash cpp/packaging/build_macos.sh
# -> cpp/build/packaging/Syspeek-<version>-macos.dmg
```

Universal `arm64;x86_64` by default (`SYSPEEK_ARCHS` overrides), requires
Qt 6.8+ (`QT_ROOT_DIR` or `macdeployqt` in PATH) and Xcode CLT.
Notarization is pending (no Apple Developer account yet); the DMG is
signed-free until then.
