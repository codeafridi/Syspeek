# Syspeek PowerShell installer (Windows 10+).
#
# Bootstrap one-liner (no Git Bash needed):
#   irm https://raw.githubusercontent.com/codeafridi/Syspeek/main/cpp/packaging/windows/install.ps1 | iex
#
# With options (irm|iex cannot take parameters - they come from env):
#   $env:SYSPEEK_UNINSTALL='1';  irm ... | iex
#   $env:SYSPEEK_NO_AUTOSTART='1'; irm ... | iex
#   $env:SYSPEEK_WIN_URL='<zip url>'; irm ... | iex
# Direct invocation takes the same options as parameters:
#   ./install.ps1 -Uninstall | -NoAutostart | -Url <zip url>
#
# Mirrors install.sh (bash) for Windows: portable zip into
# %LOCALAPPDATA%\Syspeek, 'syspeek' shims in the WindowsApps PATH
# directory, HKCU Run autostart through the app's own --autostart verb.

#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$NoAutostart,
    [string]$Url
)

$ErrorActionPreference = 'Stop'
$Repo = 'codeafridi/Syspeek'

function Info([string]$msg) { Write-Host $msg }
function Die([string]$msg) { throw "error: $msg" }

if ($env:SYSPEEK_UNINSTALL) { $Uninstall = $true }
if ($env:SYSPEEK_NO_AUTOSTART) { $NoAutostart = $true }

function LatestTag {
    if ($env:SYSPEEK_TEST_TAG) { return $env:SYSPEEK_TEST_TAG }
    $null = Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
    $h = New-Object System.Net.Http.HttpClientHandler
    $h.AllowAutoRedirect = $false
    $c = New-Object System.Net.Http.HttpClient($h)
    try {
        $resp = $c.GetAsync("https://github.com/$Repo/releases/latest").GetAwaiter().GetResult()
        $loc = $resp.Headers.Location
        if (-not $loc) { throw 'error: could not resolve the latest release' }
        $tag = ($loc.ToString() -split '/')[-1]
        if (-not $tag) { throw 'error: could not resolve the latest release' }
        Info "latest release: $tag"
        return $tag
    } finally {
        $c.Dispose()
    }
}

function ZipUrl {
    if ($env:SYSPEEK_WIN_URL) { return $env:SYSPEEK_WIN_URL }
    if ($Url) { return $Url }
    $tag = LatestTag
    $ver = ($tag -replace '^v', '')
    return "https://github.com/$Repo/releases/download/$tag/Syspeek-$ver-windows-portable.zip"
}

$AppDir = Join-Path $env:LOCALAPPDATA 'Syspeek'
$ShimDir = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps'
$Exe = Join-Path $AppDir 'syspeek.exe'

if ($Uninstall) {
    if (Test-Path $Exe) {
        & $Exe --autostart=off 2>$null | Out-Null
    }
    Get-Process -Name syspeek -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $AppDir) { Remove-Item -Recurse -Force $AppDir }
    $shim = Join-Path $ShimDir 'syspeek.cmd'
    $shimPosix = Join-Path $ShimDir 'syspeek'
    if (Test-Path $shim) { Remove-Item -Force $shim }
    if (Test-Path $shimPosix) { Remove-Item -Force $shimPosix }
    Info 'Syspeek uninstalled (Windows).'
    return
}

Info 'Downloading Syspeek for Windows...'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('syspeek-win-' + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'syspeek.zip'
    $zipUrl = ZipUrl
    $ok = $false
    foreach ($attempt in 1..2) {
        try {
            Invoke-WebRequest -Uri $zipUrl -OutFile $zip -UseBasicParsing -TimeoutSec 600
            if ((Get-Item -LiteralPath $zip).Length -lt 10MB) { throw 'downloaded zip is too small' }
            $ok = $true
            break
        } catch {
            if ($attempt -eq 2) { throw "download failed: $($_.Exception.Message)" }
            Info "download attempt $attempt failed, retrying"
        }
    }

    Get-Process -Name syspeek -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $AppDir) { Remove-Item -Recurse -Force $AppDir }
    New-Item -ItemType Directory -Path $AppDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $AppDir -Force

    if (-not (Test-Path $Exe)) {
        Die 'portable install incomplete (syspeek.exe missing)'
    }

    New-Item -ItemType Directory -Path $ShimDir -Force | Out-Null
    $shimCmd = Join-Path $ShimDir 'syspeek.cmd'
    [IO.File]::WriteAllBytes($shimCmd,
        [Text.Encoding]::ASCII.GetBytes("@echo off`r`n""%LOCALAPPDATA%\Syspeek\syspeek.exe"" %*`r`n"))
    $shimPosix = Join-Path $ShimDir 'syspeek'
    [IO.File]::WriteAllBytes($shimPosix,
        [Text.Encoding]::ASCII.GetBytes("#!/bin/sh`nexec `"`$LOCALAPPDATA/Syspeek/syspeek.exe`" `"`$@`"`n"))

    if ($NoAutostart) {
        Info 'not started - open a NEW shell and type: syspeek'
    } else {
        & $Exe --autostart=on
        Start-Process -FilePath $Exe
    }

    Info ''
    Info 'Control it:'
    Info '  syspeek                  run the HUD (any new shell; shim on PATH)'
    Info '  syspeek --autostart=on|off|status   login autostart (HKCU Run key)'
    Info '  syspeek --dump           JSON snapshot of the machine (headless check)'
    Info '  update                   re-run this bootstrapper'
    Info "  uninstall                \$env:SYSPEEK_UNINSTALL='1'; irm this script | iex"
    Info "note: unsigned build - SmartScreen 'More info / Run anyway' on first GUI launch."
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
