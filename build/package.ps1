<#
.SYNOPSIS
    Packages a built WinDV for release: a portable zip and an installer.

.DESCRIPTION
    Run after building WinDV.sln (Release) for the same platform. Produces, in -OutDir:

      WinDV-<version>-<arch>-portable.zip
          WinDV-<version>-<arch>\WinDV.exe    launcher (launcher\), starts app\WinDV.exe
          WinDV-<version>-<arch>\README.md
          WinDV-<version>-<arch>\app\...      the WinUI app and its runtime
      WinDV-<version>-<arch>-Setup.exe        Inno Setup installer (installer\WinDV.iss)

    The installer needs Inno Setup 6 (winget install JRSoftware.InnoSetup).

.EXAMPLE
    build\package.ps1 -Arch x64
#>
param(
    [Parameter(Mandatory)] [ValidateSet('x64', 'x86')] [string] $Arch,
    # Defaults to <Version> in ui\WinDV.csproj.
    [string] $Version,
    [string] $OutDir = 'dist',
    # Build only the zip (no Inno Setup needed).
    [switch] $NoInstaller
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$platform = if ($Arch -eq 'x86') { 'Win32' } else { 'x64' }

if (-not $Version) {
    $Version = (Select-Xml -Path "$root\ui\WinDV.csproj" -XPath '//Version').Node.InnerText
}
# 2.1.0-0123abcd -> 2.1.0 for version resources.
$fileVersion = ($Version -split '[-+]')[0]

$appOut = "$root\ui\bin\$Arch\Release\net10.0-windows10.0.26100.0\win-$Arch"
$launcher = "$root\$platform\Release\WinDVLauncher.exe"
foreach ($path in "$appOut\WinDV.exe", $launcher) {
    if (-not (Test-Path $path)) { throw "$path not found. Build WinDV.sln (Release|$platform) first." }
}

if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$name = "WinDV-$Version-$Arch"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\app" | Out-Null

# The app, without debug symbols.
Copy-Item "$appOut\*" "$stage\app" -Recurse
Get-ChildItem "$stage\app" -Recurse -Filter *.pdb | Remove-Item

# Portable zip: only the launcher and README at the top level.
Copy-Item $launcher "$stage\WinDV.exe"
Copy-Item "$root\README.md" "$stage\README.md"
$zip = Join-Path $OutDir "$name-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"

if (-not $NoInstaller) {
    $iscc = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found. Install it (winget install JRSoftware.InnoSetup) or pass -NoInstaller.' }

    & $iscc /Q "/DAppVersion=$Version" "/DFileVersion=$fileVersion" "/DArch=$Arch" `
        "/DAppDir=$stage\app" "/DOutputDir=$OutDir" "$root\installer\WinDV.iss"
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }
    Write-Host "Created $(Join-Path $OutDir "$name-Setup.exe")"
}

Remove-Item $stage -Recurse -Force
