# Windows (beta, 0.8+)

Syspeek on Windows is the same cross-platform overlay HUD behind the
M42 platform seam. Windows is NOT the primary reference platform —
Linux is. Everything below was collected with native Windows APIs
(CI-asserted on every push; see the release workflow).

## Install

**MSI (recommended)** — download
`Syspeek-<version>-windows.msi` from the
[releases](https://github.com/codeafridi/Syspeek/releases) page and run
it. Per-machine install into `C:\Program Files\Syspeek`, Start Menu
shortcut, Add/Remove Programs entry, WiX major upgrades supported.

**Portable zip** — `Syspeek-<version>-windows-portable.zip`, unzip
anywhere, run `syspeek.exe` directly (Qt runtime is bundled next to the
exe). No registry, no install.

**Scripted portable install (one line, inside [Git Bash](https://git-scm.com/downloads)):**

```bash
curl -fsSL https://raw.githubusercontent.com/codeafridi/Syspeek/main/install.sh | sh
```

It downloads the portable zip into `%LOCALAPPDATA%\Syspeek` (Expand-Archive),
writes a `syspeek` command shim into `%LOCALAPPDATA%\Microsoft\WindowsApps`
(already on PATH), sets HKCU Run autostart via the exe's own
`--autostart=on` verb, and starts the HUD. `sh install.sh --uninstall`
removes all of it. (That downloads the same zip; there is no MotW
because curl does not stamp it.)

**Scoop** — the bucket manifest is generated per release by
`cpp/packaging/windows/write_manifests.ps1` (see `manifests/` in the
repo after a release); copy `syspeek-scoop.json` into a bucket repo if
you self-host one. `scoop` support does not require an account.

**winget** — manifests are generated per release by
`cpp/packaging/windows/write_manifests.ps1` and need the standard
[fork→PR flow](https://github.com/microsoft/winget-pkgs) to appear in
the community source (one-time PR per version after a release).

## Unsigned build — SmartScreen first run

There is no Authenticode certificate yet (same policy as the macOS
DMG). On the first run the MSI/portable you may see
"Windows protected your PC":

1. Click **More info**.
2. Click **Run anyway**.

For the portable zip additionally:
`Unblock-File .\syspeek.exe` (or the ZIP properties → Unblock) clears
the MotW prompt. Verify the SHA256 from the release's
`Syspeek-<version>-windows.msi.sha256` /
`...-portable.zip.sha256` first.

## Autostart

Run-key autostart, grounded on the same key shipped tools use
(SystemInformer's HKCU Run key):

```bat
syspeek --autostart=on
syspeek --autostart=status
syspeek --autostart=off
```

## Feature matrix (M49 + M50)

| Card | Status on Windows | Notes |
| --- | --- | --- |
| Overview (host CPU/mem/load strip) | live | GetSystemTimes + GlobalMemoryStatusEx; `--` for loadavg (no honest analogue) |
| per-PID CPU/mem/threads/FDs | live | Toolhelp32 + GetProcessTimes/MemoryInfo + handle count |
| Context switches per process | live | NtQuerySystemInformation SystemProcessInformation walk (cumulative total) |
| Processes cmdline + env | live | PEB walk, same-user targets only (elevated targets yield empty) |
| Network sockets + TCP health | live | GetExtendedTcp/UdpTable OWNER_PID + GetTcpStatisticsEx — works unprivileged (unlike macOS) |
| Disks | live | PDH `\PhysicalDisk(*)\*` |
| Host inventory | live | CentralProcessor registry + drives + GetAdaptersAddresses |
| Software (SBOM) | live | registry Uninstall keys, purls `pkg:generic/<name>@<ver>` |
| Services | live | SCM EnumServicesStatusExW |
| Logs / errors / threat | live | EventLog System+Application channels |
| FIM | live | QFileSystemWatcher (ReadDirectoryChangesW backend), default roots from the machine drive |
| Containers / k8s / DBM / TLS / OTLP / DogStatsD | live | same as Linux (network APIs) |
| Security posture | live | UAC / SecureBoot / CFG / ASLR / SEHOP registry checks |
| Notifications | live | Shell_NotifyIconW tray balloon |
| Cgroups / PSI / dpkg SBOM / Profile threads | gated | n/a on Windows ("not supported on this OS") |

## Collection discipline

- No WMI / PowerShell spawning anywhere; registry reads via
  QSettings NativeFormat only.
- Elevated processes are not introspected (OpenProcess VM_READ fails
  gracefully → the UI keeps its last good value).
- All counters are the exact ship-tool vocabulary
  (windows_exporter / Sysinternals names for PDH and SCM; osquery's
  programs table for the SBOM keys).

## Packaging internals

- `cpp/packaging/windows/Syspeek.wxs` — WiX v4/5 authoring
  (per-machine, single-cab, MajorUpgrade). UpgradeCode is fixed:
  `9C7C4470-8E27-4BDB-98C9-3774BF58B4F9`.
- `cpp/packaging/windows/build_msi.ps1` — windeployqt stage, payload
  fragment generation, `wix build` (WiX v5.0.2 pinned; v7 gates every
  run on the OSMF EULA, CI-hostile), portable zip, SHA256s.
- The exe resource (`syspeek.rc`) + icon (`syspeek.ico`) is generated
  by the repo (no font dependency), read from `cpp/packaging/windows/`.
