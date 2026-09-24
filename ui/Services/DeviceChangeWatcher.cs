using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;

namespace WinDV.Services;

/// <summary>
/// Tells when a capture device arrives or goes away: a camcorder turned on or
/// off, or plugged in or out. Windows sends one notification per device
/// interface, so a single camcorder raises <see cref="Changed"/> several times;
/// callers should wait for things to settle before looking.
/// </summary>
public sealed unsafe partial class DeviceChangeWatcher : IDisposable
{
    // KSCATEGORY_CAPTURE and KSCATEGORY_VIDEO: DirectShow's video input devices
    // (and the MSDV camcorder driver) register under both.
    private static readonly Guid[] InterfaceClasses =
    [
        new("65E8773D-8F56-11D0-A3B9-00A0C9223196"),
        new("6994AD05-93EF-11D0-A3CC-00A0C9223196"),
    ];

    private readonly DispatcherQueue _dispatcher;
    private readonly List<nint> _registrations = [];
    private GCHandle _self;

    /// <summary>Call on the UI thread; <see cref="Changed"/> is raised there.</summary>
    public DeviceChangeWatcher(DispatcherQueue dispatcher)
    {
        _dispatcher = dispatcher;
        _self = GCHandle.Alloc(this);
        foreach (Guid interfaceClass in InterfaceClasses)
        {
            var filter = new CmNotifyFilter
            {
                Size = sizeof(CmNotifyFilter),
                FilterType = CmNotifyFilterTypeDeviceInterface,
                ClassGuid = interfaceClass,
            };
            // Without notifications WinDV still works; it just won't notice changes.
            if (RegisterNotification(&filter, GCHandle.ToIntPtr(_self), &OnNotification, out nint handle) == 0)
            {
                _registrations.Add(handle);
            }
        }
    }

    /// <summary>A capture device arrived or was removed.</summary>
    public event EventHandler? Changed;

    public void Dispose()
    {
        // Waits for callbacks in progress, so the handle is safe to free afterwards.
        foreach (nint handle in _registrations)
        {
            UnregisterNotification(handle);
        }

        _registrations.Clear();
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    private static uint OnNotification(nint notify, nint context, int action, nint eventData, uint eventDataSize)
    {
        // A thread-pool thread: hand over to the UI thread and return at once.
        if (action is CmNotifyActionDeviceInterfaceArrival or CmNotifyActionDeviceInterfaceRemoval &&
            GCHandle.FromIntPtr(context).Target is DeviceChangeWatcher watcher)
        {
            watcher._dispatcher.TryEnqueue(() => watcher.Changed?.Invoke(watcher, EventArgs.Empty));
        }
        return 0; // ERROR_SUCCESS
    }

    private const int CmNotifyFilterTypeDeviceInterface = 0;
    private const int CmNotifyActionDeviceInterfaceArrival = 0;
    private const int CmNotifyActionDeviceInterfaceRemoval = 1;

    // CM_NOTIFY_FILTER: the union's largest member is a 200-character instance ID.
    [StructLayout(LayoutKind.Explicit, Size = 416)]
    private struct CmNotifyFilter
    {
        [FieldOffset(0)] public int Size;
        [FieldOffset(4)] public int Flags;
        [FieldOffset(8)] public int FilterType;
        [FieldOffset(12)] public int Reserved;
        [FieldOffset(16)] public Guid ClassGuid;
    }

    [LibraryImport("cfgmgr32.dll", EntryPoint = "CM_Register_Notification")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial uint RegisterNotification(CmNotifyFilter* filter, nint context,
        delegate* unmanaged[Stdcall]<nint, nint, int, nint, uint, uint> callback, out nint notify);

    [LibraryImport("cfgmgr32.dll", EntryPoint = "CM_Unregister_Notification")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvStdcall)])]
    private static partial uint UnregisterNotification(nint notify);
}
