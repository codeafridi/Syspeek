<#
.SYNOPSIS
Writes winget + Scoop manifests for a released Syspeek version, reading
SHA256 hashes from the local release artifacts.
.DESCRIPTION
Run after the Windows artifacts exist (CI build-windows job or maintainer
host). Installer URLs point at the tag download for that version. Winget
manifests are staged under cpp/packaging/windows/manifests/<version>/

.EXAMPLE
powershell -File cpp/packaging/windows/write_manifests.ps1 -Version 0.8.1
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Version
)

$ErrorActionPreference = "Stop"

function WriteNoBom([string] $path, [string] $text) {
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
}

$outRoot = Join-Path $PSScriptRoot "manifests/$Version"
New-Item -ItemType Directory -Force $outRoot | Out-Null

$packDir = Join-Path (
    Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
) "cpp/build/packaging"
$msi = Join-Path $packDir "Syspeek-$Version-windows.msi"
$zip = Join-Path $packDir "Syspeek-$Version-windows-portable.zip"
if (-not (Test-Path $msi)) { throw "missing $msi (build the MSI first)" }
if (-not (Test-Path $zip)) { throw "missing $zip (build the portable zip first)" }
$msiSha = (Get-FileHash -Algorithm SHA256 $msi).Hash.ToLower()
$zipSha = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
$base = "https://github.com/codeafridi/Syspeek/releases/download/v$Version"

# ---- winget (manifest 1.6.0, three files) ----
$pkgId = "Codeafridi.Syspeek"
WriteNoBom (Join-Path $outRoot "$pkgId.yaml") (@'
PackageIdentifier: Codeafridi.Syspeek
PackageVersion: __VERSION__
DefaultLocale: en-US
PackageName: Syspeek
Publisher: Syspeek project
PackageUrl: https://github.com/codeafridi/Syspeek
License: MIT
LicenseUrl: https://github.com/codeafridi/Syspeek/blob/main/LICENSE
ShortDescription: Local system HUD (CPU, memory, network, processes, containers) overlay for Linux, macOS and Windows.
Moniker: syspeek
Tags:
- monitoring
- hud
- systems
- performance
ReleaseNotesUrl: https://github.com/codeafridi/Syspeek/releases/tag/v__VERSION__
ManifestType: version
ManifestVersion: 1.6.0
'@.Replace('__VERSION__', $Version))

# Installer manifest. No ProductCode line: the WiX Package generates a
# random ProductCode per build; winget resolves the package via the MSI
# UpgradeCode (9C7C4470-8E27-4BDB-98C9-3774BF58B4F9) in the actual installer.
# Literal here-string: tokens replaced below (a double-quoted here-string
# would expand $MSISHA/$ZIPSHA to empty BEFORE the .Replace calls).
WriteNoBom (Join-Path $outRoot "$pkgId.installer.yaml") (@'
PackageIdentifier: Codeafridi.Syspeek
PackageVersion: __VERSION__
InstallerLocale: en-US
Installers:
- InstallerLocale: en-US
  Architecture: x64
  InstallerType: wix
  InstallerUrl: https://github.com/codeafridi/Syspeek/releases/download/v__VERSION__/Syspeek-__VERSION__-windows.msi
  InstallerSha256: __MSISHA__
- InstallerLocale: en-US
  Architecture: x64
  InstallerType: zip
  NestedInstallerType: portable
  InstallerUrl: https://github.com/codeafridi/Syspeek/releases/download/v__VERSION__/Syspeek-__VERSION__-windows-portable.zip
  InstallerSha256: __ZIPSHA__
  NestedInstallerFiles:
  - RelativeFilePath: syspeek.exe
    PortableCommandAlias: syspeek
ManifestType: installer
ManifestVersion: 1.6.0
'@.Replace('__VERSION__', $Version).
    Replace('__MSISHA__', $msiSha).
    Replace('__ZIPSHA__', $zipSha))

WriteNoBom (Join-Path $outRoot "$pkgId.locale.en-US.yaml") (@'
PackageIdentifier: Codeafridi.Syspeek
PackageVersion: __VERSION__
PackageLocale: en-US
Publisher: Syspeek project
PackageName: Syspeek
License: MIT
ShortDescription: Local system HUD overlay (CPU/memory/network/processes/containers).
Description: |-
  Syspeek is a lightweight native-Qt overlay HUD for your own machine:
  per-process CPU and memory, network sockets and rates, disks,
  containers, kubernetes info, alerts, log pipeline and more, collected
  locally only.
ManifestType: defaultLocale
ManifestVersion: 1.6.0
'@.Replace('__VERSION__', $Version))

# ---- Scoop (portable zip; MSI offered via the notes) ----
WriteNoBom (Join-Path $outRoot "syspeek-scoop.json") ($(
    [ordered]@{
        homepage    = "https://github.com/codeafridi/Syspeek"
        description = "Local system HUD (CPU, memory, network, processes, containers) overlay."
        version     = $Version
        license     = "MIT"
        notes       = "Unsigned build: Windows SmartScreen may warn on first run. Installs the portable zip; run syspeek.exe from anywhere. The Windows MSI is on the releases page."
        url         = "$base/Syspeek-$Version-windows-portable.zip"
        hash        = $zipSha
        bin         = "syspeek.exe"
        checkver    = "github"
        autoupdate  = @{
            url    = "https://github.com/codeafridi/Syspeek/releases/download/v`$version/Syspeek-`$version-windows-portable.zip"
            hash   = @{ url = "https://github.com/codeafridi/Syspeek/releases/download/v`$version/Syspeek-`$version-windows-portable.zip.sha256" }
        }
    } | ConvertTo-Json -Depth 4) )

Write-Host ("manifests written to {0} (msi {1}, zip {2})" -f $outRoot, (Join-Path $outRoot "$pkgId.installer.yaml"), (Join-Path $outRoot "syspeek-scoop.json"))
Write-Host "submit winget -> microsoft/winget-pkgs (fork then PR); scoop -> a bucket repo"
