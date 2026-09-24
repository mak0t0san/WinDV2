# WinDV 2

[![Build](https://github.com/mak0t0san/WinDV2/actions/workflows/build.yml/badge.svg)](https://github.com/mak0t0san/WinDV2/actions/workflows/build.yml)

A small Windows tool for moving DV video between a camcorder and disk over FireWire
(IEEE 1394):

- **Capture**: pull DV from a camcorder and write type-1 or type-2 AVI files, with
  automatic scene splitting on timecode discontinuities and date/time-based filenames.
- **Record**: push AVI files back out to DV tape, optionally concatenating several
  files into one continuous recording.

WinDV 2 is by Makoto, <https://github.com/mak0t0san/WinDV2>.
**[Download the latest release](https://github.com/mak0t0san/WinDV2/releases/latest)**:
unzip it and run `WinDV.exe`. Nothing needs installing.

## Thanks

WinDV was written by **Petr Mourek** (2002–2003, <http://windv.mourek.cz>), and for two
decades it has been the tool people reach for to get their DV tapes onto a computer.
He published its source so that others could build on it, and WinDV 2 is built on it:
his DV capture engine is still at the heart of this version. Thank you, Petr.

## What's new in 2.0

WinDV 2 starts from the 1.2.3 source and modernizes it. It builds with Visual Studio
2026 as a Unicode C++20 application for both x86 and x64, and a number of long-standing
bugs are fixed (see [Changes from the original](#changes-from-the-original)). File
naming and registry settings are unchanged, so an existing WinDV configuration carries
over.

There are two front ends over the same DirectShow engine:

- **The new WinUI 3 app** (`ui/`, C#). It has a Windows 11 look (Mica, Fluent controls,
  light and dark themes) and VCR-style transport controls: Rewind, Play, Pause, Stop,
  Fast-forward, plus a separate red **REC** button that writes AVI files while the tape
  runs.
- **The original MFC dialog** (`app/`). It is kept, with its behaviour unchanged, until the
  new app has been tested with a camcorder.

## Requirements

- Windows 10 or 11
- **Visual Studio 2026** with these components:
  - *Desktop development with C++*
  - **C++ MFC for latest v145 build tools (x86 & x64)**. This one is easy to miss and
    is not part of the default C++ workload. Without it the build fails at `afxwin.h`.
  - Windows 11 SDK (10.0.26100 or similar)
  - *.NET desktop development* or just the **.NET 10 SDK**, for the WinUI app. The
    Windows App SDK comes from NuGet at restore time, so the first build needs network
    access.

No DirectX SDK is required. The DirectShow base classes are vendored in this repo;
see [`external/baseclasses/`](external/baseclasses/).

## Building

Open [`WinDV.sln`](WinDV.sln) in Visual Studio and build, or from a shell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /restore /p:Configuration=Release /p:Platform=x64
```

`Platform` is `Win32` or `x64`; `Configuration` is `Debug` or `Release`. (For the C#
project the solution maps `Win32` to `x86`.) Output:

| What | Where |
| --- | --- |
| New WinUI app (run this folder as is) | `ui\bin\<x64\|x86>\<Configuration>\net10.0-windows10.0.26100.0\win-<x64\|x86>\WinDV.exe` |
| Engine DLL used by it | `<Platform>\<Configuration>\WinDV.Native.dll`, copied next to the app |
| Original MFC app | `<Platform>\<Configuration>\WinDV.exe` |
| Tests | `<Platform>\<Configuration>\WinDV.Tests.exe` |

The C++ code is warning-free at `/W4` and the C# at the default level; both treat
warnings as errors. Everything links the CRT statically, and the WinUI app is
self-contained (.NET and the Windows App SDK runtime are in its folder), so neither
app needs anything installed.

## Tests

The solution also builds `WinDV.Tests.exe` next to `WinDV.exe`. It covers the logic
that doesn't need a camcorder: DV timestamp decoding, capture file numbering,
date/time format validation, command-line parsing, the frame queue, and what each
transport button asks the deck to do.

```powershell
x64\Release\WinDV.Tests.exe
```

The DirectShow and UI code still needs a real device to test: capture type-1 and
type-2 AVIs, check the scene split on a timecode gap, record back to tape, and try
DV transport control on and off.

## Running

`WinDV.exe` needs no installation. Connect a DV camcorder over FireWire, set it to
**VTR/VCR (tape) mode**, and it appears in the *Camcorder* list.

In the WinUI app:

- **Capture from tape.** The transport buttons drive the camcorder, and the preview
  follows the tape. Fast-forward and Rewind wind a stopped tape, and cue (search with a
  picture) while it is playing or paused. **REC** starts and stops writing AVI files.
  With *Let WinDV run the tape* turned on in Settings, REC also starts the tape.
- **Record to tape.** Choose or drop AVI files, then press Play. Pause holds the current
  frame on the output, and Stop ends the recording.
- Keyboard: Ctrl+Space play, Ctrl+P pause, Esc stop, Ctrl+Left/Right rewind and
  fast-forward, Ctrl+R REC.

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
WinDV.sln                    Solution: all projects below
ui/                          WinDV.UI: the WinUI 3 app (C#, .NET 10, unpackaged, self-contained)
  Interop/                   P/Invoke over windv_api.h, and the DVEngine wrapper
  ViewModels/MainViewModel   Transport commands, status polling, pipeline lifecycle
  Views/                     Settings screen, About dialog
  Services/SettingsStore     Registry settings (shared with the MFC app)
native/                      WinDV.Native: the engine as a DLL with a flat C API (windv_api.h)
engine/                      WinDVEngine: the DirectShow engine (Win32 + ATL, no MFC)
app/                         The original MFC application
  DVToolsDlg.cpp / .h        Main dialog: tabs, status, command-line handling
  DVView.cpp / .h            CDV: the preview control, wrapping DVEngine
  CaptureCfg, RecordCfg      Settings pages
  VideoDeviceSel             Device picker dialog
  ToolTab, DropFilesEdit     UI helpers (tab control, drag-and-drop edit box)
  WinDV.rc, Resource.h       Resources; embeds WinDV.exe.manifest at ID 1
core/                        WinDVCore: standard C++ logic with no MFC or DirectShow
tests/                       WinDV.Tests: doctest unit tests for core/
external/baseclasses/        Vendored DirectShow base classes (MIT, Microsoft)
external/doctest/            Vendored doctest 2.4.12 (MIT)
legacy/                      Original VC6 WinDV.dsp/.dsw/.clw and a stale CppProperties.json,
                             kept for reference only; not part of the build
```

Each project folder keeps its `.cpp` and `.h` files side by side. Build output goes to
`<Platform>\<Configuration>\` at the repository root for every project.

### The DirectShow engine

`engine/` has one file pair per piece: `FrameInterfaces.h`, `FilterGraph`,
`InputGraph`, `OutputGraph`, `DVDevice` (enumeration, transport, camcorder in and out),
`AviSource`, `AviWriter`, `Monitor` and `DVEngine` (the controller).

It is built around two interfaces: `CFrameSource` produces DV frames and
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

`DVEngine` owns the pipeline, holds the state machine
(`Idle`/`Capturing`/`Recording`/…), runs the capture and record worker threads, and
sends deck transport commands (`DVTransport`; the button logic is in
`core/TransportLogic`). It reports to a `DVEngineEvents` sink. The MFC app's `CDV`
turns those events into window messages. `WinDV.Native` passes them to the C# app as
callbacks.

`WinDV.Native` runs the engine on a thread of its own in the COM multithreaded
apartment and forwards every API call to it, because the WinUI UI thread is
single-threaded. The preview is a plain Win32 child window that the C# app positions
over a placeholder in the layout. XAML can't draw over it, so it is hidden while
settings or dialogs are showing.

Errors are thrown as `DShowError`, which carries the `HRESULT` and a message saying
what failed. Errors on worker threads are stored in `DVEngine`, and the front end is
notified. Each UI shows them in its own way: the MFC status bar, or an InfoBar in the
WinUI app.

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

- Registry settings live under the same key (`HKCU\Software\Petr Mourek\WinDV 1.2`,
  named after the app title "WinDV 1.2") with the same value names, so an existing
  configuration carries over. Both front ends share it. That includes the historical
  misspelling `DiscontinuityTreshold`.
- Capture file naming, the command-line syntax, and the MFC dialog layout.
- The vendored DirectShow base classes. Only their project file changed, to add x64
  and switch to Unicode.

## Vendored dependencies

The engine (`engine/DShowBase.h`) and the MFC app (`app/StdAfx.h`) include
`<streams.h>`, the DirectShow base classes. These never shipped in
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

The original WinDV was distributed by Petr Mourek as freeware with its source. His
site says of the code: "You can use the code without any restrictions. It would be nice
if you mention the origin." This project does so, gratefully (see [Thanks](#thanks)).
