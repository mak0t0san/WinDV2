using System.Diagnostics;
using System.Net.Http.Headers;

namespace WinDV.Services;

/// <summary>
/// Downloads an installer asset and runs it. Only ever called after the user clicks
/// "Update Now" - nothing here happens on its own.
/// </summary>
public static class UpdateInstaller
{
    /// <summary>Downloads the asset to a temp file and returns its path. May throw
    /// on any network or disk failure; the caller decides how to report that.</summary>
    public static async Task<string> DownloadAsync(Uri assetUrl, CancellationToken cancellationToken = default)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
        http.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("WinDV", UpdateChecker.CurrentVersion.ToString(3)));

        string path = Path.Combine(Path.GetTempPath(), $"WinDV-Update-{Guid.NewGuid():N}.exe");
        await using Stream source = await http.GetStreamAsync(assetUrl, cancellationToken);
        await using FileStream destination = File.Create(path);
        await source.CopyToAsync(destination, cancellationToken);
        return path;
    }

    /// <summary>
    /// Launches the downloaded installer silently. The installer closes WinDV itself
    /// (WinDV.iss's CloseApplications) and relaunches it once done, so this doesn't wait
    /// for it to finish; the caller still exits its own process right after calling this,
    /// so the installer isn't fighting a running instance.
    /// </summary>
    public static void LaunchInstaller(string installerPath) =>
        Process.Start(new ProcessStartInfo(installerPath)
        {
            Arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART",
            UseShellExecute = true, // lets Windows prompt for elevation if this was an all-users install
        });
}
