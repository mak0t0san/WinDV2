# CLAUDE.md

WinDV 2 (by Makoto, <https://github.com/mak0t0san/WinDV2>; based on Petr Mourek's WinDV
1.2.3): DV capture/record over FireWire. Originally Visual C++ 6 MFC + DirectShow,
now a C++20 DirectShow engine with two front ends: a C# WinUI 3 app (`ui/`, the new UI)
and the original MFC dialog (`app/`, kept until the new app is hardware-tested). Win32
and x64, Visual Studio 2026. See [README.md](README.md) for the full picture; this file
covers what is easy to get wrong.

## Build and test

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  WinDV.sln /restore /p:Configuration=Release /p:Platform=x64      # or Win32; Debug
x64\Release\WinDV.Tests.exe
```

Outputs: `<Platform>\<Configuration>\` holds the MFC `WinDV.exe`, `WinDV.Tests.exe` and
`WinDV.Native.dll`. The WinUI app is in
`ui\bin\<x64|x86>\<Configuration>\net10.0-windows10.0.26100.0\win-<x64|x86>\WinDV.exe`,
with the DLL copied next to it. Build order: `baseclasses`, `WinDVCore` and `WinDVEngine`
(static libs), then `WinDV`, `WinDV.Native` and `WinDV.Tests`, then `WinDV.UI`.
`WinDVLauncher` (`launcher/`) has no dependencies.

Release packaging is `build\package.ps1 -Arch x64|x86` (CI uses it too). It writes an Inno
Setup installer (`installer/WinDV.iss`) and a portable zip to `dist\`. It ships a
**Native AOT publish** of `ui/` (`PublishAot`), not the build output, so the UI must stay
trim/AOT-safe: `x:Bind` rather than `{Binding}`, `LibraryImport`, no reflection. Keep
`EnableMsixTooling=true`, because without it the publish silently drops `WinDV.pri`
and the app crashes on start. The AOT linker finds the C++ tools through `vswhere.exe`
on PATH, which this machine doesn't have, so `package.ps1` adds it. The zip's top level
holds only `WinDVLauncher.exe` renamed to `WinDV.exe`, plus README. The app stays in
`app\`, because it must sit next to its runtime DLLs. The installer's uninstall must never
remove `HKCU\Software\Petr Mourek` (see Gotchas).

Layout: one folder per project, with `.cpp` and `.h` side by side (no src/include split).
The folders are `ui/` (WinDV.UI, C#), `native/` (WinDV.Native DLL), `engine/`
(WinDVEngine), `app/` (MFC WinDV), `core/`, `tests/`, `launcher/` (the portable zip's
stub exe), `installer/`, `build/`, `external/` (vendored) and `legacy/` (VC6 files, not built). The vcxproj files reach the rest of the repo through
a `$(RepoRoot)` property, so use that rather than `$(ProjectDir)` or `$(SolutionDir)`
for paths outside the project. `$(SolutionDir)` is wrong when a project is built on its
own. `app/` and `native/` add `engine/` and `core/` to their include paths.

The solution's `Win32` platform maps to `x86` for the C# project, and the csproj maps
back to `Win32` (`NativePlatform`) to build and copy the right `WinDV.Native.dll`.
CI (`.github/workflows/build.yml`) builds both platforms on `windows-2025-vs2026`, runs
the tests and uploads zipped apps. Pushing a `vX.Y.Z` tag publishes a GitHub release,
using `.github/release-notes/vX.Y.Z.md` as the notes if that file exists. The version
lives in `ui/WinDV.csproj` (`<Version>`) and `app/WinDV.rc` (VERSIONINFO). A tag build
overrides it with the tag's version.

The C# project references only the WinUI components of the Windows App SDK (pinned to
the versions of the 2.5.1 meta-package), not `Microsoft.WindowsAppSDK` itself. That
keeps the AI/ML runtimes (about 70 MB) out of the self-contained output. When
upgrading, copy the component versions from the new meta-package's nuspec.

`RuntimeIdentifiers` lists both RIDs so that one restore covers both platforms.
Without that, a no-restore x86 build fails with NETSDK1047.

Before calling a change done, build **both platforms**: x64-only breakage (pointer
casts, `UINT_PTR`, `OAHWND`) is invisible on Win32 and vice versa. Warnings are errors.

`vcvarsall.bat` is broken on this machine (fails looking for `vswhere.exe` and leaves
`cl` off PATH). MSBuild does not need it, so use MSBuild directly. git is not on PATH
either: use `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.

## Hard constraints

Changing any of these breaks the build in non-obvious ways:

- **No `/permissive-` on anything that includes `<streams.h>`** (WinDV, WinDVEngine,
  WinDV.Native). It fails under that flag (C4596, `CAggDirectDraw` destructor), so those
  projects use individual `/Zc:` switches instead. `WinDVCore` and `WinDV.Tests` include
  no DirectShow and do use `/permissive-`.
