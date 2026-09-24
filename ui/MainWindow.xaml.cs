using System.ComponentModel;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics;
using Windows.Storage;
using Windows.Storage.Pickers;
using WinDV.Interop;
using WinDV.Services;
using WinDV.ViewModels;
using WinDV.Views;
using WinRT.Interop;

namespace WinDV;

public sealed partial class MainWindow : Window, INotifyPropertyChanged
{
    // Smallest useful window, in view pixels (scaled by the monitor's DPI).
    private const int MinWidth = 820;
    private const int MinHeight = 640;

    private readonly SettingsStore _settings;
    private readonly DvEngine _engine;
    private readonly nint _hwnd;
    private int _modalCount;
    private bool _closing;
    private bool _readyToClose;

    public MainWindow()
    {
        InitializeComponent();

        _settings = SettingsStore.Load();
        TrySetWorkingDirectory(_settings.WorkingDirectory);
        ViewModel = new MainViewModel(_settings, DispatcherQueue);

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        SystemBackdrop = new MicaBackdrop();
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "WinDV.ico"));
        RestoreWindowPlacement();

        _hwnd = WindowNative.GetWindowHandle(this);
        _engine = DvEngine.Create(_hwnd, DispatcherQueue);
        ViewModel.Attach(_engine);
        ViewModel.PropertyChanged += ViewModel_PropertyChanged;
        ViewModel.CloseRequested += (_, _) => _ = ShutdownAndCloseAsync(saveSettings: true);

        (ViewModel.SelectedTool == Tool.Record ? RecordItem : CaptureItem).IsSelected = true;

        SizeChanged += (_, _) => UpdatePreview();
        AppWindow.Closing += AppWindow_Closing;
        Root.Loaded += Root_Loaded;
    }

    public MainViewModel ViewModel { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsSettingsVisible { get; private set; }
    public bool IsMainViewVisible => !IsSettingsVisible;

    // ------------------------------------------------------------------ Startup / shutdown

    private async void Root_Loaded(object sender, RoutedEventArgs e)
    {
        Root.Loaded -= Root_Loaded;
        UpdatePreview();

        string[] args = Environment.GetCommandLineArgs().Skip(1).ToArray();
        ParsedCommandLine? commandLine = DvEngine.ParseCommandLine(args);
        if (commandLine is null)
        {
            await ShowDialogAsync(new ContentDialog
            {
                Title = "WinDV command line",
                Content = new TextBlock
                {
                    Text = "Usage:\n\n    WinDV capture [-exit] [[HH:]MI:]SS[.ss] filename\n\n" +
                           "    WinDV record [-exit] filename [filename ...]",
                    FontFamily = new FontFamily("Cascadia Mono,Consolas"),
                    IsTextSelectionEnabled = true,
                },
                CloseButtonText = "Close",
            });
            await ShutdownAndCloseAsync(saveSettings: false); // as before: a usage error changes nothing
            return;
        }

        bool firstRun = _settings.IsFirstRun;
        await ViewModel.StartAsync(commandLine);
        if (commandLine.Mode != CommandLineMode.Interactive)
        {
            return; // a scripted capture or record: no notices
        }

        _ = ViewModel.CheckForUpdatesAsync();
        if (firstRun)
        {
            await ShowAboutAsync();
        }
    }

    private void AppWindow_Closing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_readyToClose)
        {
            return;
        }

        args.Cancel = true;
        _ = ShutdownAndCloseAsync(saveSettings: true);
    }

    // Every close goes through here: Window.Close() alone doesn't raise
    // AppWindow.Closing, and the engine must be shut down before the window goes.
    private async Task ShutdownAndCloseAsync(bool saveSettings)
    {
        if (_closing)
        {
            return;
        }

        _closing = true;

        if (saveSettings)
        {
            SaveWindowPlacement();
        }

        await ViewModel.ShutdownAsync();
        ViewModel.Detach();
        if (saveSettings)
        {
            _settings.WorkingDirectory = Environment.CurrentDirectory;
            try
            {
                _settings.Save();
            }
            catch (Exception e) when (e is UnauthorizedAccessException or IOException)
            {
                // Settings are a convenience; never block closing on them.
            }
        }
        _engine.Dispose();

        _readyToClose = true;
        Close();
    }

    private void RestoreWindowPlacement()
    {
        double scale = GetDpiForWindowScale();
        int minW = (int)(MinWidth * scale), minH = (int)(MinHeight * scale);
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = minW;
            presenter.PreferredMinimumHeight = minH;
        }

        // The original WinDV's window was much smaller; grow old sizes to fit.
        int width = Math.Max(_settings.WindowWidth, minW);
        int height = Math.Max(_settings.WindowHeight, minH);
        if (_settings.IsFirstRun)
        {
            AppWindow.Resize(new SizeInt32((int)(1040 * scale), (int)(780 * scale)));
            return;
        }

        var rect = new RectInt32(_settings.WindowX, _settings.WindowY, width, height);
        DisplayArea area = DisplayArea.GetFromRect(rect, DisplayAreaFallback.Nearest);
        RectInt32 work = area.WorkArea;
        rect.Width = Math.Min(rect.Width, work.Width);
        rect.Height = Math.Min(rect.Height, work.Height);
        rect.X = Math.Clamp(rect.X, work.X, work.X + work.Width - rect.Width);
        rect.Y = Math.Clamp(rect.Y, work.Y, work.Y + work.Height - rect.Height);
        AppWindow.MoveAndResize(rect);
    }

    private void SaveWindowPlacement()
    {
        if (AppWindow.Presenter is OverlappedPresenter { State: not OverlappedPresenterState.Restored })
        {
            return; // keep the last normal placement
        }

        _settings.WindowX = AppWindow.Position.X;
        _settings.WindowY = AppWindow.Position.Y;
        _settings.WindowWidth = AppWindow.Size.Width;
        _settings.WindowHeight = AppWindow.Size.Height;
    }

    private double GetDpiForWindowScale()
    {
        nint hwnd = WindowNative.GetWindowHandle(this);
        return Win32.GetDpiForWindow(hwnd) / 96.0;
    }

    private static void TrySetWorkingDirectory(string directory)
    {
        try
        {
            if (Directory.Exists(directory))
            {
                Environment.CurrentDirectory = directory;
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
        {
        }
    }

    // ------------------------------------------------------------------ Preview placement

    private void PreviewHost_SizeChanged(object sender, SizeChangedEventArgs e) => UpdatePreview();

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(MainViewModel.HasPicture) or nameof(MainViewModel.SelectedTool))
        {
            UpdatePreview();
        }
    }

    // The preview is a native child window, so it is positioned by hand over
    // PreviewHost and hidden whenever XAML needs to draw in that area.
    private void UpdatePreview()
    {
        if (Root.XamlRoot is null || IsSettingsVisible || _modalCount > 0 || !ViewModel.HasPicture ||
            PreviewHost.ActualWidth < 1 || PreviewHost.ActualHeight < 1)
        {
            _engine.HidePreview();
            return;
        }

        double scale = Root.XamlRoot.RasterizationScale;
        var origin = PreviewHost.TransformToVisual(Root).TransformPoint(new Windows.Foundation.Point(0, 0));
        // Inset by the corner radius so the square window doesn't poke out.
        const double Inset = 2;
        int x = (int)Math.Round((origin.X + Inset) * scale);
        int y = (int)Math.Round((origin.Y + Inset) * scale);
        int w = (int)Math.Round((PreviewHost.ActualWidth - 2 * Inset) * scale);
        int h = (int)Math.Round((PreviewHost.ActualHeight - 2 * Inset) * scale);
        _engine.MovePreview(x, y, w, h);
    }

    private async Task<ContentDialogResult> ShowDialogAsync(ContentDialog dialog)
    {
        dialog.XamlRoot = Root.XamlRoot;
        _modalCount++;
        UpdatePreview();
        try
        {
            return await dialog.ShowAsync();
        }
        finally
        {
            _modalCount--;
            UpdatePreview();
        }
    }

    // ------------------------------------------------------------------ Tool, devices, files

    private void ToolSelector_SelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
    {
        ViewModel.SelectedTool = sender.SelectedItem == RecordItem ? Tool.Record : Tool.Capture;
    }

    private void CaptureDevice_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (CaptureDeviceBox.SelectedItem is string device && device != ViewModel.CaptureDevice)
        {
            ViewModel.CaptureDevice = device;
        }
    }

    private void RecordDevice_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (RecordDeviceBox.SelectedItem is string device && device != ViewModel.RecordDevice)
        {
            ViewModel.RecordDevice = device;
        }
    }

    private async void BrowseCapture_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileSavePicker { SuggestedStartLocation = PickerLocationId.VideosLibrary };
        picker.FileTypeChoices.Add("AVI video", [".avi"]);
        string current = ViewModel.CaptureFile.Trim();
        picker.SuggestedFileName = current.Length > 0 ? Path.GetFileName(current) : "capture";
        InitializeWithWindow.Initialize(picker, _hwnd);

        StorageFile? file = await picker.PickSaveFileAsync();
        if (file is null)
        {
            return;
        }

        // The picker creates an empty placeholder file; captures get their own names.
        await DeletePlaceholderAsync(file);
        TrySetWorkingDirectory(Path.GetDirectoryName(file.Path) ?? ".");
        ViewModel.CaptureFile = DvEngine.CaptureBase(file.Path);
    }

    private static async Task DeletePlaceholderAsync(StorageFile file)
    {
        try
        {
            var properties = await file.GetBasicPropertiesAsync();
            if (properties.Size == 0)
            {
                await file.DeleteAsync();
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }

    private async void BrowseRecord_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.VideosLibrary };
        picker.FileTypeFilter.Add(".avi");
        picker.FileTypeFilter.Add("*");
        InitializeWithWindow.Initialize(picker, _hwnd);

        IReadOnlyList<StorageFile> files = await picker.PickMultipleFilesAsync();
        if (files.Count == 0)
        {
            return;
        }

        TrySetWorkingDirectory(Path.GetDirectoryName(files[0].Path) ?? ".");
        ViewModel.RecordFiles = string.Join(" | ", files.Select(f => f.Path));
    }

    private void Files_DragOver(object sender, DragEventArgs e)
    {
        if (e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            e.AcceptedOperation = DataPackageOperation.Link;
            e.DragUIOverride.Caption = ViewModel.IsCaptureTool ? "Save captures here" : "Record to tape";
        }
    }

    private async void Files_Drop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            return;
        }

        var paths = (await e.DataView.GetStorageItemsAsync()).Select(i => i.Path).Where(p => p.Length > 0).ToList();
        if (paths.Count == 0)
        {
            return;
        }

        if (ViewModel.IsCaptureTool)
        {
            string path = paths[0];
            // A dropped folder means "capture into this folder".
            ViewModel.CaptureFile = Directory.Exists(path)
                ? Path.Combine(path, "capture")
                : DvEngine.CaptureBase(path);
        }
        else
        {
            ViewModel.RecordFiles = string.Join(" | ", paths);
        }
    }

    private void ErrorBar_Closed(InfoBar sender, InfoBarClosedEventArgs args) => ViewModel.DismissError();

    private void UpdateBar_Closed(InfoBar sender, InfoBarClosedEventArgs args)
    {
        if (args.Reason == InfoBarCloseReason.CloseButton)
        {
            ViewModel.DismissUpdate();
        }
    }

    // ------------------------------------------------------------------ Settings and About

    private void Settings_Click(object sender, RoutedEventArgs e)
    {
        SettingsPanel.Load(_settings);
        SetSettingsVisible(true);
    }

    private void SettingsPanel_Closed(object? sender, SettingsClosedEventArgs e)
    {
        if (e.Saved)
        {
            ViewModel.ApplySettings();
        }

        SetSettingsVisible(false);
    }

    private void SetSettingsVisible(bool visible)
    {
        IsSettingsVisible = visible;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsSettingsVisible)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsMainViewVisible)));
        UpdatePreview();
    }

    private async void About_Click(object sender, RoutedEventArgs e) => await ShowAboutAsync();

    private Task ShowAboutAsync() => ShowDialogAsync(AboutDialog.Create());
}

internal static partial class Win32
{
    [System.Runtime.InteropServices.LibraryImport("user32.dll")]
    public static partial uint GetDpiForWindow(nint hwnd);
}
