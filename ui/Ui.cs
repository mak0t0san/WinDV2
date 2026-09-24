using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace WinDV;

/// <summary>Small helpers for x:Bind function bindings in XAML.</summary>
public static class Ui
{
    public static Visibility Show(bool value) => value ? Visibility.Visible : Visibility.Collapsed;

    public static Visibility ShowText(string value) =>
        string.IsNullOrEmpty(value) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>A transport button lights up (accent fill) while its mode is active.</summary>
    public static Style TransportStyle(bool active) =>
        (Style)Application.Current.Resources[active ? "TransportActiveButtonStyle" : "TransportButtonStyle"];

    public static Style RecStyle(bool capturing) =>
        (Style)Application.Current.Resources[capturing ? "RecActiveButtonStyle" : "RecButtonStyle"];

    public static Brush RecDot(bool capturing) => capturing
        ? new SolidColorBrush(Microsoft.UI.Colors.White)
        : (Brush)Application.Current.Resources["RecBrush"];

    public static string RecLabel(bool capturing) => capturing ? "Stop REC" : "REC";

    public static Brush SignalBrush(bool hasSignal) => (Brush)Application.Current.Resources[
        hasSignal ? "SystemFillColorSuccessBrush" : "SystemFillColorCautionBrush"];
}
