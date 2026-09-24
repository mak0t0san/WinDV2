using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WinDV.Interop;
using WinDV.Services;

namespace WinDV.Views;

public sealed class SettingsClosedEventArgs(bool saved) : EventArgs
{
    public bool Saved { get; } = saved;
}

/// <summary>The settings screen; replaces the original property sheet.</summary>
public sealed partial class SettingsView : UserControl
{
    private const int MaxSuffixDigits = 4;
    private SettingsStore? _settings;

    public SettingsView()
    {
        InitializeComponent();
        for (int i = 0; i <= MaxSuffixDigits; i++)
            DigitsBox.Items.Add(i.ToString());
    }

    public event EventHandler<SettingsClosedEventArgs>? Closed;

    public void Load(SettingsStore settings)
    {
        _settings = settings;
        DeckControlSwitch.IsOn = settings.DeckControl;

        AviTypeButtons.SelectedIndex = settings.Type2AVI ? 1 : 0;
        ThresholdBox.Value = settings.DiscontinuityThreshold;
        MaxFramesBox.Value = settings.MaxAVIFrames;
        EveryNthBox.Value = settings.EveryNth;

        DateFormatBox.Items.Clear();
        DateFormatBox.Items.Add(settings.DateTimeFormat);
        foreach (string format in settings.DateTimeFormatHistory.Where(f => f != settings.DateTimeFormat))
            DateFormatBox.Items.Add(format);
        if (settings.DateTimeFormat.Length > 0)
            DateFormatBox.Items.Add(""); // offer "no date in the name"
        DateFormatBox.Text = settings.DateTimeFormat;
        DateFormatBox.SelectedIndex = 0;
        DigitsBox.SelectedIndex = Math.Clamp(settings.SuffixDigits, 0, MaxSuffixDigits);

        PrefixBox.Text = settings.AVIPrefix;
        SuffixBox.Text = settings.AVISuffix;
        RecordPreviewSwitch.IsOn = settings.RecordPreview;

        ValidationText.Text = "";
        UpdateExample();
    }

    private string CurrentFormat => DateFormatBox.Text ?? "";

    private void DateFormatBox_TextSubmitted(ComboBox sender, ComboBoxTextSubmittedEventArgs args)
    {
        // Keep the typed text rather than matching it against the list.
        args.Handled = true;
        UpdateExample();
    }

    private void DateFormatBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (DateFormatBox.SelectedItem is string format)
            DateFormatBox.Text = format;
        UpdateExample();
    }

    private void DigitsBox_SelectionChanged(object sender, SelectionChangedEventArgs e) => UpdateExample();

    // Shows what a capture file name looks like with these settings.
    private void UpdateExample()
    {
        string format = CurrentFormat;
        if (!DVEngine.IsValidTimeFormat(format))
        {
            ExampleText.Text = "(the date format has an invalid % code)";
            return;
        }
        string example = "capture";
        string date = DVEngine.FormatNow(format);
        if (date.Length > 0)
            example += "." + date;
        int digits = Math.Max(DigitsBox.SelectedIndex, 0);
        if (digits > 0)
            example += "." + new string('0', digits);
        ExampleText.Text = example + ".avi";
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        if (_settings is null)
            return;
        string format = CurrentFormat;
        if (!DVEngine.IsValidTimeFormat(format))
        {
            ValidationText.Text = "The date format has an invalid % code.";
            return;
        }

        _settings.DeckControl = DeckControlSwitch.IsOn;
        _settings.Type2AVI = AviTypeButtons.SelectedIndex == 1;
        _settings.DiscontinuityThreshold = ReadNumber(ThresholdBox, 0, _settings.DiscontinuityThreshold);
        _settings.MaxAVIFrames = ReadNumber(MaxFramesBox, 10, _settings.MaxAVIFrames);
        _settings.EveryNth = ReadNumber(EveryNthBox, 1, _settings.EveryNth);
        _settings.UseDateTimeFormat(format);
        _settings.SuffixDigits = Math.Max(DigitsBox.SelectedIndex, 0);
        _settings.AVIPrefix = PrefixBox.Text.Trim();
        _settings.AVISuffix = SuffixBox.Text.Trim();
        _settings.RecordPreview = RecordPreviewSwitch.IsOn;

        Closed?.Invoke(this, new SettingsClosedEventArgs(saved: true));
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) =>
        Closed?.Invoke(this, new SettingsClosedEventArgs(saved: false));

    private static int ReadNumber(NumberBox box, int minimum, int fallback) =>
        double.IsNaN(box.Value) ? fallback : Math.Max(minimum, (int)Math.Round(box.Value));
}
