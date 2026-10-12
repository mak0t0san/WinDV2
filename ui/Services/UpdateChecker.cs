using System.Diagnostics.CodeAnalysis;
using System.Net.Http.Headers;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace WinDV.Services;

/// <summary>One release asset, e.g. "WinDV-2.5.1-x64-Setup.exe" with its download URL.</summary>
public sealed record UpdateAsset(string Name, Uri Url);

public enum UpdateCheckOutcome
{
    /// <summary>GitHub was reached; the running version is current.</summary>
    UpToDate,

    /// <summary>GitHub was reached and reports a newer release.</summary>
    Available,

    /// <summary>GitHub couldn't be reached, or gave an unexpected answer.</summary>
    CheckFailed,
}

/// <summary>The result of asking GitHub for WinDV's latest release.</summary>
public sealed record UpdateCheckResult(UpdateCheckOutcome Outcome, string? Tag, IReadOnlyList<UpdateAsset> Assets)
{
    public static readonly UpdateCheckResult Failed = new(UpdateCheckOutcome.CheckFailed, null, []);
}

/// <summary>
/// Asks GitHub for WinDV's latest release. Only the release's tag and asset list are
/// read; nothing is sent beyond an ordinary request with WinDV's version as the user
/// agent, and nothing is downloaded unless a caller explicitly asks for an asset.
/// </summary>
public static class UpdateChecker
{
    private const string LatestReleaseUrl = "https://api.github.com/repos/mak0t0san/WinDV2/releases/latest";
    private const string ReleasePageUrl = "https://github.com/mak0t0san/WinDV2/releases/tag/";

    public static Version CurrentVersion { get; } =
        Normalize(typeof(UpdateChecker).Assembly.GetName().Version ?? new Version(0, 0, 0));

    /// <summary>Never throws: any failure comes back as <see cref="UpdateCheckResult.Failed"/>.</summary>
    public static async Task<UpdateCheckResult> GetLatestReleaseAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
            http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("WinDV", CurrentVersion.ToString(3)));
            http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            await using Stream body = await http.GetStreamAsync(LatestReleaseUrl, cancellationToken);
            // JsonDocument rather than a deserializer: nothing to generate for AOT.
            using JsonDocument json = await JsonDocument.ParseAsync(body, cancellationToken: cancellationToken);
            JsonElement root = json.RootElement;

            if (!root.TryGetProperty("tag_name", out JsonElement tagElement) ||
                tagElement.ValueKind != JsonValueKind.String)
            {
                return UpdateCheckResult.Failed;
            }

            string? tag = tagElement.GetString();
            if (!TryParseVersion(tag, out _))
            {
                return UpdateCheckResult.Failed;
            }

            var assets = new List<UpdateAsset>();
            if (root.TryGetProperty("assets", out JsonElement assetsElement) &&
                assetsElement.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement asset in assetsElement.EnumerateArray())
                {
                    if (asset.TryGetProperty("name", out JsonElement nameElement) &&
                        nameElement.ValueKind == JsonValueKind.String &&
                        nameElement.GetString() is string name &&
                        asset.TryGetProperty("browser_download_url", out JsonElement urlElement) &&
                        urlElement.ValueKind == JsonValueKind.String &&
                        Uri.TryCreate(urlElement.GetString(), UriKind.Absolute, out Uri? url) &&
                        IsValidAssetUrl(url))
                    {
                        assets.Add(new UpdateAsset(name, url));
                    }
                }
            }

            UpdateCheckOutcome outcome = NewerThanCurrent(tag) is not null
                ? UpdateCheckOutcome.Available
                : UpdateCheckOutcome.UpToDate;
            return new UpdateCheckResult(outcome, tag, assets);
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException or JsonException or IOException)
        {
            return UpdateCheckResult.Failed; // offline, rate-limited, or GitHub is down: try again next time
        }
    }

    /// <summary>Ensures the download URL is HTTPS and points to trusted GitHub endpoints.</summary>
    public static bool IsValidAssetUrl(Uri? url) =>
        url is not null &&
        url.Scheme == Uri.UriSchemeHttps &&
        (string.Equals(url.Host, "github.com", StringComparison.OrdinalIgnoreCase) ||
         url.Host.EndsWith(".github.com", StringComparison.OrdinalIgnoreCase) ||
         string.Equals(url.Host, "githubusercontent.com", StringComparison.OrdinalIgnoreCase) ||
         url.Host.EndsWith(".githubusercontent.com", StringComparison.OrdinalIgnoreCase));

    /// <summary>"v2.2.3" -> 2.2.3. A pre-release suffix ("-beta", "+abc") is ignored.</summary>
    public static bool TryParseVersion(string? text, [NotNullWhen(true)] out Version? version)
    {
        version = null;
        if (string.IsNullOrEmpty(text))
        {
            return false;
        }

        string core = text.TrimStart('v', 'V').Split('-', '+')[0];
        if (!Version.TryParse(core, out Version? parsed))
        {
            return false;
        }

        version = Normalize(parsed);
        return true;
    }

    /// <summary>A newer version than this one, or null.</summary>
    public static Version? NewerThanCurrent(string? tag) =>
        TryParseVersion(tag, out Version? version) && version > CurrentVersion ? version : null;

    public static Uri ReleasePage(string tag) => new(ReleasePageUrl + Uri.EscapeDataString(tag));

    /// <summary>
    /// The installer asset matching this process's own architecture for the given release
    /// tag (build\package.ps1 names it "WinDV-&lt;version&gt;-&lt;arch&gt;-Setup.exe"), or null
    /// if there isn't one - an unexpected asset list, or a future architecture.
    /// </summary>
    public static UpdateAsset? FindInstallerAsset(IReadOnlyList<UpdateAsset> assets, string tag)
    {
        string? arch = RuntimeInformation.ProcessArchitecture switch
        {
            Architecture.X64 => "x64",
            Architecture.X86 => "x86",
            _ => null,
        };
        if (arch is null || !TryParseVersion(tag, out Version? version))
        {
            return null;
        }

        string expected = $"WinDV-{version.ToString(3)}-{arch}-Setup.exe";
        foreach (UpdateAsset asset in assets)
        {
            if (string.Equals(asset.Name, expected, StringComparison.OrdinalIgnoreCase))
            {
                return asset;
            }
        }

        return null;
    }

    // Major.minor.build only: Version counts 2.2.2 and 2.2.2.0 as different.
    private static Version Normalize(Version v) => new(v.Major, v.Minor, Math.Max(v.Build, 0));
}
