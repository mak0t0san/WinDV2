using Microsoft.Win32;

namespace WinDV.Services;

/// <summary>
/// WinDV's settings. They live where the original MFC WinDV kept them
/// (HKCU\Software\Petr Mourek\WinDV 1.2, from its "WinDV 1.2" app title), with
/// the same value names, so both versions share one configuration. The names
/// are a compatibility surface: keep them, including "DiscontinuityTreshold".
/// </summary>
public sealed class SettingsStore
{
    private const string KeyPath = @"Software\Petr Mourek\WinDV 1.2";
    private const string DefaultDevice = "Microsoft DV Camera and VCR";
    private const string DefaultFormatHistory =
        "%y-%m-%d_%H-%M-%S\n%Y-%m-%d_%H-%M\n%Y-%m-%d_%H-%M-%S\n%Y%m%d-%H%M%S\n%a_%H-%M-%S";
    public const int MaxFormatHistory = 10;

    // MainWindow
    public int WindowX { get; set; }
    public int WindowY { get; set; }
    public int WindowWidth { get; set; }
    public int WindowHeight { get; set; }
    /// <summary>WinDV drives the tape: plays it when capture starts, records when recording starts.</summary>
    public bool DeckControl { get; set; }
    /// <summary>0 = capture, 1 = record to tape.</summary>
    public int SelectedTool { get; set; }
    public string WorkingDirectory { get; set; } = ".";

    // Capture
    public string CaptureDevice { get; set; } = DefaultDevice;
    public string CaptureFile { get; set; } = "";
    public bool Type2AVI { get; set; } = true;
    public int DiscontinuityThreshold { get; set; } = 1;
    public int MaxAVIFrames { get; set; } = 25 * 60 * 15;
    public int EveryNth { get; set; } = 1;
    public string DateTimeFormat { get; set; } = "%y-%m-%d_%H-%M";
    public List<string> DateTimeFormatHistory { get; set; } = [];
    public int SuffixDigits { get; set; } = 2;
    /// <summary>Stop capturing after this many seconds without a DV signal (end of tape); 0 = never.</summary>
    public int SignalLossSeconds { get; set; }
    public const int DefaultSignalLossSeconds = 10;

    // Record
    public string RecordDevice { get; set; } = DefaultDevice;
    public string RecordFile { get; set; } = "";
    public string AVIPrefix { get; set; } = "";
    public string AVISuffix { get; set; } = "";
    public bool RecordPreview { get; set; } = true;

    public bool IsFirstRun => WindowWidth <= 0 || WindowHeight <= 0;

    public static SettingsStore Load()
    {
        var s = new SettingsStore();
        using RegistryKey? root = Registry.CurrentUser.OpenSubKey(KeyPath);
        using RegistryKey? main = root?.OpenSubKey("MainWindow");
        using RegistryKey? capture = root?.OpenSubKey("Capture");
        using RegistryKey? record = root?.OpenSubKey("Record");

        s.WindowX = GetInt(main, "X", 0);
        s.WindowY = GetInt(main, "Y", 0);
        s.WindowWidth = GetInt(main, "W", 0);
        s.WindowHeight = GetInt(main, "H", 0);
        s.DeckControl = GetInt(main, "DVControlEnabled", 0) > 0;
        s.SelectedTool = Math.Clamp(GetInt(main, "SelectedTool", 0), 0, 1);
        s.WorkingDirectory = GetString(main, "WorkingDirectory", ".");

        s.CaptureDevice = GetString(capture, "DVDevice", DefaultDevice);
        s.CaptureFile = GetString(capture, "File", "");
        s.Type2AVI = GetInt(capture, "Type2AVI", 1) > 0;
        s.DiscontinuityThreshold = Math.Max(0, GetInt(capture, "DiscontinuityTreshold", 1));
        s.MaxAVIFrames = Math.Max(10, GetInt(capture, "MaxAVIFrames", 25 * 60 * 15));
        s.EveryNth = Math.Max(1, GetInt(capture, "EveryNth", 1));
        s.DateTimeFormat = GetString(capture, "DateTimeFormat", "%y-%m-%d_%H-%M");
        s.DateTimeFormatHistory = GetString(capture, "DateTimeFormatHistory", DefaultFormatHistory)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries).ToList();
        s.SuffixDigits = Math.Clamp(GetInt(capture, "SuffixDigits", 2), 0, 4);
        // New in WinDV 2; the original ignores it.
        s.SignalLossSeconds = Math.Clamp(GetInt(capture, "StopOnSignalLoss", 0), 0, 3600);