- **Static CRT everywhere** (`/MT`, and static MFC in `app/`). All C++ projects must
  match, or the link fails. Debug builds must also define `DEBUG` to match baseclasses,
  because it changes the layout of their classes.
- **`<streams.h>` pulls in `edevdefs.h`, not `xprtdefs.h`.** `ED_MODE_PLAY_FASTEST_FWD`
  and `_REV` are therefore missing. `engine/DVDevice.cpp` defines them locally; don't
  include `xprtdefs.h`, because it clashes with `edevdefs.h`.
- **WinDV.Native exports go through `WinDV.Native.def`**, and every export is
  `WINDV_CALL` (`__stdcall`). Without the .def, the x86 names are decorated
  (`_windv_create@16`) and `LibraryImport` can't find them. A new export needs an entry
  in `windv_api.h`, `NativeApi.cpp`, the .def and `ui/Interop/NativeMethods.cs`. Keep
  the C# enums and structs in step with the header.
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
  capture file numbering and base names (`CaptureNaming`), safe `strftime`
  (`TimeFormat`), argv parsing (`CommandLine`), the `FrameQueue` ring buffer and the
  VCR button logic (`TransportLogic`). Anything testable without hardware belongs here,
  with tests in `tests/`.
- `engine/` (**WinDVEngine**) is the DirectShow engine: Win32 + ATL, **no MFC** (use
  `std::wstring`, not `CString`). Every file includes `DShowBase.h` first (it is the
  PCH). `CFrameSource` produces DV frames and `CFrameHandler` consumes them
  (`HandleFrame(duration, span)`, `EndOfStream()`, `SourceError()`), with a
  `windv::FrameQueue` decoupling the threads.
- Sources and sinks are *custom DirectShow filters*: `CInputGraph` wraps a
  `CBaseInputPin`, `COutputGraph` a `CBaseOutputPin`.
  - Sources: `CDVInput` (camcorder), `CAVIReader`, `CAVIJoiner` (concatenates files on
    a helper thread)
  - Sinks: `CDVOutput` (to tape), `CAVIWriter`, `CMonitor` (preview; optional, and
    capture works without it)
- `DVEngine` owns the pipeline through `unique_ptr`s, the atomic
  `Idle`/`Capturing`/`Recording` state machine, and one `std::jthread` worker. It
  reports through `DVEngineEvents` (called on worker threads). `Transport()` drives the
  deck independently of capture. `m_DVctrl` makes capture/record also move the tape, as
  the MFC app always did.
- `app/`: `CDV` (`DVView.h`) is a `CStatic` that is also the `DVEngine`, and turns its
  events into `WM_DV_*` posts. `CDVToolsDlg` is the MFC UI on top.
- `native/` (**WinDV.Native**) wraps the engine in a flat C API (`windv_api.h`) for
  `ui/`. The C# app sets `deckFollowsPipeline` (the engine's `m_DVctrl`) only on the
  record tab. On the capture tab it drives the deck itself: REC starts the tape when the
  *DVControlEnabled* setting is on.
- `ui/` (**WinDV.UI**, C#, WinUI 3, .NET 10, Windows App SDK, unpackaged and
  self-contained): `Interop/DVEngine` wraps the DLL, and `MainViewModel` holds the
  state, the commands and a 200 ms status poll. `RunAsync` runs one pipeline action at
  a time; on failure it resets the pipeline and shows the first error (as the MFC
  `Guarded` does). Use `[ObservableProperty]` on **partial properties**, not fields:
  field-based ones trigger an AOT/WinRT warning, which is an error under
  `TreatWarningsAsErrors`.

Filters derive from `CBaseFilter`, which inherits `IUnknown` twice, so they cannot go
in a `CComPtr<CMyFilter>`. The pattern is a raw typed pointer plus a
`CComPtr<IBaseFilter>` holding the reference (`m_inputFilter` / `m_inputFilterRef`).

### Errors

- Throw `DShowError` via `CheckHR` (fails on anything but `S_OK`, as the original code
  did) or `CheckSucceeded` (fails only on `FAILED(hr)`). Always pass a message that
  says what was being attempted; `AMGetErrorText` output is appended automatically.
- MFC UI thread: `CDVToolsDlg::Guarded(...)` catches, resets the pipeline and shows the
  message in the status bar.
- Across the C API, exceptions become a `windv_result` plus a message fetched with
  `windv_take_error` (`windv_engine::Call`). No exception may cross an export.
- Worker threads and DirectShow streaming threads must not let exceptions escape.
  Workers catch and call `DVEngine::ReportError`, which fires
  `DVEngineEvents::OnError`. Pin callbacks (`Receive`, `EndOfStream`) catch everything
  and return an HRESULT.

### Threads

- Everything that touches the engine is in the COM MTA: the MFC UI thread, the
  WinDV.Native engine thread and every worker (each opens its own `ComApartment`).
- Never call `CWnd` methods (`GetParent()`, etc.) from a worker: MFC handle maps are
  per thread. `CDV` posts to its `m_notifyWnd` with `::PostMessage`.
