# WinDV 1.2.3

A small Windows tool for moving DV video between a camcorder and disk over FireWire
(IEEE 1394):

- **Capture**: pull DV from a camcorder and write type-1 or type-2 AVI files, with
  automatic scene splitting on timecode discontinuities and date/time-based filenames.
- **Record**: push AVI files back out to DV tape, optionally concatenating several
  files into one continuous recording.

Original program by Petr Mourek (2002–2003), <http://windv.mourek.cz>.

This repository is that 1.2.3 source, modernized: it builds with Visual Studio 2026 as
a Unicode C++20 application for both x86 and x64, and a number of long-standing bugs
are fixed (see [Changes from the original](#changes-from-the-original)). Behaviour,
file naming and registry settings are unchanged.

## Requirements

- Windows 10 or 11
- **Visual Studio 2026** with these components:
  - *Desktop development with C++*
  - **C++ MFC for latest v145 build tools (x86 & x64)**. This one is easy to miss and
    is not part of the default C++ workload. Without it the build fails at `afxwin.h`.
  - Windows 11 SDK (10.0.26100 or similar)

No DirectX SDK is required. The DirectShow base classes are vendored in this repo;
see [`external/baseclasses/`](external/baseclasses/).

## Building

Open [`WinDV.sln`](WinDV.sln) in Visual Studio and build, or from a shell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /p:Configuration=Release /p:Platform=x64
```

`Platform` is `Win32` or `x64`; `Configuration` is `Debug` or `Release`. Output goes to
`<Platform>\<Configuration>\`, e.g. `x64\Release\WinDV.exe`.

The build is warning-free at `/W4` and treats warnings as errors. The executable links
MFC and the CRT statically, so it runs on a clean machine with no redistributable
installed; its only dependencies are stock OS DLLs.

## Tests

The solution also builds `WinDV.Tests.exe` next to `WinDV.exe`. It covers the logic
that doesn't need a camcorder: DV timestamp decoding, capture file numbering,
date/time format validation, command-line parsing, and the frame queue.

```powershell
x64\Release\WinDV.Tests.exe
```

The DirectShow and UI code still needs a real device to test: capture type-1 and
type-2 AVIs, check the scene split on a timecode gap, record back to tape, and try
DV transport control on and off.

## Running

`WinDV.exe` needs no installation. Connect a DV camcorder over FireWire, set it to
**VTR/VCR (tape) mode**, and it appears in the *Video source* picker.

> **DirectShow DV capture is exclusive.** Only one process can hold the camcorder at a
> time. If another copy of WinDV (or any other capture program) is already using the
> device, the status bar shows
> `Error: Can't connect to the DV device (is another program using it?)`.
> Close the other program first.

If the camcorder is in stills/photo mode, Windows loads its still-image driver instead
of the DV one and it will not show up as a capture device.

### Command line

```
WinDV capture [-exit] [[HH:]MI:]SS[.ss] <file>
WinDV record  [-exit] <file> [<file> ...]
```

A capture duration of `0` captures until stopped. `-exit` closes WinDV when the
capture or recording finishes. Quote paths that contain spaces.

## Repository layout

```
WinDV.sln                    Solution: baseclasses, WinDVCore, WinDV, WinDV.Tests
app/                         The WinDV application (one project, sources and headers together)
  WinDV.vcxproj              Application project (Win32/x64, Unicode, static MFC, v145)
  WinDV.cpp / WinDV.h        CWinApp entry point, file dialog helper
  DVToolsDlg.cpp / .h        Main dialog: tabs, status, command-line handling
  DShow.cpp / DShow.h        DirectShow layer (see below)
  CaptureCfg, RecordCfg      Settings pages
  VideoDeviceSel             Device picker dialog
  ToolTab, DropFilesEdit     UI helpers (tab control, drag-and-drop edit box)
  WinDV.rc, Resource.h       Resources; embeds WinDV.exe.manifest at ID 1
  res/                       Icons
core/                        WinDVCore: standard C++ logic with no MFC or DirectShow
tests/                       WinDV.Tests: doctest unit tests for core/
external/baseclasses/        Vendored DirectShow base classes (MIT, Microsoft)
external/doctest/            Vendored doctest 2.4.12 (MIT)
legacy/                      Original VC6 WinDV.dsp/.dsw/.clw and a stale CppProperties.json,
                             kept for reference only; not part of the build
```

Each project folder keeps its `.cpp` and `.h` files side by side. Build output goes to
`<Platform>\<Configuration>\` at the repository root for every project.

### The DirectShow layer

`DShow.h` is built around two interfaces: `CFrameSource` produces DV frames and
`CFrameHandler` consumes them. A `windv::FrameQueue` ring buffer sits between them so
capture and disk I/O run on separate threads.

Sources and sinks are custom DirectShow filters (`CInputGraph` wraps a `CBaseInputPin`,
`COutputGraph` a `CBaseOutputPin`). These let the application see raw DV frames rather
than letting the graph handle everything internally:

| Sources (`CFrameSource`)      | Sinks (`CFrameHandler`)          |
| ----------------------------- | -------------------------------- |
| `CDVInput`: camcorder         | `CDVOutput`: out to DV tape      |
| `CAVIReader`: one AVI file    | `CAVIWriter`: out to an AVI      |
| `CAVIJoiner`: several AVIs    | `CMonitor`: on-screen preview    |

`CDV` (a `CStatic` subclass) owns the pipeline, holds the state machine
(`Idle`/`Capturing`/`Recording`/…), and runs the capture and record worker threads.

Errors are thrown as `DShowError`, which carries the `HRESULT` and a message saying
what failed. Errors on the UI thread are caught in `CDVToolsDlg`. Errors on worker
threads are stored in `CDV` and posted to the dialog as `WM_DV_ERROR`. Either way they
end up in the status bar.

## Changes from the original

### Bugs fixed

- The device picker listed only every other device (a loop incremented its index twice).
- An exception on a worker thread terminated the process. This happened when a new
  capture file could not be created (full disk, bad path) or when the next file of a
  multi-file recording failed to open. These errors now appear in the status bar.
- Typing an invalid `%` code into the date/time format box in the settings passed it
  straight to `strftime`. The Universal CRT treats that as an invalid parameter and
  terminates the program. Formats are now validated.
- Closing or switching tabs could hang forever if the preview never received a buffer.
- COM objects leaked on every error path. Several unchecked HRESULTs could dereference
  null pointers, for example opening a file that isn't an AVI.
- Paths longer than 255 characters were silently truncated. Command-line paths with
  spaces were split apart.
- If renaming the finished capture from its temporary `~` name failed, the file was
  left behind with no warning. The failure is now reported.
- Picking a capture file inside a folder whose name contains a dot cut the path at
  that dot.
- A stale "selected tab" registry value caused undefined behaviour at startup.
- The DV recording timestamp was truncated from `time_t` to `int`. Garbage subcode
  data was fed to `mktime` without checks.
- State shared between the UI, capture and DirectShow threads had no
  synchronization. It now uses atomics and mutexes, and the frame queue uses condition
  variables.
- `EveryNth = 0` in the registry caused a division by zero.
- Every error used to read `Error: Error`. Messages now say what failed and include the
  DirectShow error text.

### Modernization

- Unicode build (non-ASCII file names work) and a new x64 configuration.
- C++20, `/W4` with warnings as errors, `/sdl`, Control Flow Guard, and individual
  `/Zc` conformance switches. `/permissive-` stays off because the vendored `streams.h`
  does not compile under it.
- `CComPtr` for all COM references, `std::unique_ptr` ownership, `std::jthread`
  workers, and `std::span` for frame buffers.
- Pure logic moved to `core/` (WinDVCore) with unit tests.
- ClassWizard markers and VC6 boilerplate removed. Sources are formatted with
  `.clang-format`.
- WinDV keeps the machine awake while capturing or recording, not just the display.

### Unchanged on purpose

- Registry settings live under the same key (`HKCU\Software\Petr Mourek\WinDV`) with the
  same value names, so an existing configuration carries over. That includes the
  historical misspelling `DiscontinuityTreshold`.
- Capture file naming, the command-line syntax, and the dialog layout.
- The vendored DirectShow base classes. Only their project file changed, to add x64
  and switch to Unicode.

## Vendored dependencies

`StdAfx.h` includes `<streams.h>`, the DirectShow base classes. These never shipped in
the Windows SDK; they came from the DirectX SDK samples, which are long discontinued.
(Confusingly, the Windows SDK *does* still ship a prebuilt `strmbase.lib`, just not its
headers.)

[`external/baseclasses/`](external/baseclasses/) is therefore a copy of the base class
sources from Microsoft's
[Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/multimedia/directshow/baseclasses)
repository, built from source as a static library so headers and binary always match.
Only a project file was added; no source file was modified.

[`external/doctest/`](external/doctest/) is the single-header
[doctest](https://github.com/doctest/doctest) 2.4.12 test framework, used only by
`WinDV.Tests`.

## License

The vendored base classes under `external/baseclasses/` are MIT licensed by Microsoft.
doctest under `external/doctest/` is MIT licensed by Viktor Kirilov.

WinDV itself was distributed by Petr Mourek as freeware with source available; the
original release stated no formal license text. Check with the original author before
redistributing.
