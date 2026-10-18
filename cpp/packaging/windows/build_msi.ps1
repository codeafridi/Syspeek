<#
.SYNOPSIS
Stages the Windows release payload (windeployqt), generates the WiX
payload fragment, and builds the Syspeek MSI with the WiX .NET tool.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File cpp/packaging/windows/build_msi.ps1 -Version 0.8.0
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Version
)

$ErrorActionPreference = "Stop"
# Native tool (windeployqt/wix/dotnet) nonzero exits abort the script.
$PSNativeCommandUseErrorActionPreference = $true

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$cppBuild = Join-Path $repoRoot "cpp/build"
$exe = Join-Path $cppBuild "Release/syspeek.exe"
if (-not (Test-Path $exe)) {
    throw "syspeek.exe not found: build Release first ($exe)"
}

$stage = Join-Path $cppBuild "packaging/windows-stage"
if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}
New-Item -ItemType Directory -Force $stage | Out-Null
$stagedExe = Join-Path $stage "syspeek.exe"
Copy-Item $exe $stagedExe

# Assemble the Qt runtime next to the staged exe (Core/Gui/Widgets/
# Network + platforms/plugins). No translations, no DirectX/D3D
# redistributables, no OpenGL software rasterizer.
windeployqt --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw --no-compiler-runtime $stagedExe

# ---- Generate payload.wxs: one Component per file, nested Directories.
$files = Get-ChildItem -Recurse -File $stage
$sha1 = New-Object System.Security.Cryptography.SHA1CryptoServiceProvider

function Get-ComponentIds([string] $relativePath) {
    $bytes = $sha1.ComputeHash([Text.Encoding]::UTF8.GetBytes($relativePath))
    $hex = [BitConverter]::ToString($bytes).Replace("-", "")
    return @{ Component = "C$hex"; File = "F$hex" }
}

$directoryIds = @{"." = "INSTALLFOLDER" }
$directoryXml = New-Object System.Text.StringBuilder
# INSTALLFOLDER itself is authored in Syspeek.wxs; the payload fragment
# only appends plugin subdirectories via DirectoryRef (a link, not a
# redefinition - duplicate symbols collide otherwise).
[void]$directoryXml.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')

$subDirs = @()
foreach ($file in $files) {
    $relLeaf = $file.FullName.Substring($stage.Length + 1)
    $relDir = Split-Path $relLeaf -Parent
    if ([string]::IsNullOrEmpty($relDir)) { $relDir = "." }
    if (-not $directoryIds.ContainsKey($relDir)) {
        $subDirs += $relDir
        $directoryIds[$relDir] = $null
    }
}
# Deterministic order, flat one-level dirs only (windeployqt layout).
$subDirs = $subDirs | Sort-Object
foreach ($relDir in $subDirs) {
    $dirName = $relDir.Replace("/", "\")
    $dirId = "D" + ("{0:x8}" -f ($dirName.ToLower().GetHashCode()))
    $directoryIds[$relDir] = $dirId
    [void]$directoryXml.AppendLine("      <Directory Id=`"$dirId`" Name=`"$(Split-Path $dirName -Leaf)`" />")
}
[void]$directoryXml.AppendLine('    </DirectoryRef>')

$componentXml = New-Object System.Text.StringBuilder
[void]$componentXml.AppendLine('    <ComponentGroup Id="PayloadComponents">')
foreach ($file in $files | Sort-Object DataSource) {
    $relLeaf = $file.FullName.Substring($stage.Length + 1)
    $relDir = Split-Path $relLeaf -Parent
    if ([string]::IsNullOrEmpty($relDir)) { $relDir = "." }
    $ids = Get-ComponentIds $relLeaf
    $dirId = $directoryIds[$relDir]
    $leafName = Split-Path $relLeaf -Leaf
    [void]$componentXml.AppendLine("      <Component Id=`"$($ids.Component)`" Directory=`"$dirId`" Bitness=`"always64`">")
    [void]$componentXml.AppendLine("        <File Id=`"$($ids.File)`" Name=`"$leafName`" Source=`"$($relLeaf.Replace("/", "\"))`" />")
    [void]$componentXml.AppendLine('      </Component>')
}
[void]$componentXml.AppendLine('    </ComponentGroup>')

$payload = New-Object System.Text.StringBuilder
[void]$payload.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
[void]$payload.AppendLine('  <Fragment>')
[void]$payload.Append($directoryXml.ToString())
[void]$payload.AppendLine('  </Fragment>')
[void]$payload.AppendLine('  <Fragment>')
[void]$payload.Append($componentXml.ToString())
[void]$payload.AppendLine('  </Fragment>')
[void]$payload.AppendLine('</Wix>')
$payloadPath = Join-Path $PSScriptRoot "payload.wxs"
$payload.ToString() | Out-File -Encoding ascii $payloadPath
Write-Host "payload.wxs: $($files.Count) files, $($subDirs.Count) subdirs"

# ---- WiX build. Pinned to WiX v5: v7 gates every run behind the OSMF
# EULA acceptance (error WIX7015 in CI); v5 CLI needs the .NET SDK 6+
# preinstalled on GH runners.
$wixCommand = Get-Command wix -ErrorAction SilentlyContinue
if ($null -eq $wixCommand) {
    dotnet tool install --global wix --version 5.0.2 | Out-Null
    $env:PATH = "$env:PATH;$env:USERPROFILE\.dotnet\tools"
} else {
    dotnet tool update --global wix --version 5.0.2 | Out-Null
}
wix --version

$outDir = Join-Path $cppBuild "packaging"
New-Item -ItemType Directory -Force $outDir | Out-Null
$msiOut = Join-Path $outDir "Syspeek-$Version-windows.msi"
wix build -arch x64 -d Version=$Version -b $stage -b $PSScriptRoot -o $msiOut `
    (Join-Path $PSScriptRoot "Syspeek.wxs") $payloadPath

$hash = (Get-FileHash -Algorithm SHA256 $msiOut).Hash.ToLower()
"$hash  Syspeek-$Version-windows.msi" |
    Out-File -Encoding ascii (Join-Path $outDir "Syspeek-$Version-windows.msi.sha256")
Write-Host "msi ok: $msiOut ($hash)"

# Portable zip (Scoop-friendly; runs from any directory, no install).
$zipOut = Join-Path $outDir "Syspeek-$Version-windows-portable.zip"
Compress-Archive -Path "$stage/*" -DestinationPath $zipOut -Force
$zipHash = (Get-FileHash -Algorithm SHA256 $zipOut).Hash.ToLower()
"$zipHash  Syspeek-$Version-windows-portable.zip" |
    Out-File -Encoding ascii (Join-Path $outDir "Syspeek-$Version-windows-portable.zip.sha256")
Write-Host "portable ok: $zipOut ($zipHash)"
