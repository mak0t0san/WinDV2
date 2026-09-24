# CLAUDE.md

WinDV 1.2.3: DV capture/record over FireWire. Originally Visual C++ 6 MFC + DirectShow,
now a Unicode C++20 app for Win32 and x64 built with Visual Studio 2026. See
[README.md](README.md) for the full picture; this file covers what is easy to get wrong.

## Build and test

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /p:Configuration=Release /p:Platform=x64      # or Win32; Debug
x64\Release\WinDV.Tests.exe
```

Output: `<Platform>\<Configuration>\WinDV.exe` and `WinDV.Tests.exe`. Build order:
`baseclasses` and `WinDVCore` (static libs), then `WinDV` and `WinDV.Tests`.

Before calling a change done, build **both platforms**: x64-only breakage (pointer
casts, `UINT_PTR`, `OAHWND`) is invisible on Win32 and vice versa. Warnings are errors.

`vcvarsall.bat` is broken on this machine (fails looking for `vswhere.exe` and leaves
`cl` off PATH). MSBuild does not need it, so use MSBuild directly. git is not on PATH
either: use `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.

## Hard constraints

Changing any of these breaks the build in non-obvious ways:

- **No `/permissive-` on WinDV.** The vendored `<streams.h>` fails under it (C4596,
  `CAggDirectDraw` destructor). WinDV uses individual `/Zc:` switches instead.
  `WinDVCore` and `WinDV.Tests` include no DirectShow and do use `/permissive-`.
- **Static MFC** (`UseOfMfc=Static`, `/MT`). All four projects must use the matching
  static CRT, or the link fails.
- **`PlatformToolset` is `v145`**, the VS 2026 toolset. Not `v180`: that is only the
  name of the `MSBuild\Microsoft\VC\v180` directory and is not a valid toolset value.
- **`GenerateManifest=false`.** `WinDV.rc` embeds `WinDV.exe.manifest` at resource
  ID 1, the same ID MSBuild's generated manifest uses. Re-enabling it gives
  `CVT1100: duplicate resource` at link time. The manifest uses
  `processorArchitecture="*"` so one file serves both platforms.
- **doctest needs `DOCTEST_CONFIG_USE_STD_HEADERS`** (set in `WinDV.Tests.vcxproj`).
  Without it doctest forward-declares `std::tuple`, which MSVC 14.5x rejects (C5285).
- **Source files are ASCII.** There is no `/utf-8`, because `WinDV.rc` relies on code
  pages 1250/1252. Non-ASCII characters in `.cpp`/`.h`, even in comments, are read as
  ANSI.

## MFC availability

