using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;

namespace WinDV.Interop;

/// <summary>A failed engine call, with the engine's own message.</summary>
public sealed class EngineException(string message, bool deviceNotFound = false) : Exception(message)
{
    public bool DeviceNotFound { get; } = deviceNotFound;
}

public readonly record struct EngineStatus(
    EngineState State,
    DeckMode DeckMode,
    bool CanControlDeck,
    int Dropped,
    int Counter,
    int QueueLoad,
    int QueueCapacity,
    int FramesReceived,
    StopReason StopReason,
    long Time,
    long DvTime,
    int FileFrameCount);

public sealed record EngineOptions(
    bool Type2Avi,
    int DiscontinuityThreshold,
    int MaxAviFrames,
    int EveryNth,
    bool RecordPreview,
    bool DeckFollowsPipeline,
    int SignalLossSeconds);

public sealed record ParsedCommandLine(CommandLineMode Mode, bool ExitOnFinish, long Duration, string Files);

/// <summary>
/// The DirectShow engine in WinDV.Native.dll. Slow calls are async and run off
/// the UI thread; the engine serializes them on its own thread. Events are
/// raised on the UI thread.
/// </summary>
public sealed unsafe class DvEngine : IDisposable
{
    private nint _handle;
    private GCHandle _self;
    private readonly DispatcherQueue _dispatcher;

    private DvEngine(DispatcherQueue dispatcher) => _dispatcher = dispatcher;

    /// <summary>The camcorder's recording date/time changed (see <see cref="EngineStatus.DvTime"/>).</summary>
    public event EventHandler? DvTimeChanged;

    /// <summary>The pipeline failed on a worker thread and has stopped.</summary>
    public event EventHandler<string>? Error;

    /// <summary>Creates the engine; its preview is a child of <paramref name="parentHwnd"/>. Call on the UI thread.</summary>
    public static DvEngine Create(nint parentHwnd, DispatcherQueue dispatcher)
    {
        var engine = new DvEngine(dispatcher);
        engine._self = GCHandle.Alloc(engine);
        var result = NativeMethods.Create(parentHwnd, &OnNativeEvent, GCHandle.ToIntPtr(engine._self),
            out engine._handle);
        if (result != NativeResult.Ok)
        {
            engine._self.Free();
            throw new EngineException("Can't start the DV engine.");
        }
        return engine;
    }