        s.RecordDevice = GetString(record, "DVDevice", DefaultDevice);
        s.RecordFile = GetString(record, "File", "");
        s.AVIPrefix = GetString(record, "AVIPrefix", "");
        s.AVISuffix = GetString(record, "AVISuffix", "");
        s.RecordPreview = GetInt(record, "Preview", 1) > 0;
        return s;
    }

    public void Save()
    {
        using RegistryKey root = Registry.CurrentUser.CreateSubKey(KeyPath);
        using (RegistryKey main = root.CreateSubKey("MainWindow"))
        {
            SetInt(main, "X", WindowX);
            SetInt(main, "Y", WindowY);
            SetInt(main, "W", WindowWidth);
            SetInt(main, "H", WindowHeight);
            SetInt(main, "DVControlEnabled", DeckControl ? 1 : 0);
            SetInt(main, "SelectedTool", SelectedTool);
            main.SetValue("WorkingDirectory", WorkingDirectory, RegistryValueKind.String);
        }
        using (RegistryKey capture = root.CreateSubKey("Capture"))
        {
            capture.SetValue("DVDevice", CaptureDevice, RegistryValueKind.String);
            capture.SetValue("File", CaptureFile, RegistryValueKind.String);
            SetInt(capture, "Type2AVI", Type2AVI ? 1 : 0);
            SetInt(capture, "DiscontinuityTreshold", DiscontinuityThreshold);
            SetInt(capture, "MaxAVIFrames", MaxAVIFrames);
            SetInt(capture, "EveryNth", EveryNth);
            capture.SetValue("DateTimeFormat", DateTimeFormat, RegistryValueKind.String);
            capture.SetValue("DateTimeFormatHistory", string.Join("\n", DateTimeFormatHistory),
                RegistryValueKind.String);
            SetInt(capture, "SuffixDigits", SuffixDigits);
            SetInt(capture, "StopOnSignalLoss", SignalLossSeconds);
        }
        using (RegistryKey record = root.CreateSubKey("Record"))
        {
            record.SetValue("DVDevice", RecordDevice, RegistryValueKind.String);
            record.SetValue("File", RecordFile, RegistryValueKind.String);
            record.SetValue("AVIPrefix", AVIPrefix, RegistryValueKind.String);
            record.SetValue("AVISuffix", AVISuffix, RegistryValueKind.String);
            SetInt(record, "Preview", RecordPreview ? 1 : 0);
        }
    }

    /// <summary>Makes <paramref name="format"/> current; the old one moves into the history, most recent first.</summary>
    public void UseDateTimeFormat(string format)
    {
        var history = new List<string>();
        if (DateTimeFormat.Length > 0)
            history.Add(DateTimeFormat);
        history.AddRange(DateTimeFormatHistory);
        DateTimeFormatHistory = history
            .Where(f => f.Length > 0 && f != format)
            .Distinct()
            .Take(MaxFormatHistory)
            .ToList();
        DateTimeFormat = format;
    }

    private static int GetInt(RegistryKey? key, string name, int fallback) =>
        key?.GetValue(name) switch
        {
            int i => i,
            string s when int.TryParse(s, out int parsed) => parsed,
            _ => fallback,
        };

    private static string GetString(RegistryKey? key, string name, string fallback) =>
        key?.GetValue(name) as string ?? fallback;

    // MFC's WriteProfileInt stores a REG_DWORD; negative positions wrap as in C.
    private static void SetInt(RegistryKey key, string name, int value) =>
        key.SetValue(name, value, RegistryValueKind.DWord);
}