Building needs `Microsoft.VisualStudio.Component.VC.ATLMFC` (MFC for the *latest*
toolset), which installs into `VC\Tools\MSVC\14.51.x\atlmfc\`. The x64 build needs the
x64 MFC libraries from the same component.

Do not conclude MFC is present just because some `atlmfc\include\afxwin.h` exists. The
separate `VC.14.44.17.14.MFC` component creates a fully populated `14.44.35207\atlmfc\`
with **no compiler in that toolset at all** (no `bin\` directory). Check for
`VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe` before trusting a toolset.

If a VS install is needed: `vs_installer.exe ... --passive` fails with **exit code
5007 and no UAC prompt** unless the launching process is already elevated. Use
`Start-Process -Verb RunAs`.

## Architecture

- `core/` (**WinDVCore**) holds standard C++ only, with no MFC, DirectShow or
  `windows.h` types in its headers. It contains DV timestamp parsing (`DVTimecode`),
  capture file numbering (`CaptureNaming`), safe `strftime` (`TimeFormat`), argv
  parsing (`CommandLine`) and the `FrameQueue` ring buffer. Anything testable without
  hardware belongs here, with tests in `tests/`.
- `DShow.h` is the heart of the app. `CFrameSource` produces DV frames and
  `CFrameHandler` consumes them (`HandleFrame(duration, span)`, `EndOfStream()`,
  `SourceError()`), with a `windv::FrameQueue` decoupling the threads.
- Sources and sinks are *custom DirectShow filters*: `CInputGraph` wraps a
  `CBaseInputPin`, `COutputGraph` a `CBaseOutputPin`.
  - Sources: `CDVInput` (camcorder), `CAVIReader`, `CAVIJoiner` (concatenates files on
    a helper thread)
  - Sinks: `CDVOutput` (to tape), `CAVIWriter`, `CMonitor` (preview; optional, and
    capture works without it)
- `CDV` (a `CStatic`) owns the pipeline through `unique_ptr`s, the atomic
  `Idle`/`Capturing`/`Recording` state machine, and one `std::jthread` worker.
  `CDVToolsDlg` is the UI on top.

Filters derive from `CBaseFilter`, which inherits `IUnknown` twice, so they cannot go
in a `CComPtr<CMyFilter>`. The pattern is a raw typed pointer plus a
`CComPtr<IBaseFilter>` holding the reference (`m_inputFilter` / `m_inputFilterRef`).

### Errors

- Throw `DShowError` via `CheckHR` (fails on anything but `S_OK`, as the original code
  did) or `CheckSucceeded` (fails only on `FAILED(hr)`). Always pass a message that
  says what was being attempted; `AMGetErrorText` output is appended automatically.
- UI thread: `CDVToolsDlg::Guarded(...)` catches, resets the pipeline and shows the
  message in the status bar.
- Worker threads and DirectShow streaming threads must not let exceptions escape.
  Workers catch and call `CDV::ReportError`, which posts `WM_DV_ERROR`. Pin callbacks
  (`Receive`, `EndOfStream`) catch everything and return an HRESULT.

### Threads

- The UI thread and workers are all in the COM MTA; each worker opens its own
  `ComApartment`.
- Never call `CWnd` methods (`GetParent()`, etc.) from a worker: MFC handle maps are
  per thread. Workers post to `CDV::m_notifyWnd` with `::PostMessage`.
- `CDV::Destroy()` order matters: set `Idle`, close the queue, stop the sources, join
  the worker, then destroy the objects. Closing the queue first is what unblocks the
  worker and any streaming thread stuck in `FrameQueue::Put`.
- `FrameQueue::Get()` data stays valid until the next `Get()`. The record loop relies
  on this to repeat the last frame while paused or finished.

## Gotchas when testing

- **DV capture is exclusive.** Only one process can hold the camcorder. This machine has
  an installed WinDV (WinGet package `PetrMourek.WinDV`) that may be running and
  holding it. A second instance then shows
  `Error: Can't connect to the DV device (is another program using it?) (0x80040217 ...)`.
  That is correct behaviour, not a regression. Check for other `WinDV.exe` processes,
  and do not kill the user's.
- WinDV reads and writes `HKCU\Software\Petr Mourek\WinDV`, **shared with the installed
  copy**. Back it up (`reg export`) before automated runs that close the app, and
  restore it afterwards.
- The camcorder must be in **VTR/tape mode**. In stills mode Windows loads the
  still-image driver and it never appears as a DirectShow capture device.
- Without hardware, drive the UI with Win32 messages (`WM_COMMAND` with a control ID)
  rather than UI Automation. MFC buttons in this dialog don't expose `InvokePattern`.

## Conventions

- `external/baseclasses/` is vendored Microsoft sample code, MIT licensed. Do not modify
  its sources. Only its `.vcxproj` is ours. If something seems to need a fix there, the
  problem is almost certainly in how it is being used. The same goes for
  `external/doctest/`.
- Formatting: `.clang-format` at the root (external/ opts out). Run VS's
  `VC\Tools\Llvm\x64\bin\clang-format.exe -i` on changed files. Include order is
  preserved deliberately: `stdafx.h` must be first in every WinDV `.cpp`.
- Registry value names are a compatibility surface. Keep them, including the
  misspelled `DiscontinuityTreshold`.
- The original VC6 `.dsp`/`.dsw`/`.clw` are kept for reference. They are not the build
  system; `WinDV.sln` is.
- `CppProperties.json` is a leftover from VS "Open Folder" mode. It does not affect
  MSBuild; treat the `.sln` as authoritative if they disagree.