    public void Dispose()
    {
        if (_handle != 0)
        {
            NativeMethods.Destroy(_handle);
            _handle = 0;
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    private static void OnNativeEvent(nint context, int nativeEvent)
    {
        // Engine or worker thread: hand over to the UI thread and return at once.
        if (GCHandle.FromIntPtr(context).Target is not DvEngine engine)
        {
            return;
        }

        engine._dispatcher.TryEnqueue(() => engine.Raise((NativeEvent)nativeEvent));
    }

    private void Raise(NativeEvent nativeEvent)
    {
        if (_handle == 0)
        {
            return;
        }

        switch (nativeEvent)
        {
            case NativeEvent.DvTimeChanged:
                DvTimeChanged?.Invoke(this, EventArgs.Empty);
                break;
            case NativeEvent.Error:
                string message = TakeError();
                if (message.Length > 0)
                {
                    Error?.Invoke(this, message);
                }

                break;
        }
    }

    private string TakeError() =>
        NativeMethods.ReadString((buffer, length) => NativeMethods.TakeError(_handle, (char*)buffer, length));

    private void Check(NativeResult result)
    {
        if (result == NativeResult.Ok)
        {
            return;
        }

        string message = TakeError();
        if (message.Length == 0)
        {
            message = $"The DV engine reported an error ({result}).";
        }

        throw new EngineException(message, result == NativeResult.DeviceNotFound);
    }

    private Task Run(Func<nint, NativeResult> call)
    {
        nint handle = _handle;
        return Task.Run(() => Check(call(handle)));
    }

    public Task<string[]> ListDevicesAsync()
    {
        nint handle = _handle;
        return Task.Run(() =>
        {
            int length = 4096;
            for (;;)
            {
                char[] buffer = new char[length];
                NativeResult result;
                int needed;
                fixed (char* p = buffer)
                {
                    result = NativeMethods.ListDevices(handle, p, length, out needed);
                }

                if (result == NativeResult.BufferTooSmall)
                {
                    length = needed;
                    continue;
                }
                Check(result);
                string list = new(buffer, 0, Math.Max(needed - 1, 0));
                return list.Split('\n', StringSplitOptions.RemoveEmptyEntries);
            }
        });
    }

    public Task ResetAsync() => Run(NativeMethods.Reset);

    public Task BuildCaptureAsync(string device) => Run(h => NativeMethods.BuildCapture(h, device));

    public Task CaptureStartAsync(string fileBase, string dateFormat, int suffixDigits, long duration = 0) =>
        Run(h => NativeMethods.CaptureStart(h, fileBase, dateFormat, suffixDigits, duration));

    public Task CaptureStopAsync() => Run(NativeMethods.CaptureStop);

    public Task TransportAsync(DeckCommand command) => Run(h => NativeMethods.Transport(h, (int)command));

    public Task BuildRecordAsync(string files, string device) =>
        Run(h => NativeMethods.BuildRecord(h, files, device));

    public Task RecordStartAsync() => Run(NativeMethods.RecordStart);

    public Task RecordStopAsync() => Run(NativeMethods.RecordStop);

    /// <summary>Never blocks: reads what the engine thread last published.</summary>
    public EngineStatus GetStatus()
    {
        if (_handle == 0)
        {
            return new EngineStatus(EngineState.Idle, DeckMode.Unknown, false, 0, -1, 0, 0, 0, StopReason.None, -1, 0, -1);
        }

        NativeMethods.GetStatus(_handle, out var s);
        return new EngineStatus((EngineState)s.State, (DeckMode)s.DeckMode, s.CanControlDeck != 0, s.Dropped,
            s.Counter, s.QueueLoad, s.QueueCapacity, s.FramesReceived, (StopReason)s.StopReason, s.Time, s.DVTime,
            s.FileFrameCount);
    }

    public void SetOptions(EngineOptions options)
    {
        var native = new NativeOptions
        {
            Type2AVI = options.Type2Avi ? 1 : 0,
            DiscontinuityThreshold = options.DiscontinuityThreshold,
            MaxAVIFrames = options.MaxAviFrames,
            EveryNth = options.EveryNth,
            RecordPreview = options.RecordPreview ? 1 : 0,
            DeckFollowsPipeline = options.DeckFollowsPipeline ? 1 : 0,
            SignalLossSeconds = options.SignalLossSeconds,
        };
        NativeMethods.SetOptions(_handle, in native);
    }

    /// <summary>Places the preview, in physical pixels of the parent's client area. Call on the UI thread.</summary>
    public void MovePreview(int x, int y, int width, int height)
    {
        if (_handle != 0)
        {
            NativeMethods.PreviewMove(_handle, x, y, width, height);
        }
    }

    public void HidePreview() => MovePreview(0, 0, 0, 0);

    /// <summary>Parses WinDV's command line; null means a usage error.</summary>
    public static ParsedCommandLine? ParseCommandLine(IReadOnlyList<string> args)
    {
        var pointers = new nint[args.Count];
        try
        {
            for (int i = 0; i < args.Count; i++)
            {
                pointers[i] = Marshal.StringToHGlobalUni(args[i]);
            }

            int length = 1024;
            for (;;)
            {
                char[] buffer = new char[length];
                NativeResult result;
                NativeCommandLine parsed;
                int needed;
                fixed (nint* argv = pointers)
                fixed (char* p = buffer)
                {
                    result = NativeMethods.ParseCommandLine(argv, args.Count, out parsed, p, length, out needed);
                }

                if (result == NativeResult.BufferTooSmall)
                {
                    length = needed;
                    continue;
                }
                if (result != NativeResult.Ok)
                {
                    return null;
                }

                return new ParsedCommandLine((CommandLineMode)parsed.Mode, parsed.ExitOnFinish != 0,
                    parsed.Duration, new string(buffer, 0, Math.Max(needed - 1, 0)));
            }
        }
        finally
        {
            foreach (nint p in pointers)
            {
                Marshal.FreeHGlobal(p);
            }
        }
    }

    /// <summary>"D:\dv\tape.04-07-15.00.avi" gives "D:\dv\tape".</summary>
    public static string CaptureBase(string filename) =>
        NativeMethods.ReadString((buffer, length) => NativeMethods.CaptureBase(filename, (char*)buffer, length));

    public static bool IsValidTimeFormat(string format) => NativeMethods.IsValidTimeFormat(format) != 0;

    public static string FormatNow(string format) =>
        NativeMethods.ReadString((buffer, length) => NativeMethods.FormatNow(format, (char*)buffer, length));
}
