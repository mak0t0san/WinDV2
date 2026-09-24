; WinDV installer (Inno Setup 6). Built by build\package.ps1, which passes:
;   /DAppVersion=2.1.0      shown to the user (may carry a -suffix on CI builds)
;   /DFileVersion=2.1.0     numeric, for the setup exe's version resource
;   /DArch=x64|x86
;   /DAppDir=<folder>       the WinUI app's output, without .pdb files
;   /DOutputDir=<folder>
;
; Installs per user by default (no admin prompt) into
; %LocalAppData%\Programs\WinDV; the first page offers an all-users install.
; The settings in HKCU\Software\Petr Mourek are shared with the MFC app and the
; original WinDV, so this script must never remove them on uninstall.

#ifndef AppVersion
  #error Pass /DAppVersion=...
#endif
#ifndef FileVersion
  #define FileVersion AppVersion
#endif
#ifndef Arch
  #error Pass /DArch=x64 or /DArch=x86
#endif
#ifndef AppDir
  #error Pass /DAppDir=... (the WinUI app's output folder)
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
; One AppId for both architectures: installing one replaces the other.
AppId={{9D543489-B23F-4B57-ABCD-CA1B11302AF5}
; "WinDV 2", so the Start menu and Settings > Apps tell it apart from an
; installed original WinDV (winget's PetrMourek.WinDV is also called "WinDV").
AppName=WinDV 2
AppVersion={#AppVersion}
AppPublisher=Makoto
AppPublisherURL=https://github.com/mak0t0san/WinDV2
AppSupportURL=https://github.com/mak0t0san/WinDV2/issues
AppUpdatesURL=https://github.com/mak0t0san/WinDV2/releases
AppCopyright=Copyright (c) 2026 Makoto. Original WinDV (c) 2002-2003 Petr Mourek.
VersionInfoVersion={#FileVersion}
DefaultDirName={autopf}\WinDV
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=10.0.19041
#if Arch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
CloseApplications=yes
WizardStyle=modern
SetupIconFile=..\ui\Assets\WinDV.ico
UninstallDisplayIcon={app}\WinDV.exe
UninstallDisplayName=WinDV 2
OutputDir={#OutputDir}
OutputBaseFilename=WinDV-{#AppVersion}-{#Arch}-Setup
Compression=lzma2/max
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; Up to 2.2.0 the app shipped with the .NET runtime (not Native AOT): ~390 files the
; current app doesn't use. Remove them when upgrading such an install, which
; WinDV.runtimeconfig.json identifies. It is deleted last, because the Check runs
; again for each entry.
[InstallDelete]
Type: files; Name: "{app}\*.dll"; Check: IsDotNetRuntimeInstall
Type: files; Name: "{app}\*.xbf"; Check: IsDotNetRuntimeInstall
Type: files; Name: "{app}\createdump.exe"; Check: IsDotNetRuntimeInstall
Type: filesandordirs; Name: "{app}\Views"; Check: IsDotNetRuntimeInstall
; The ~85 WinUI language folders (de-DE, sr-Latn-RS, ...); only en-us is reinstalled.
Type: filesandordirs; Name: "{app}\*-*"; Check: IsDotNetRuntimeInstall
Type: files; Name: "{app}\WinDV.deps.json"; Check: IsDotNetRuntimeInstall
Type: files; Name: "{app}\WinDV.runtimeconfig.json"; Check: IsDotNetRuntimeInstall

[Files]
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\WinDV 2"; Filename: "{app}\WinDV.exe"
Name: "{autodesktop}\WinDV 2"; Filename: "{app}\WinDV.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\WinDV.exe"; Description: "{cm:LaunchProgram,WinDV 2}"; Flags: nowait postinstall skipifsilent

[Code]
function IsDotNetRuntimeInstall: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\WinDV.runtimeconfig.json'));
end;
