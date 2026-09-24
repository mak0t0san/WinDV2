using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;

namespace WinDV.Views;

internal static class AboutDialog
{
    public static ContentDialog Create()
    {
        var text = new RichTextBlock { TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };

        var intro = new Paragraph();
        intro.Inlines.Add(new Run { Text = "Capture DV video from a camcorder over FireWire, and record it back to tape." });
        text.Blocks.Add(intro);

        var credits = new Paragraph { Margin = new Thickness(0, 12, 0, 0) };
        credits.Inlines.Add(new Run { Text = "WinDV 1.2.3 by Petr Mourek, 2002-2003 (" });
        var site = new Hyperlink { NavigateUri = new Uri("http://windv.mourek.cz/") };
        site.Inlines.Add(new Run { Text = "windv.mourek.cz" });
        credits.Inlines.Add(site);
        credits.Inlines.Add(new Run { Text = ")." });
        text.Blocks.Add(credits);

        var version = new Paragraph { Margin = new Thickness(0, 12, 0, 0) };
        version.Inlines.Add(new Run
        {
            Text = $"Version {typeof(AboutDialog).Assembly.GetName().Version?.ToString(3)}, " +
                   $"{(Environment.Is64BitProcess ? "64-bit" : "32-bit")}",
        });
        text.Blocks.Add(version);

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
}
