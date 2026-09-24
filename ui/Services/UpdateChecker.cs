using System.Diagnostics.CodeAnalysis;
using System.Net.Http.Headers;
using System.Text.Json;

namespace WinDV.Services;

/// <summary>
/// Asks GitHub for WinDV's latest release. Only the release's tag is read; nothing
/// is sent beyond an ordinary request with WinDV's version as the user agent.
/// </summary>
public static class UpdateChecker
{
    private const string LatestReleaseUrl = "https://api.github.com/repos/mak0t0san/WinDV2/releases/latest";
    private const string ReleasePageUrl = "https://github.com/mak0t0san/WinDV2/releases/tag/";

    public static Version CurrentVersion { get; } =
        Normalize(typeof(UpdateChecker).Assembly.GetName().Version ?? new Version(0, 0, 0));

    /// <returns>The latest release's tag (e.g. "v2.2.3"), or null if GitHub couldn't be
    /// reached or gave an unexpected answer. Never throws.</returns>
    public static async Task<string?> GetLatestTagAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
            http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("WinDV", CurrentVersion.ToString(3)));
            http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            await using Stream body = await http.GetStreamAsync(LatestReleaseUrl, cancellationToken);
            // JsonDocument rather than a deserializer: nothing to generate for AOT.
            using JsonDocument json = await JsonDocument.ParseAsync(body, cancellationToken: cancellationToken);
            return json.RootElement.TryGetProperty("tag_name", out JsonElement tag) &&
                   tag.ValueKind == JsonValueKind.String &&
                   TryParseVersion(tag.GetString(), out _)
                ? tag.GetString()
                : null;
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException or JsonException or IOException)
        {
            return null; // offline, rate-limited, or GitHub is down: try again next time
        }
    }

    /// <summary>"v2.2.3" -> 2.2.3. A pre-release suffix ("-beta", "+abc") is ignored.</summary>
    public static bool TryParseVersion(string? text, [NotNullWhen(true)] out Version? version)
    {
        version = null;
        if (string.IsNullOrEmpty(text))
            return false;
        string core = text.TrimStart('v', 'V').Split('-', '+')[0];
        if (!Version.TryParse(core, out Version? parsed))
            return false;
        version = Normalize(parsed);
        return true;
    }

    /// <summary>A newer version than this one, or null.</summary>
    public static Version? NewerThanCurrent(string? tag) =>
        TryParseVersion(tag, out Version? version) && version > CurrentVersion ? version : null;

    public static Uri ReleasePage(string tag) => new(ReleasePageUrl + Uri.EscapeDataString(tag));

    // Major.minor.build only: Version counts 2.2.2 and 2.2.2.0 as different.
    private static Version Normalize(Version v) => new(v.Major, v.Minor, Math.Max(v.Build, 0));
}