- `DVEngine::Destroy()` order matters: set `Idle`, close the queue, stop the sources,
  join the worker, then destroy the objects. Closing the queue first is what unblocks
  the worker and any streaming thread stuck in `FrameQueue::Put`. Owners that receive
  events (`CDV`) call `Destroy()` in their own destructor, so workers are joined while
  the event sink is still intact.
- WinUI's UI thread is an STA, so **WinDV.Native runs the engine on its own MTA thread**
  (`EngineThread`). **That thread must run a message loop.** DirectShow creates the
  renderer's windows on the thread that builds the graph, and they are children of the UI
  window, so the two threads' input is attached. Without the loop the whole UI hung as
  soon as a preview existed. Every API call is forwarded there and waited on. A UI-thread caller
  waits with `MsgWaitForMultipleObjectsEx(QS_SENDMESSAGE)`. The video renderer's window
  is a child of a UI-thread window, and it `SendMessage`s to it (for example in
  `put_Owner`), so a plain wait would deadlock. `windv_get_status` and
  `windv_set_options` never wait, so they are safe from a UI timer.
- The C# app calls slow engine functions through `Task.Run`, so the UI stays responsive
  while a device opens. Native callbacks arrive on worker threads and are marshalled with
  `DispatcherQueue.TryEnqueue`.
- **Preview airspace:** the preview is a native child HWND (`WinDVPreview`), positioned
  by `MainWindow.UpdatePreview()` over the `PreviewHost` border in physical pixels.
  It **must stay `WS_EX_LAYERED`**. WinUI's top-level window has no GDI redirection
  surface, so the legacy video renderer's output inside a plain child window is never
  shown (frames arrive, and the picture stays black).
- **Never create an MSDV filter just to inspect it** (`BindToObject` then release).
  Throwaway instances crashed the process about 1 launch in 3, with an access
  violation in ntdll a few seconds later. `GetVideoDeviceList` identifies DV devices
  from the property bag's `DevicePath` (`\\?\avc#...`, `windv::IsDVDevicePath`),
  without opening them.
- Capture stops on its own with a `StopReason` (duration, signal lost, disk full), and
  the UI explains why. Free-space and signal-loss rules live in
  `core/CaptureGuards`. The capture loop polls with `FrameQueue::GetFor(250 ms)` so it
  notices a missing signal. The camcorder keeps sending frames while paused and stops
  only when the tape stops.
- Camcorders report trick-play modes (`PLAY_FAST_FWD_1..6`, `PLAY_FAST_REV_1..6`, `_X`,
  `REVERSE_FREEZE`) rather than the `PLAY_FASTEST_*` they were sent. `ToDeckMode` in
  `DVDevice.cpp` maps each family.
  Nothing in XAML can draw over it, so hide it (`_modalCount`, settings view) before
  showing any dialog, flyout or overlay in that area.
- Only user-initiated closes raise `AppWindow.Closing`, not `Window.Close()`. Every close
  goes through `MainWindow.ShutdownAndCloseAsync`, which stops the engine and saves the
  settings.
- `FrameQueue::Get()` data stays valid until the next `Get()`. The record loop relies
  on this to repeat the last frame while paused or finished.

## Gotchas when testing

- **DV capture is exclusive.** Only one process can hold the camcorder. This machine has
  an installed WinDV (WinGet package `PetrMourek.WinDV`) that may be running and
  holding it. A second instance then shows
  `Error: Can't connect to the DV device (is another program using it?) (0x80040217 ...)`.
  That is correct behaviour, not a regression. Check for other `WinDV.exe` processes,
  and do not kill the user's.
- WinDV reads and writes `HKCU\Software\Petr Mourek\WinDV 1.2` (the subkey comes from
  the MFC app title `AFX_IDS_APP_TITLE`, not from "WinDV"). It is **shared by both front
  ends and the installed copy**. Back it up (`reg export "HKCU\Software\Petr Mourek"`)
  before automated runs that close either app, and restore it afterwards.
- WinUI buttons *do* expose `InvokePattern`, and the `SelectorBar` items expose
  `SelectionItemPattern`, so UI Automation works for the new app. Icon-only buttons need
  `AutomationProperties.Name`. For screenshots of a window on a monitor with a different
  DPI, use `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` with `PrintWindow`, not
  `GetWindowRect`, which is DPI-virtualized.
- The camcorder must be in **VTR/tape mode**. In stills mode Windows loads the
  still-image driver and it never appears as a DirectShow capture device.
- Without hardware, drive the MFC UI with Win32 messages (`WM_COMMAND` with a control
  ID) rather than UI Automation. MFC buttons in this dialog don't expose
  `InvokePattern`.

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
- `legacy/` holds the original VC6 `.dsp`/`.dsw`/`.clw` and `CppProperties.json` (a
  leftover from VS "Open Folder" mode that still declares the old MBCS/UNICODE defines).
  None of it is built, and its paths predate the `app/` move. `WinDV.sln` is
  authoritative.
