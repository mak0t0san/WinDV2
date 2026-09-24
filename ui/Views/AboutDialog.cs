using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;

namespace WinDV.Views;

internal static class AboutDialog
{
    public const string ProjectUrl = "https://github.com/mak0t0san/WinDV2";
    private const string OriginalUrl = "http://windv.mourek.cz/";

    public static ContentDialog Create()
    {
        var text = new RichTextBlock { TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };

        var intro = new Paragraph();
        intro.Inlines.Add(new Run { Text = "Capture DV video from a camcorder over FireWire, and record it back to tape." });
        text.Blocks.Add(intro);

        var version = new Paragraph { Margin = new Thickness(0, 12, 0, 0) };
        version.Inlines.Add(new Run
        {
            Text = $"WinDV {typeof(AboutDialog).Assembly.GetName().Version?.ToString(3)} " +
                   $"({(Environment.Is64BitProcess ? "64-bit" : "32-bit")}) by Makoto, 2026",
        });
        version.Inlines.Add(new LineBreak());
        version.Inlines.Add(Link(ProjectUrl, "github.com/mak0t0san/WinDV2"));
        text.Blocks.Add(version);

        var thanks = new Paragraph { Margin = new Thickness(0, 12, 0, 0) };
        thanks.Inlines.Add(new Run
        {
            Text = "With thanks to Petr Mourek, who wrote the original WinDV (2002-2003) and generously " +
                   "shared its source. His capture engine is still the heart of this version. ",
        });
        thanks.Inlines.Add(Link(OriginalUrl, "windv.mourek.cz"));
        text.Blocks.Add(thanks);

        var tips = new Paragraph { Margin = new Thickness(0, 12, 0, 0) };
        tips.Inlines.Add(new Run
        {
            Text = "Tip: put the camcorder in VCR (tape) mode. Only one program at a time can use it.",
        });
        text.Blocks.Add(tips);

        return new ContentDialog
        {
            Title = "About WinDV",
            Content = text,
            CloseButtonText = "Close",
            DefaultButton = ContentDialogButton.Close,
        };
    }

    private static Hyperlink Link(string url, string label)
    {
        var link = new Hyperlink { NavigateUri = new Uri(url) };
        link.Inlines.Add(new Run { Text = label });
        return link;
    }
}
