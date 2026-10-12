using Microsoft.Win32;

namespace WinDV.Services;

/// <summary>
/// Whether this is the installed WinDV, detected through its own uninstall registry
/// entry. The portable zip never has one, wherever its WinDV.exe happens to run from.
/// </summary>
public static class InstallInfo
{
    // installer/WinDV.iss [Setup] AppId. One id for both architectures.
    private const string UninstallKeyPath =
        @"Software\Microsoft\Windows\CurrentVersion\Uninstall\{9D543489-B23F-4B57-ABCD-CA1B11302AF5}_is1";

    /// <summary>
    /// Checks both the per-user key (the installer's default) and the machine-wide one
    /// (used only if the user chose an all-users install). Each hive is read through the
    /// registry view that matches this process's own architecture, which is also the view
    /// the installer itself wrote through when it ran with the same architecture.
    /// </summary>
    public static bool IsInstalledCopy()
    {
        return RunsFromInstallLocation(Registry.CurrentUser) || RunsFromInstallLocation(Registry.LocalMachine);
    }

    // The entry only proves WinDV is installed somewhere: a portable copy on the same
    // machine must not count, or "Update Now" would update the other copy instead.
    private static bool RunsFromInstallLocation(RegistryKey hive)
    {
        using RegistryKey? key = hive.OpenSubKey(UninstallKeyPath);
        if (key?.GetValue("InstallLocation") is not string location || location.Length == 0)
        {
            return false;
        }

        try
        {
            return string.Equals(
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(location)),
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(AppContext.BaseDirectory)),
                StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }
}
