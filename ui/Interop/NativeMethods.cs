// P/Invoke declarations for WinDV.Native.dll. Mirrors native\windv_api.h: keep
// the enums and struct layouts in step with it.

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace WinDV.Interop;

internal enum NativeResult
{
    Ok = 0,
    Failed = 1,
    DeviceNotFound = 2,
    InvalidArgument = 3,
    BufferTooSmall = 4,
    UsageError = 5,
}

/// <summary>Pipeline state (DVEngine::State).</summary>
public enum EngineState
{
    Idle = 0,
    RecordPaused = 1,
    Recording = 2,
    CapturePaused = 3,
    Capturing = 4,
    Finished = 5,
}

/// <summary>A VCR button (windv::DeckCommand).</summary>
public enum DeckCommand
{
    Play = 0,
    Pause = 1,
    Stop = 2,
    FastForward = 3,
    Rewind = 4,
}

/// <summary>What the deck is doing (windv::DeckMode).</summary>
public enum DeckMode
{
    Unknown = 0,
    Stopped = 1,
    Playing = 2,
    Paused = 3,
    FastForward = 4,
    Rewind = 5,
    CueForward = 6,
    CueReverse = 7,
    Recording = 8,
    RecordPaused = 9,
}

internal enum NativeEvent
{
    DVTimeChanged = 1,
    Error = 2,
}

public enum CommandLineMode
{
    Interactive = 0,
    Capture = 1,
    Record = 2,
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeStatus
{
    public int State;
    public int DeckMode;
    public int CanControlDeck;
    public int Dropped;
    public int Counter;
    public int QueueLoad;
    public int QueueCapacity;
    public int FramesReceived;
    public long Time;
    public long DVTime;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeOptions
{
    public int Type2AVI;
    public int DiscontinuityThreshold;
    public int MaxAVIFrames;
    public int EveryNth;
    public int RecordPreview;
    public int DeckFollowsPipeline;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCommandLine
{
    public int Mode;
    public int ExitOnFinish;
    public long Duration;
}

internal static unsafe partial class NativeMethods
{
    private const string Dll = "WinDV.Native.dll";

    [LibraryImport(Dll, EntryPoint = "windv_create")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult Create(nint parentHwnd,
        delegate* unmanaged[Stdcall]<nint, int, void> callback, nint context, out nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial void Destroy(nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_list_devices")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult ListDevices(nint engine, char* buffer, int length, out int needed);

    [LibraryImport(Dll, EntryPoint = "windv_preview_move")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial void PreviewMove(nint engine, int x, int y, int width, int height);

    [LibraryImport(Dll, EntryPoint = "windv_set_options")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial void SetOptions(nint engine, in NativeOptions options);

    [LibraryImport(Dll, EntryPoint = "windv_get_status")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial void GetStatus(nint engine, out NativeStatus status);

    [LibraryImport(Dll, EntryPoint = "windv_take_error")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial int TakeError(nint engine, char* buffer, int length);

    [LibraryImport(Dll, EntryPoint = "windv_reset")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult Reset(nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_build_capture", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult BuildCapture(nint engine, string device);

    [LibraryImport(Dll, EntryPoint = "windv_capture_start", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult CaptureStart(nint engine, string fileBase, string dateFormat,
        int suffixDigits, long duration);

    [LibraryImport(Dll, EntryPoint = "windv_capture_stop")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult CaptureStop(nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_transport")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult Transport(nint engine, int command);

    [LibraryImport(Dll, EntryPoint = "windv_build_record", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult BuildRecord(nint engine, string files, string device);

    [LibraryImport(Dll, EntryPoint = "windv_record_start")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult RecordStart(nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_record_stop")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult RecordStop(nint engine);

    [LibraryImport(Dll, EntryPoint = "windv_parse_command_line")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial NativeResult ParseCommandLine(nint* args, int count, out NativeCommandLine result,
        char* files, int length, out int needed);

    [LibraryImport(Dll, EntryPoint = "windv_capture_base", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial int CaptureBase(string filename, char* buffer, int length);

    [LibraryImport(Dll, EntryPoint = "windv_is_valid_time_format", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial int IsValidTimeFormat(string format);

    [LibraryImport(Dll, EntryPoint = "windv_format_now", StringMarshalling = StringMarshalling.Utf16)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    public static partial int FormatNow(string format, char* buffer, int length);

    /// <summary>Calls a native "fill this buffer" function, growing the buffer until it fits.</summary>
    public static string ReadString(Func<nint, int, int> fill, int initialLength = 512)
    {
        int length = initialLength;
        for (;;)
        {
            char[] buffer = new char[length];
            int needed;
            fixed (char* p = buffer)
                needed = fill((nint)p, length);
            if (needed <= 0)
                return string.Empty;
            if (needed <= length)
                return new string(buffer, 0, needed - 1);
            length = needed;
        }
    }
}
