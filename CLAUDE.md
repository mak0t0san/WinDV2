# CLAUDE.md

WinDV 1.2.3 — DV capture/record over FireWire. Visual C++ 6-era MFC + DirectShow code,
retargeted to build with Visual Studio 2026. See [README.md](README.md) for the full
picture; this file covers what is easy to get wrong.

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /p:Configuration=Release /p:Platform=Win32
```

Output: `Win32\Release\WinDV.exe`. `baseclasses` builds first as a static lib.

`vcvarsall.bat` is broken on this machine (fails looking for `vswhere.exe` and leaves
`cl` off PATH). MSBuild does not need it — use MSBuild directly rather than trying to
fix the environment.

## Hard constraints

Changing any of these breaks the build in non-obvious ways:

- **Win32/x86 only.** No x64 configuration exists. The code is full of VC6-era
  assumptions about pointer and `long` width; an x64 port is real work, not a flag.
- **MBCS, not Unicode.** `CharacterSet=MultiByte`. The source uses `char`/`LPCSTR`
  throughout with no `_T()` wrapping, so flipping to Unicode breaks it everywhere.
- **Static MFC** (`UseOfMfc=Static`, `/MT`). Keeps the exe standalone. `baseclasses`
  must use the matching CRT or the link fails.
- **`PlatformToolset` is `v145`**, the VS 2026 toolset. Not `v180` — that is only the
  name of the `MSBuild\Microsoft\VC\v180` directory and is not a valid toolset value.
- **`GenerateManifest=false`.** `WinDV.rc` embeds `WinDV.exe.manifest` at resource
  ID 1, the same ID MSBuild's generated manifest uses. Re-enabling it gives
  `CVT1100: duplicate resource` at link time.

## MFC availability

Building needs `Microsoft.VisualStudio.Component.VC.ATLMFC` (MFC for the *latest*
toolset), which installs into `VC\Tools\MSVC\14.51.x\atlmfc\`.

Do not conclude MFC is present just because some `atlmfc\include\afxwin.h` exists. The
separate `VC.14.44.17.14.MFC` component creates a fully populated `14.44.35207\atlmfc\`
with **no compiler in that toolset at all** — no `bin\` directory. Check for
`VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe` before trusting a toolset.

If a VS install is needed: `vs_installer.exe ... --passive` fails with **exit code
5007 and no UAC prompt** unless the launching process is already elevated. Use
`Start-Process -Verb RunAs`.

## Architecture

`DShow.h` is the heart of it. Two interfaces: `CFrameSource` produces DV frames,
`CFrameHandler` consumes them, with `CDVQueue` (ring buffer) decoupling the threads.

Sources and sinks are *custom DirectShow filters* — `CInputGraph` wraps a
`CBaseInputPin`, `COutputGraph` a `CBaseOutputPin` — so the app handles raw DV frames
itself instead of letting the graph do the work end to end.

- Sources: `CDVInput` (camcorder), `CAVIReader`, `CAVIJoiner` (concatenates files)
- Sinks: `CDVOutput` (to tape), `CAVIWriter`, `CMonitor` (preview)
- `CDV` (a `CStatic`) owns the pipeline, the `Idle`/`Capturing`/`Recording` state
  machine, and the worker threads. `CDVToolsDlg` is the UI on top.
- `DV.cpp` parses raw DV pack data for the camcorder's recording timestamp.

Errors surface as `CDShowException` thrown by `CHECK_HR`, caught in `CDVToolsDlg` and
written to the status bar via `Exception2Status`.

## Gotchas when testing

- **DV capture is exclusive.** Only one process can hold the camcorder. If another
  WinDV (e.g. an installed copy) is capturing, a second instance shows `Error: Error`
  in its status bar — that is `BuildCapturing` failing, and it is correct behavior,
  not a regression. Check for other `WinDV.exe` processes before investigating.
- `Error: Error` is just the default message string in `CHECK_HR`; it carries no
  information about what actually failed. Trace the specific `CHECK_HR` call instead.
- The camcorder must be in **VTR/tape mode**. In stills mode Windows loads the
  still-image driver and it never appears as a DirectShow capture device.

## Conventions

- `external/baseclasses/` is vendored Microsoft sample code, MIT licensed. It compiles
  clean as-is. Do not modify it — if something seems to need a fix there, the problem
  is almost certainly in how it is being used.
- The original VC6 `.dsp`/`.dsw`/`.clw` are kept for reference. They are not the build
  system; `WinDV.sln` is.
- `CppProperties.json` is a leftover from VS "Open Folder" mode and declares
  `UNICODE`/`_UNICODE`, which contradicts the actual MBCS build. It does not affect
  MSBuild. Ignore it, or treat the `.sln` as authoritative if they disagree.
- This is 2002-era MFC. Match the surrounding style when editing rather than
  modernizing it; keep changes minimal and local.
