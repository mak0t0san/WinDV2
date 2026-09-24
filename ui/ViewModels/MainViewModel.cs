using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Dispatching;
using WinDV.Interop;
using WinDV.Services;

namespace WinDV.ViewModels;

public enum Tool
{
    Capture = 0,
    Record = 1,
}

/// <summary>
/// The main window's state and commands. Capture: the transport buttons drive the
/// camcorder and the preview follows the tape; REC writes AVI files. Record to
/// tape: Play records the chosen files onto the tape, Pause holds the current
/// frame, Stop resets.
/// </summary>
public sealed partial class MainViewModel : ObservableObject
{
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(200);

    private readonly SettingsStore _settings;
    private readonly DispatcherQueueTimer _timer;
    private readonly SemaphoreSlim _busy = new(1, 1);
    private DVEngine? _engine;
    private EngineStatus _status;
    private bool _exitOnFinish;
    private bool _suppressToolChange;

    public MainViewModel(SettingsStore settings, DispatcherQueue dispatcher)
    {
        _settings = settings;
        // No engine is attached yet, so these don't trigger pipeline rebuilds.
        SelectedTool = (Tool)settings.SelectedTool;
        CaptureDevice = settings.CaptureDevice;
        RecordDevice = settings.RecordDevice;
        CaptureFile = settings.CaptureFile;
        RecordFiles = settings.RecordFile;
        Timecode = FormatTimecode(-1);
        RecordedAt = StatusText = DeckText = DroppedText = ErrorMessage = SignalText = "";

        _timer = dispatcher.CreateTimer();
        _timer.Interval = PollInterval;
        _timer.Tick += (_, _) => UpdateStatus();
    }

    /// <summary>The window should close (a command-line run has finished).</summary>
    public event EventHandler? CloseRequested;

    public SettingsStore Settings => _settings;

