#include "theme_manager.h"

#include <QApplication>
#include <QStringLiteral>

namespace vedit::app {

namespace {

QColor safeColor(QRgb rgb, const QColor &fallback)
{
    const QColor color(rgb);
    return color.isValid() ? color : fallback;
}

} // namespace

ThemeManager::ThemeManager()
    : palette_(QApplication::palette())
{
}

QStringList ThemeManager::availableThemes() const
{
    QStringList names;
    const auto themes = repository_.themes();
    names.reserve(themes.size());
    for (const KSyntaxHighlighting::Theme &theme : themes) {
        names.append(theme.name());
    }
    return names;
}

QString ThemeManager::defaultThemeName() const
{
    const KSyntaxHighlighting::Theme theme =
        repository_.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    if (!theme.name().isEmpty()) {
        return theme.name();
    }
    const auto themes = repository_.themes();
    return themes.isEmpty() ? QStringLiteral("Breeze Light") : themes.first().name();
}

bool ThemeManager::resolveTheme(const QString &name, KSyntaxHighlighting::Theme *theme,
                                QString *error) const
{
    if (name.isEmpty()) {
        *theme = repository_.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
        return theme->isValid();
    }
    *theme = repository_.theme(name);
    if (!theme->isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("unknown theme '%1' (or not loaded by KSyntaxHighlighting)").arg(name);
        }
        return false;
    }
    return true;
}

QPalette ThemeManager::paletteForTheme(const KSyntaxHighlighting::Theme &theme)
{
    using KSTextStyle = KSyntaxHighlighting::Theme::TextStyle;
    using KSEditorColor = KSyntaxHighlighting::Theme::EditorColorRole;

    const QColor background =
        safeColor(theme.editorColor(KSEditorColor::BackgroundColor), QColor(255, 255, 255));
    const QColor foreground = safeColor(theme.textColor(KSTextStyle::Normal), QColor(20, 20, 20));
    QColor selection = safeColor(theme.editorColor(KSEditorColor::TextSelection), background.darker(125));
    if (!selection.isValid() || selection == background) {
        selection = background.lighter(160);
    }
    const bool dark = background.lightness() < 128;
    const QColor window = dark ? background : background.darker(104);
    const QColor button = dark ? background.lighter(115) : background.darker(108);
    const QColor base = background;
    const QColor alternateBase = dark ? background.lighter(108) : background.darker(103);
    const QColor tooltipBase = dark ? background.lighter(112) : background.lighter(103);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, foreground);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternateBase);
    palette.setColor(QPalette::Text, foreground);
    palette.setColor(QPalette::PlaceholderText, foreground.lighter(dark ? 150 : 160));
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, foreground);
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, dark ? QColor(20, 20, 20) : QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipBase, tooltipBase);
    palette.setColor(QPalette::ToolTipText, foreground);
    palette.setColor(QPalette::Link, safeColor(theme.textColor(KSTextStyle::Function), selection));
    palette.setColor(QPalette::LinkVisited, safeColor(theme.textColor(KSTextStyle::Import), selection));
    return palette;
}

bool ThemeManager::applyTheme(const QString &name, QString *error)
{
    KSyntaxHighlighting::Theme theme;
    if (!resolveTheme(name, &theme, error)) {
        return false;
    }
    const QPalette palette = paletteForTheme(theme);
    QApplication::setPalette(palette);
    palette_ = palette;
    current_ = theme.name();
    return true;
}

QString ThemeManager::currentTheme() const
{
    return current_;
}

QColor ThemeManager::windowColor() const
{
    return palette_.color(QPalette::Window);
}

QColor ThemeManager::textColor() const
{
    return palette_.color(QPalette::Text);
}

QString ThemeManager::syntaxThemeName() const
{
    return current_;
}

} // namespace vedit::app
