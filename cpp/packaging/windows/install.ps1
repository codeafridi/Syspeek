# Crowsnest PowerShell installer (Windows 10+).
#
# Bootstrap one-liner (no Git Bash needed):
#   irm https://raw.githubusercontent.com/codeafridi/Crowsnest/main/cpp/packaging/windows/install.ps1 | iex
#
# With options (irm|iex cannot take parameters - they come from env):
#   $env:CROWSNEST_UNINSTALL='1';  irm ... | iex
#   $env:CROWSNEST_NO_AUTOSTART='1'; irm ... | iex
#   $env:CROWSNEST_WIN_URL='<zip url>'; irm ... | iex
# Direct invocation takes the same options as parameters:
#   ./install.ps1 -Uninstall | -NoAutostart | -Url <zip url>
#
# Mirrors install.sh (bash) for Windows: portable zip into
# %LOCALAPPDATA%\Crowsnest, 'crowsnest' shims in the WindowsApps PATH
# directory, HKCU Run autostart through the app's own --autostart verb.

#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$NoAutostart,
    [string]$Url
)

$ErrorActionPreference = 'Stop'
$Repo = 'codeafridi/Crowsnest'

function Info([string]$msg) { Write-Host $msg }
function Die([string]$msg) { throw "error: $msg" }

if ($env:CROWSNEST_UNINSTALL) { $Uninstall = $true }
if ($env:CROWSNEST_NO_AUTOSTART) { $NoAutostart = $true }

function LatestTag {
    if ($env:CROWSNEST_TEST_TAG) { return $env:CROWSNEST_TEST_TAG }
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
    if ($env:CROWSNEST_WIN_URL) { return $env:CROWSNEST_WIN_URL }
    if ($Url) { return $Url }
    $tag = LatestTag
    $ver = ($tag -replace '^v', '')
    return "https://github.com/$Repo/releases/download/$tag/Crowsnest-$ver-windows-portable.zip"
}

$AppDir = Join-Path $env:LOCALAPPDATA 'Crowsnest'
$ShimDir = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps'
$Exe = Join-Path $AppDir 'crowsnest.exe'

if ($Uninstall) {
    if (Test-Path $Exe) {
        & $Exe --autostart=off 2>$null | Out-Null
    }
    Get-Process -Name crowsnest -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $AppDir) { Remove-Item -Recurse -Force $AppDir }
    $shim = Join-Path $ShimDir 'crowsnest.cmd'
    $shimPosix = Join-Path $ShimDir 'crowsnest'
    if (Test-Path $shim) { Remove-Item -Force $shim }
    if (Test-Path $shimPosix) { Remove-Item -Force $shimPosix }
    Info 'Crowsnest uninstalled (Windows).'
    return
}

Info 'Downloading Crowsnest for Windows...'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('crowsnest-win-' + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'crowsnest.zip'
    $zipUrl = ZipUrl
    $ok = $false
    foreach ($attempt in 1..2) {
        try {
            if ($zipUrl -match '^(?i)file:/') {
                $localZip = [Uri]::UnescapeDataString(([Uri]$zipUrl).LocalPath)
                Copy-Item -LiteralPath $localZip -Destination $zip -Force
            } elseif ($zipUrl -match '^[a-zA-Z]:[\\/]') {
                Copy-Item -LiteralPath $zipUrl -Destination $zip -Force
            } else {
                Invoke-WebRequest -Uri $zipUrl -OutFile $zip -UseBasicParsing -TimeoutSec 600
            }
            if ((Get-Item -LiteralPath $zip).Length -lt 10MB) { throw 'downloaded zip is too small' }
            $ok = $true
            break
        } catch {
            if ($attempt -eq 2) { throw "download failed: $($_.Exception.Message)" }
            Info "download attempt $attempt failed, retrying"
        }
    }

    Get-Process -Name crowsnest -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path $AppDir) { Remove-Item -Recurse -Force $AppDir }
    New-Item -ItemType Directory -Path $AppDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $AppDir -Force

    if (-not (Test-Path $Exe)) {
        Die 'portable install incomplete (crowsnest.exe missing)'
    }

    New-Item -ItemType Directory -Path $ShimDir -Force | Out-Null
    $shimCmd = Join-Path $ShimDir 'crowsnest.cmd'
    # A bare `crowsnest` must return the prompt immediately. Calling a GUI
    # executable directly from cmd.exe waits for the HUD to exit; verbs with
    # arguments stay synchronous so their output/exit status remains useful.
    $shimCmdContents = (@(
        '@echo off',
        'if "%~1"=="" (',
        '  start "" /b "%LOCALAPPDATA%\Crowsnest\crowsnest.exe"',
        '  exit /b 0',
        ')',
        '"%LOCALAPPDATA%\Crowsnest\crowsnest.exe" %*'
    ) -join "`r`n") + "`r`n"
    [IO.File]::WriteAllBytes($shimCmd,
        [Text.Encoding]::ASCII.GetBytes($shimCmdContents))
    $shimPosix = Join-Path $ShimDir 'crowsnest'
    $shimPosixContents = (@(
        '#!/bin/sh',
        'if [ "$#" -eq 0 ]; then',
        '  cmd.exe //c start "" /b "$LOCALAPPDATA\Crowsnest\crowsnest.exe" >/dev/null 2>&1',
        '  exit 0',
        'fi',
        'exec "$LOCALAPPDATA/Crowsnest/crowsnest.exe" "$@"'
    ) -join "`n") + "`n"
    [IO.File]::WriteAllBytes($shimPosix,
        [Text.Encoding]::ASCII.GetBytes($shimPosixContents))

    if ($NoAutostart) {
        Info 'not started - open a NEW shell and type: crowsnest'
    } else {
        & $Exe --autostart=on
        Start-Process -FilePath $Exe
    }

    Info ''
    Info 'Control it:'
    Info '  crowsnest                  launch the HUD and return to the prompt'
    Info '  crowsnest start            launch the HUD if it is not running yet'
    Info '  crowsnest stop             stop every running HUD instance'
    Info '  crowsnest restart          restart the HUD'
    Info '  crowsnest status           autostart state + running instances'
    Info '  crowsnest --autostart=on|off|status   login autostart (HKCU Run key)'
    Info '  crowsnest --dump           JSON snapshot of the machine (headless check)'
    Info '  update                   re-run this bootstrapper'
    Info "  uninstall                \$env:CROWSNEST_UNINSTALL='1'; irm this script | iex"
    Info "note: unsigned build - SmartScreen 'More info / Run anyway' on first GUI launch."
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
