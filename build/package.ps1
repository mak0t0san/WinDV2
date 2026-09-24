<#
.SYNOPSIS
    Packages a built WinDV for release: a portable zip and an installer.

.DESCRIPTION
    Run after building WinDV.sln (Release) for the same platform. Publishes the WinUI
    app as Native AOT (ui\WinDV.csproj) and produces, in -OutDir:

      WinDV-<version>-<arch>-portable.zip
          WinDV-<version>-<arch>\WinDV.exe    launcher (launcher\), starts app\WinDV.exe
          WinDV-<version>-<arch>\README.md
          WinDV-<version>-<arch>\app\...      the WinUI app, its runtime and LICENSE.txt
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

$launcher = "$root\$platform\Release\WinDVLauncher.exe"
if (-not (Test-Path $launcher)) { throw "$launcher not found. Build WinDV.sln (Release|$platform) first." }

$msbuild = (Get-Command MSBuild.exe -ErrorAction SilentlyContinue).Source
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not $msbuild -and (Test-Path $vswhere)) {
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) { throw 'MSBuild.exe not found. Run from a VS developer prompt or install Visual Studio 2026.' }
# The AOT linker finds the C++ tools through vswhere.exe, which it expects on PATH.
if ((Test-Path $vswhere) -and -not (Get-Command vswhere.exe -ErrorAction SilentlyContinue)) {
    $env:Path = "$(Split-Path $vswhere);$env:Path"
}

if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$name = "WinDV-$Version-$Arch"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\app" | Out-Null

# The app, without debug symbols.
& $msbuild "$root\ui\WinDV.csproj" /t:Publish /nologo /v:m /p:Configuration=Release `
    "/p:Platform=$Arch" "/p:Version=$Version" "/p:PublishDir=$stage\app\"
if ($LASTEXITCODE -ne 0) { throw "Publishing the app failed with exit code $LASTEXITCODE." }
Get-ChildItem "$stage\app" -Recurse -Filter *.pdb | Remove-Item
# MIT: the license travels with every copy (zip and installer both ship app\).
Copy-Item "$root\LICENSE" "$stage\app\LICENSE.txt"

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
