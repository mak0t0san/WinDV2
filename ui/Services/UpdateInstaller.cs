using System.Diagnostics;
using System.Net;
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
        if (!UpdateChecker.IsValidAssetUrl(assetUrl))
        {
            throw new ArgumentException("Untrusted or invalid update URL.", nameof(assetUrl));
        }

        // Redirects are followed by hand so every hop can be checked against the allow-list.
        using var handler = new HttpClientHandler { AllowAutoRedirect = false };
        using var http = new HttpClient(handler) { Timeout = Timeout.InfiniteTimeSpan };
        http.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("WinDV", UpdateChecker.CurrentVersion.ToString(3)));

        DeleteStaleDownloads();
        // HttpClient.Timeout stops applying once the headers arrive, so one token covers
        // the whole download, including the body.
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(5));
        CancellationToken token = timeout.Token;

        string path = Path.Combine(Path.GetTempPath(), $"WinDV-Update-{Guid.NewGuid():N}.exe");
        try
        {
            using HttpResponseMessage response = await GetFollowingRedirectsAsync(http, assetUrl, token);
            await using Stream source = await response.Content.ReadAsStreamAsync(token);
            await using FileStream destination = File.Create(path);
            await source.CopyToAsync(destination, token);
            return path;
        }
        catch
        {
            TryDeleteFile(path);
            throw;
        }
    }

    private const int MaxRedirects = 5;

    // A launched installer can't delete itself while it runs, so earlier attempts'
    // files are cleared at the start of the next download (locked ones are skipped).
    private static void DeleteStaleDownloads()
    {
        try
        {
            foreach (string file in Directory.EnumerateFiles(Path.GetTempPath(), "WinDV-Update-*.exe"))
            {
                TryDeleteFile(file);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            // Best-effort cleanup
        }
    }

    private static async Task<HttpResponseMessage> GetFollowingRedirectsAsync(
        HttpClient http, Uri url, CancellationToken cancellationToken)
    {
        for (int hop = 0; hop <= MaxRedirects; hop++)
        {
            HttpResponseMessage response = await http.GetAsync(
                url, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
            if (!response.StatusCode.IsRedirect())
            {
                try
                {
                    return response.EnsureSuccessStatusCode();
                }
                catch
                {
                    response.Dispose();
                    throw;
                }
            }

            Uri? location = response.Headers.Location;
            response.Dispose();
            if (location is null)
            {
                throw new HttpRequestException("Redirect without a location.");
            }

            url = location.IsAbsoluteUri ? location : new Uri(url, location);
            if (!UpdateChecker.IsValidAssetUrl(url))
            {
                throw new HttpRequestException("Update download redirected to an untrusted URL.");
            }
        }

        throw new HttpRequestException("Too many redirects.");
    }

    private static bool IsRedirect(this HttpStatusCode code) =>
        code is HttpStatusCode.MovedPermanently or HttpStatusCode.Found or HttpStatusCode.SeeOther
            or HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect;

    /// <summary>Deletes a temporary file safely, ignoring any errors.</summary>
    public static void TryDeleteFile(string? path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return;
        }

        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
            // Best-effort cleanup
        }
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
