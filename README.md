# WinDV 1.2.3

A small Windows tool for moving DV video between a camcorder and disk over FireWire
(IEEE 1394):

- **Capture** — pull DV from a camcorder and write type-1 or type-2 AVI files, with
  automatic scene splitting on timecode discontinuities and date/time-based filenames.
- **Record** — push AVI files back out to DV tape, optionally concatenating several
  files into one continuous recording.

Original program by Petr Mourek (2002–2003), <http://windv.mourek.cz>.

This repository is that 1.2.3 source, updated to build with a current toolchain. The
program itself is unchanged apart from one 64-bit correctness fix (see
[Changes from the original](#changes-from-the-original)).

## Requirements

- Windows 10 or 11
- **Visual Studio 2026** with these components:
  - *Desktop development with C++*
  - **C++ MFC for latest v145 build tools (x86 & x64)** — this one is easy to miss and
    is not part of the default C++ workload. Without it the build fails at `afxwin.h`.
  - Windows 11 SDK (10.0.26100 or similar)

No DirectX SDK is required. The DirectShow base classes are vendored in this repo —
see [`external/baseclasses/`](external/baseclasses/).

## Building

Open [`WinDV.sln`](WinDV.sln) in Visual Studio and build, or from a shell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /p:Configuration=Release /p:Platform=Win32
```

The solution builds `baseclasses` first, then `WinDV`. Output:

```
Win32\Release\WinDV.exe
```

**Win32/x86 only.** The original is 32-bit MBCS code and has not been ported to x64.

The executable links MFC and the CRT statically, so it runs on a clean machine with no
redistributable installed — its only dependencies are stock OS DLLs.

## Running

`WinDV.exe` needs no installation. Connect a DV camcorder over FireWire, set it to
**VTR/VCR (tape) mode**, and it appears in the *Video source* dropdown.

> **DirectShow DV capture is exclusive.** Only one process can hold the camcorder at a
> time. If another copy of WinDV (or any other capture program) is already using the
> device, this one shows `Error: Error` in its status bar as graph construction fails.
> That is the expected failure, not a bug — close the other program first.

If the camcorder is in stills/photo mode, Windows loads its still-image driver instead
of the DV one and it will not show up as a capture device.

## Repository layout

```
WinDV.sln                  Solution: baseclasses + WinDV
WinDV.vcxproj              Application project (Win32, MBCS, static MFC, v145)
WinDV.cpp / WinDV.h        CWinApp entry point
DVToolsDlg.cpp / .h        Main dialog: tabs, status, command-line handling
DShow.cpp / DShow.h        DirectShow layer (see below)
DV.cpp / DV.h              Raw DV frame parsing (recording date from DV pack data)
CaptureCfg, RecordCfg      Settings dialogs
VideoDeviceSel             Device picker dialog
ToolTab, DropFilesEdit     UI helpers (tab control, drag-and-drop edit box)
WinDV.rc, Resource.h       Resources; embeds WinDV.exe.manifest at ID 1
res/                       Icons
external/baseclasses/      Vendored DirectShow base classes (see below)
WinDV.dsp / .dsw / .clw    Original Visual C++ 6 project files, kept for reference
```

### The DirectShow layer

`DShow.h` is built around two interfaces — `CFrameSource` produces DV frames,
`CFrameHandler` consumes them — with a `CDVQueue` ring buffer between them so capture
and disk I/O run on separate threads.

Sources and sinks are custom DirectShow filters (`CInputGraph` wraps a `CBaseInputPin`,
`COutputGraph` a `CBaseOutputPin`) that let the application see raw DV frames rather
than letting the graph handle everything internally:

| Sources (`CFrameSource`)      | Sinks (`CFrameHandler`)          |
| ----------------------------- | -------------------------------- |
| `CDVInput` — camcorder        | `CDVOutput` — out to DV tape     |
| `CAVIReader` — one AVI file   | `CAVIWriter` — out to an AVI     |
| `CAVIJoiner` — several AVIs   | `CMonitor` — on-screen preview   |

`CDV` (a `CStatic` subclass) owns the pipeline, holds the state machine
(`Idle`/`Capturing`/`Recording`/…), and runs the capture and record worker threads.

## Changes from the original

The VC6-era source needed very little to build on a 2026 toolchain:

- **`DVToolsDlg.cpp` — one real fix.** `OnDVTimeChange` passed `&lParam` straight to
  `localtime()`. That worked when `time_t` was 32 bits, but it is 64 bits now while
  `LPARAM` is still 32 bits on Win32, so the value is copied into a real `time_t`
  before use. A null check on `localtime()` was added alongside it, since a bad
  timestamp would otherwise crash `strftime`.
- **Build system.** The VC6 `.dsp`/`.dsw` were replaced with `WinDV.sln` /
  `WinDV.vcxproj`. `largeint.lib` and `olepro32.lib` were dropped from the link line
  (neither ships in the modern SDK) and `strmiids.lib` added.
- **Manifest.** `WinDV.rc` already embeds `WinDV.exe.manifest` as resource ID 1, which
  collides with the manifest MSBuild generates by default (`CVT1100: duplicate
  resource`), so generation is disabled and the original manifest kept.

No changes were needed to the DirectShow base classes.

## Vendored dependency

`StdAfx.h` includes `<streams.h>`, the DirectShow base classes. These never shipped in
the Windows SDK — they came from the DirectX SDK samples, which are long discontinued.
(Confusingly, the Windows SDK *does* still ship a prebuilt `strmbase.lib`, just not its
headers.)

[`external/baseclasses/`](external/baseclasses/) is therefore a copy of the base class
sources from Microsoft's
[Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/multimedia/directshow/baseclasses)
repository, built from source as a static library so headers and binary always match.
Only a project file was added; no source file was modified. That code is Microsoft's
and is MIT licensed — see the upstream repository for its terms.

## License

The vendored base classes under `external/baseclasses/` are MIT licensed by Microsoft.

WinDV itself was distributed by Petr Mourek as freeware with source available; the
original release stated no formal license text. Check with the original author before
redistributing.
