using Microsoft.Win32;

namespace WinDV.Services;

/// <summary>
/// WinDV's settings. They live where the original MFC WinDV kept them
/// (HKCU\Software\Petr Mourek\WinDV 1.2, from its "WinDV 1.2" app title), with
/// the same value names, so both versions share one configuration, except
/// DiscontinuityThreshold: WinDV 2 intentionally ignores the old misspelled
/// DiscontinuityTreshold value once so it resets to the default. The names are
/// a compatibility surface: keep them.
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
    /// <summary>The live preview's audio volume, 0-100.</summary>
    public int PreviewVolume { get; set; } = 100;
    public bool PreviewMuted { get; set; }

    // Capture
    public string CaptureDevice { get; set; } = DefaultDevice;
    public string CaptureFile { get; set; } = "";
    public bool Type2Avi { get; set; } = true;
    public int DiscontinuityThreshold { get; set; } = 1;
    public int MaxAviFrames { get; set; } = 25 * 60 * 15;
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
    public string AviPrefix { get; set; } = "";
    public string AviSuffix { get; set; } = "";
    public bool RecordPreview { get; set; } = true;

    // Updates (new in WinDV 2; the MFC app ignores this subkey)
    public bool CheckForUpdates { get; set; } = true;
    /// <summary>Unix time of the last successful check; checked at most once a day.</summary>
    public long LastUpdateCheck { get; set; }
    /// <summary>The newest release tag seen on GitHub, e.g. "v2.2.3".</summary>
    public string LatestRelease { get; set; } = "";
    /// <summary>A release whose notice the user closed; it isn't shown again.</summary>
    public string DismissedRelease { get; set; } = "";

    public bool IsFirstRun => WindowWidth <= 0 || WindowHeight <= 0;

    public static SettingsStore Load()
    {
        var settings = new SettingsStore();
        using RegistryKey? root = Registry.CurrentUser.OpenSubKey(KeyPath);
        using RegistryKey? main = root?.OpenSubKey("MainWindow");
        using RegistryKey? capture = root?.OpenSubKey("Capture");
        using RegistryKey? record = root?.OpenSubKey("Record");
        using RegistryKey? updates = root?.OpenSubKey("Updates");

        settings.WindowX = GetInt(main, "X", 0);
        settings.WindowY = GetInt(main, "Y", 0);
        settings.WindowWidth = GetInt(main, "W", 0);
        settings.WindowHeight = GetInt(main, "H", 0);
        settings.DeckControl = GetInt(main, "DVControlEnabled", 0) > 0;
        settings.SelectedTool = Math.Clamp(GetInt(main, "SelectedTool", 0), 0, 1);
        settings.WorkingDirectory = GetString(main, "WorkingDirectory", ".");
        settings.PreviewVolume = Math.Clamp(GetInt(main, "PreviewVolume", 100), 0, 100);
        settings.PreviewMuted = GetInt(main, "PreviewMuted", 0) > 0;

        settings.CaptureDevice = GetString(capture, "DVDevice", DefaultDevice);
        settings.CaptureFile = GetString(capture, "File", "");
        settings.Type2Avi = GetInt(capture, "Type2AVI", 1) > 0;
        settings.DiscontinuityThreshold = Math.Max(0, GetInt(capture, "DiscontinuityThreshold", 1));
        settings.MaxAviFrames = Math.Max(10, GetInt(capture, "MaxAVIFrames", 25 * 60 * 15));
        settings.EveryNth = Math.Max(1, GetInt(capture, "EveryNth", 1));
        settings.DateTimeFormat = GetString(capture, "DateTimeFormat", "%y-%m-%d_%H-%M");
        settings.DateTimeFormatHistory = GetString(capture, "DateTimeFormatHistory", DefaultFormatHistory)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries).ToList();
        settings.SuffixDigits = Math.Clamp(GetInt(capture, "SuffixDigits", 2), 0, 4);
        // New in WinDV 2; the original ignores it.
        settings.SignalLossSeconds = Math.Clamp(GetInt(capture, "StopOnSignalLoss", 0), 0, 3600);

        settings.RecordDevice = GetString(record, "DVDevice", DefaultDevice);
        settings.RecordFile = GetString(record, "File", "");
        settings.AviPrefix = GetString(record, "AVIPrefix", "");
        settings.AviSuffix = GetString(record, "AVISuffix", "");
        settings.RecordPreview = GetInt(record, "Preview", 1) > 0;

        settings.CheckForUpdates = GetInt(updates, "CheckForUpdates", 1) > 0;
        settings.LastUpdateCheck = updates?.GetValue("LastCheck") is long last ? last : 0;
        settings.LatestRelease = GetString(updates, "LatestRelease", "");
        settings.DismissedRelease = GetString(updates, "DismissedRelease", "");
        return settings;
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
            SetInt(main, "PreviewVolume", PreviewVolume);
            SetInt(main, "PreviewMuted", PreviewMuted ? 1 : 0);
        }
        using (RegistryKey capture = root.CreateSubKey("Capture"))
        {
            capture.SetValue("DVDevice", CaptureDevice, RegistryValueKind.String);
            capture.SetValue("File", CaptureFile, RegistryValueKind.String);
            SetInt(capture, "Type2AVI", Type2Avi ? 1 : 0);
            SetInt(capture, "DiscontinuityThreshold", DiscontinuityThreshold);
            SetInt(capture, "MaxAVIFrames", MaxAviFrames);
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
            record.SetValue("AVIPrefix", AviPrefix, RegistryValueKind.String);
            record.SetValue("AVISuffix", AviSuffix, RegistryValueKind.String);
            SetInt(record, "Preview", RecordPreview ? 1 : 0);
        }
        using (RegistryKey updates = root.CreateSubKey("Updates"))
        {
            SetInt(updates, "CheckForUpdates", CheckForUpdates ? 1 : 0);
            updates.SetValue("LastCheck", LastUpdateCheck, RegistryValueKind.QWord);
            updates.SetValue("LatestRelease", LatestRelease, RegistryValueKind.String);
            updates.SetValue("DismissedRelease", DismissedRelease, RegistryValueKind.String);
        }
    }

    /// <summary>Makes <paramref name="format"/> current; the old one moves into the history, most recent first.</summary>
    public void UseDateTimeFormat(string format)
    {
        var history = new List<string>();
        if (DateTimeFormat.Length > 0)
        {
            history.Add(DateTimeFormat);
        }

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