    public ObservableCollection<string> Devices { get; } = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsCaptureTool), nameof(IsRecordTool))]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand), nameof(PauseCommand), nameof(StopCommand),
        nameof(FastForwardCommand), nameof(RewindCommand), nameof(RecCommand))]
    public partial Tool SelectedTool { get; set; }

    [ObservableProperty] public partial string CaptureDevice { get; set; }
    [ObservableProperty] public partial string RecordDevice { get; set; }
    [ObservableProperty] public partial string CaptureFile { get; set; }
    [ObservableProperty] public partial string RecordFiles { get; set; }

    [ObservableProperty] public partial string Timecode { get; set; }
    [ObservableProperty] public partial string RecordedAt { get; set; }
    [ObservableProperty] public partial string StatusText { get; set; }
    [ObservableProperty] public partial string DeckText { get; set; }
    [ObservableProperty] public partial string DroppedText { get; set; }
    [ObservableProperty] public partial double QueueFill { get; set; }
    [ObservableProperty] public partial bool IsQueueVisible { get; set; }

    [ObservableProperty] public partial bool IsCapturing { get; set; }
    [ObservableProperty] public partial bool IsRecordingToTape { get; set; }
    [ObservableProperty] public partial bool IsPlaying { get; set; }
    [ObservableProperty] public partial bool IsPaused { get; set; }
    /// <summary>Winding or cueing forward: lights up Fast-forward.</summary>
    [ObservableProperty] public partial bool IsGoingForward { get; set; }
    /// <summary>Winding or cueing backward: lights up Rewind.</summary>
    [ObservableProperty] public partial bool IsGoingBackward { get; set; }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand), nameof(PauseCommand), nameof(StopCommand),
        nameof(FastForwardCommand), nameof(RewindCommand), nameof(RecCommand))]
    public partial bool IsBusy { get; set; }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand), nameof(PauseCommand), nameof(StopCommand),
        nameof(FastForwardCommand), nameof(RewindCommand), nameof(RecCommand))]
    public partial bool CanControlDeck { get; set; }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RecCommand))]
    public partial bool IsLive { get; set; }

    /// <summary>A pipeline is running, so the preview window has something to show.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsPlaceholder))]
    public partial bool HasPicture { get; set; }

    public bool ShowsPlaceholder => !HasPicture;

    /// <summary>"Signal" / "No signal": whether DV frames are arriving from the source.</summary>
    [ObservableProperty] public partial string SignalText { get; set; }
    [ObservableProperty] public partial bool HasSignal { get; set; }

    [ObservableProperty] public partial string ErrorMessage { get; set; }
    [ObservableProperty] public partial bool HasError { get; set; }

    public bool IsCaptureTool => SelectedTool == Tool.Capture;
    public bool IsRecordTool => SelectedTool == Tool.Record;

    public void Attach(DVEngine engine)
    {
        _engine = engine;
        _engine.Error += (_, message) => _ = OnEngineErrorAsync(message);
        _engine.DVTimeChanged += (_, _) => UpdateStatus();
        _timer.Start();
    }

    public void Detach()
    {
        _timer.Stop();
        _settings.SelectedTool = (int)SelectedTool;
        _settings.CaptureDevice = CaptureDevice;
        _settings.RecordDevice = RecordDevice;
        _settings.CaptureFile = CaptureFile;
        _settings.RecordFile = RecordFiles;
        _engine = null;
    }

    /// <summary>First start: devices, then the pipeline or the command-line job.</summary>
    public async Task StartAsync(ParsedCommandLine? commandLine)
    {
        await RefreshDevicesAsync();
        if (commandLine is null || commandLine.Mode == CommandLineMode.Interactive)
        {
            await RunAsync(InitPipelineAsync, resetOnError: false);
            return;
        }

        _exitOnFinish = commandLine.ExitOnFinish;
        _suppressToolChange = true;
        if (commandLine.Mode == CommandLineMode.Capture)
        {
            SelectedTool = Tool.Capture;
            CaptureFile = commandLine.Files;
            _suppressToolChange = false;
            await RunAsync(async () =>
            {
                await BuildCaptureAsync();
                await StartCaptureAsync(commandLine.Duration);
            });
        }
        else
        {
            SelectedTool = Tool.Record;
            RecordFiles = commandLine.Files;
            _suppressToolChange = false;
            await RunAsync(async () =>
            {
                await BuildRecordAsync();
                await Engine.RecordStartAsync();
            });
        }
    }

    /// <summary>Called when the window closes: leaves the deck stopped if WinDV was driving it.</summary>
    public async Task ShutdownAsync()
    {
        if (_engine is null)
            return;
        try
        {
            if (IsCaptureTool && CanControlDeck && _settings.DeckControl)
                await _engine.TransportAsync(DeckCommand.Stop);
        }
        catch (EngineException)
        {
            // Closing anyway.
        }
    }

    [RelayCommand]
    public async Task RefreshDevicesAsync()
    {
        if (_engine is null)
            return;
        string[] devices;
        try
        {
            devices = await _engine.ListDevicesAsync();
        }
        catch (EngineException e)
        {
            ShowError(e.Message);
            return;
        }

        string capture = CaptureDevice, record = RecordDevice;
        Devices.Clear();
        foreach (string device in devices)
            Devices.Add(device);
        // Keep the saved choices selectable even while the device is unplugged.
        foreach (string saved in new[] { capture, record })
        {
            if (saved.Length > 0 && !Devices.Contains(saved))
                Devices.Add(saved);
        }
        CaptureDevice = capture;
        RecordDevice = record;
        // The values may be unchanged, but the lists they select from are new.
        OnPropertyChanged(nameof(CaptureDevice));
        OnPropertyChanged(nameof(RecordDevice));
    }

    public void DismissError()
    {
        HasError = false;
        ErrorMessage = "";
    }

    /// <summary>Settings changed: push the engine options, and rebuild if the pipeline depends on them.</summary>
    public void ApplySettings()
    {
        ApplyOptions();
    }

    // ------------------------------------------------------------------ Tool / device changes

    partial void OnSelectedToolChanged(Tool value)
    {
        _settings.SelectedTool = (int)value;
        if (!_suppressToolChange && _engine is not null)
            _ = RunAsync(InitPipelineAsync, resetOnError: false);
    }

    partial void OnCaptureDeviceChanged(string oldValue, string newValue)
    {
        if (oldValue is not null && oldValue != newValue && IsCaptureTool && _engine is not null && newValue.Length > 0)
            _ = RunAsync(InitPipelineAsync, resetOnError: false);
    }

    // ------------------------------------------------------------------ Transport commands

    private bool CanUseDeckButtons() => !IsBusy && (IsRecordTool || CanControlDeck);
    private bool CanWind() => !IsBusy && IsCaptureTool && CanControlDeck;
    private bool CanRec() => !IsBusy && IsCaptureTool && IsLive;

    [RelayCommand(CanExecute = nameof(CanUseDeckButtons))]
    private Task PlayAsync() => RunAsync(async () =>
    {
        if (IsCaptureTool)
        {
            await Engine.TransportAsync(DeckCommand.Play);
            return;
        }
        switch (_status.State)
        {
            case EngineState.RecordPaused:
                await Engine.RecordStartAsync();
                break;
            case EngineState.Recording:
                break;
            default: // idle or finished: start over with the current file list
                await BuildRecordAsync();
                await Engine.RecordStartAsync();
                break;
        }
    });

    [RelayCommand(CanExecute = nameof(CanUseDeckButtons))]
    private Task PauseAsync() => RunAsync(async () =>
    {
        if (IsCaptureTool)
        {
            await Engine.TransportAsync(DeckCommand.Pause);
            return;
        }
        switch (_status.State)
        {
            case EngineState.Recording:
                await Engine.RecordStopAsync();
                break;
            case EngineState.RecordPaused:
                await Engine.RecordStartAsync();
                break;
            default: // cue up the first frame without recording
                await BuildRecordAsync();
                break;
        }
    });

    [RelayCommand(CanExecute = nameof(CanUseDeckButtons))]
    private Task StopAsync() => RunAsync(async () =>
    {
        if (IsCaptureTool)
        {
            if (_status.State == EngineState.Capturing)
                await Engine.CaptureStopAsync();
            await Engine.TransportAsync(DeckCommand.Stop);
            return;
        }
        await InitPipelineAsync();
    });

    [RelayCommand(CanExecute = nameof(CanWind))]
    private Task FastForwardAsync() => RunAsync(() => Engine.TransportAsync(DeckCommand.FastForward));

    [RelayCommand(CanExecute = nameof(CanWind))]
    private Task RewindAsync() => RunAsync(() => Engine.TransportAsync(DeckCommand.Rewind));

    [RelayCommand(CanExecute = nameof(CanRec))]
    private Task RecAsync() => RunAsync(async () =>
    {
        if (_status.State == EngineState.Capturing)
        {
            await Engine.CaptureStopAsync();
            return;
        }
        if (_status.State is EngineState.Idle or EngineState.Finished)
            await BuildCaptureAsync();
        await StartCaptureAsync(0);
    });

    // ------------------------------------------------------------------ Pipeline

    private DVEngine Engine => _engine ?? throw new EngineException("The DV engine is not running.");

    // "InitVideo": capture shows the live picture straight away; record waits
    // until files are chosen and Play is pressed.
    private async Task InitPipelineAsync()
    {
        _exitOnFinish = false;
        if (IsCaptureTool)
        {
            StatusText = "Connecting to the camcorder...";
            await BuildCaptureAsync();
        }
        else
        {
            await Engine.ResetAsync();
        }
        UpdateStatus();
    }

    private async Task BuildCaptureAsync()
    {
        ApplyOptions();
        if (string.IsNullOrWhiteSpace(CaptureDevice))
            throw new EngineException("Choose a DV device first.");
        await Engine.BuildCaptureAsync(CaptureDevice);
    }

    private async Task StartCaptureAsync(long duration)
    {
        string fileBase = CaptureFile.Trim();
        if (fileBase.Length == 0)
            throw new EngineException("Choose where to save the capture first.");
        if (_settings.DeckControl && CanControlDeckNow() && !IsTapeMoving())
            await Engine.TransportAsync(DeckCommand.Play);
        await Engine.CaptureStartAsync(fileBase, _settings.DateTimeFormat, _settings.SuffixDigits, duration);
    }

    private async Task BuildRecordAsync()
    {
        ApplyOptions();
        if (string.IsNullOrWhiteSpace(RecordFiles))
            throw new EngineException("Choose the AVI files to record first.");
        if (string.IsNullOrWhiteSpace(RecordDevice))
            throw new EngineException("Choose a DV device first.");
        string list = $"{_settings.AVIPrefix}|{RecordFiles}|{_settings.AVISuffix}";
        await Engine.BuildRecordAsync(list, RecordDevice);
    }

    private bool CanControlDeckNow() => Engine.GetStatus().CanControlDeck;

    private bool IsTapeMoving() => Engine.GetStatus().DeckMode is DeckMode.Playing;

    private void ApplyOptions()
    {
        // On the capture tab the buttons drive the deck directly; "deck control"
        // then only means REC starts the tape (see StartCaptureAsync). Recording
        // to tape needs the engine to put the deck into record itself.
        _engine?.SetOptions(new EngineOptions(
            _settings.Type2AVI,
            _settings.DiscontinuityThreshold,
            _settings.MaxAVIFrames,
            _settings.EveryNth,
            _settings.RecordPreview,
            DeckFollowsPipeline: IsRecordTool && _settings.DeckControl));
    }

    /// <summary>
    /// Runs one pipeline action at a time. On failure the pipeline is reset (as
    /// the original WinDV did) and the first error is shown.
    /// </summary>
    private async Task RunAsync(Func<Task> action, bool resetOnError = true)
    {
        if (_engine is null)
            return;
        await _busy.WaitAsync();
        IsBusy = true;
        try
        {
            DismissError();
            await action();
        }
        catch (EngineException e)
        {
            if (resetOnError)
            {
                try
                {
                    await InitPipelineAsync();
                }
                catch (EngineException)
                {
                    // Report the original problem, not the consequence.
                }
            }
            ShowError(e.Message);
        }
        finally
        {
            IsBusy = false;
            _busy.Release();
            UpdateStatus();
        }
    }

    private async Task OnEngineErrorAsync(string message)
    {
        await RunAsync(InitPipelineAsync, resetOnError: false);
        ShowError(message);
    }

    private void ShowError(string message)
    {
        ErrorMessage = message;
        HasError = true;
        UpdateStatus();
    }

    // ------------------------------------------------------------------ Status

    private void UpdateStatus()
    {
        if (_engine is null)
            return;
        _status = _engine.GetStatus();
        var s = _status;

        HasPicture = s.State != EngineState.Idle;
        UpdateSignal(s);
        IsLive = s.State is EngineState.CapturePaused or EngineState.Capturing or EngineState.Finished;
        IsCapturing = s.State == EngineState.Capturing;
        IsRecordingToTape = s.State == EngineState.Recording;
        CanControlDeck = s.CanControlDeck;
        IsPlaying = s.DeckMode == DeckMode.Playing || s.State == EngineState.Recording;
        IsPaused = s.DeckMode == DeckMode.Paused || s.State == EngineState.RecordPaused;
        IsGoingForward = s.DeckMode is DeckMode.FastForward or DeckMode.CueForward;
        IsGoingBackward = s.DeckMode is DeckMode.Rewind or DeckMode.CueReverse;

        Timecode = FormatTimecode(s.State == EngineState.Idle ? -1 : s.Time);
        RecordedAt = s.DVTime > 0
            ? DateTimeOffset.FromUnixTimeSeconds(s.DVTime).ToLocalTime().ToString("G")
            : "";
        DeckText = s.State == EngineState.Idle ? "" : DeckModeText(s);
        DroppedText = s.State == EngineState.Capturing && s.Dropped > 0 ? $"{s.Dropped} dropped" : "";
        IsQueueVisible = s.State is EngineState.Capturing or EngineState.Recording or EngineState.RecordPaused;
        QueueFill = s.QueueCapacity > 0 ? 100.0 * s.QueueLoad / s.QueueCapacity : 0;
        if (!IsBusy || s.State != EngineState.Idle)
            StatusText = StateText(s.State);

        if (s.State == EngineState.Finished && _exitOnFinish)
        {
            _exitOnFinish = false;
            CloseRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    private int _lastFrames;
    private long _lastFrameTick;

    // A signal is present while the frame count keeps moving; allow a second
    // of silence before saying otherwise, so a status poll between frames
    // doesn't flicker.
    private void UpdateSignal(EngineStatus s)
    {
        long now = Environment.TickCount64;
        if (s.FramesReceived != _lastFrames)
        {
            _lastFrames = s.FramesReceived;
            _lastFrameTick = now;
        }
        bool live = s.State != EngineState.Idle;
        HasSignal = live && s.FramesReceived > 0 && now - _lastFrameTick < 1000;
        SignalText = !live ? "" : HasSignal ? "Signal" : "No signal";
    }

    private string StateText(EngineState state) => state switch
    {
        EngineState.CapturePaused => "Live. Press REC to capture.",
        EngineState.Capturing => "Capturing to disk",
        EngineState.RecordPaused => "Ready. Press Play to record to tape.",
        EngineState.Recording => "Recording to tape",
        EngineState.Finished => "Finished",
        _ => IsCaptureTool ? "Not connected" : "Choose AVI files, then press Play to record them to tape.",
    };

    private static string DeckModeText(EngineStatus s)
    {
        if (!s.CanControlDeck)
            return "No deck control";
        return s.DeckMode switch
        {
            DeckMode.Stopped => "Stopped",
            DeckMode.Playing => "Playing",
            DeckMode.Paused => "Paused",
            DeckMode.FastForward => "Fast forward",
            DeckMode.Rewind => "Rewind",
            DeckMode.CueForward => "Cue forward",
            DeckMode.CueReverse => "Cue reverse",
            DeckMode.Recording => "Recording",
            DeckMode.RecordPaused => "Record pause",
            _ => "",
        };
    }

    /// <summary>h:mm:ss.t from 100 ns units; blank when there is no position.</summary>
    public static string FormatTimecode(long time)
    {
        if (time < 0)
            return "-:--:--.-";
        long tenths = time / 1_000_000;
        return $"{tenths / 36000}:{tenths / 600 % 60:00}:{tenths / 10 % 60:00}.{tenths % 10}";
    }
}
