#include "theme_manager.h"

#include "product_theme.h"
#include "themed_icons.h"

#include <QApplication>
#include <QStringLiteral>
#include <QCoreApplication>

#include <utility>

namespace vedit::app {

namespace {

QColor safeColor(QRgb rgb, const QColor &fallback)
{
    const QColor color(rgb);
    return color.isValid() ? color : fallback;
}

// RM-7: the theme is applied as a QPalette (the product has no .qss resources),
// and `QPalette::setColor(role, colour)` fills ALL THREE color groups — so a
// palette built only that way hands the style a Disabled group that is a
// byte-for-byte copy of Active. Every style then paints a functionally disabled
// control exactly like an enabled one (the "disabled" state was measured as
// identical for Text in 30/30 shipped themes; see
// evidence/ux-batch-2/rm7-disabled-look/).
//
// The Disabled group is therefore derived from the theme itself: each disabled
// text colour is moved a fixed fraction of the way from its normal colour toward
// the surface it is painted on. A fixed fraction (rather than a hard-coded grey)
// is what makes this hold for light and dark themes alike, and it leaves the
// enabled colours untouched, so an editable control (e.g. the Lint page's
// TransportPending knob) stays fully saturated and can never be confused with an
// unsupported (disabled) one.
//
// 55% is measured, not guessed: across the 30 themes the smallest resulting
// channel delta is 72/255, the smallest relative-luminance delta is 0.183 and the
// smallest enabled/disabled contrast-ratio drop is 2.13x (the worst theme is
// Solarized Dark for luminance and Solarized Light for the contrast drop).
// `vedit-app-disabled-look-selftest` asserts floors of 40/255, 0.10 and 1.40x per
// theme and per primary role.
constexpr int kDisabledTextBlendPercent = 55;

QColor blendToward(const QColor &color, const QColor &surface, int percent)
{
    const qreal weight = qBound(0, percent, 100) / 100.0;
    return QColor(qRound(color.red() * (1.0 - weight) + surface.red() * weight),
                  qRound(color.green() * (1.0 - weight) + surface.green() * weight),
                  qRound(color.blue() * (1.0 - weight) + surface.blue() * weight));
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
            *error = QCoreApplication::translate("VEdit", "unknown theme '%1' (or not loaded by KSyntaxHighlighting)").arg(name);
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
    const QColor placeholderText = foreground.lighter(dark ? 150 : 160);
    const QColor highlightText = dark ? QColor(20, 20, 20) : QColor(255, 255, 255);
    const QColor brightText(255, 255, 255);
    const QColor link = safeColor(theme.textColor(KSTextStyle::Function), selection);
    const QColor linkVisited = safeColor(theme.textColor(KSTextStyle::Import), selection);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, foreground);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternateBase);
    palette.setColor(QPalette::Text, foreground);
    palette.setColor(QPalette::PlaceholderText, placeholderText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, foreground);
    palette.setColor(QPalette::BrightText, brightText);
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, highlightText);
    palette.setColor(QPalette::ToolTipBase, tooltipBase);
    palette.setColor(QPalette::ToolTipText, foreground);
    palette.setColor(QPalette::Link, link);
    palette.setColor(QPalette::LinkVisited, linkVisited);

    // RM-7: every setColor(role, colour) above filled the Disabled group with
    // the same value as Active, which is why nothing looked disabled. Build the
    // Disabled group here instead: the text roles move toward the surface they
    // are drawn on, so the dimming is real in light AND dark themes. Background
    // roles (Window/Base/Button) stay as they are — Qt's own styles keep a
    // disabled field/button background unchanged, and dimming them as well would
    // make the enabled-but-pending state harder to tell apart.
    palette.setColor(QPalette::Disabled, QPalette::WindowText,
                     blendToward(foreground, window, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     blendToward(foreground, base, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     blendToward(foreground, button, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::ToolTipText,
                     blendToward(foreground, tooltipBase, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText,
                     blendToward(placeholderText, base, kDisabledTextBlendPercent));
    // A disabled selection has no "highlight text" of its own: paint it with the
    // ordinary disabled text colour, so it is unambiguously dimmer than the
    // enabled HighlightedText (which is forced to white/black) even on the themes
    // whose selection colour is close to that white/black.
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     blendToward(foreground, base, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Highlight,
                     blendToward(selection, base, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Link,
                     blendToward(link, window, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::LinkVisited,
                     blendToward(linkVisited, window, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::BrightText,
                     blendToward(brightText, base, kDisabledTextBlendPercent));
    return palette;
}

bool ThemeManager::applyTheme(const QString &name, QString *error)
{
    KSyntaxHighlighting::Theme theme;
    if (!resolveTheme(name, &theme, error)) {
        return false;
    }
    const QPalette palette = paletteForTheme(theme);
    // UI-Mosaic T1：产品主题接管外观之后，语法主题切换只影响编辑器配色，不再
    // 用语法配色重刷窗口 chrome——否则用户在首选项里换一个语法主题，卡片 /
    // 工具栏会瞬间变成另一套颜色（契约 §1.5：applyTheme 不得清掉产品主题）。
    // 未应用产品主题时保持既有语义逐字不变（既有测试与 RM-7 都依赖它）。
    if (!uiThemeActive_) {
        QApplication::setPalette(palette);
    }
    palette_ = palette;
    current_ = theme.name();
    // UI-Mosaic F2: the syntax theme is rendered by the editor's own theme
    // layer, not by the application palette (which a product theme now owns).
    // Hand the effective name to the installed consumer; with no consumer
    // (demo provider / no editor lane) this is a no-op, never an error.
    if (syntaxThemeApplier_) {
        syntaxThemeApplier_(current_);
    }
    return true;
}

void ThemeManager::setSyntaxThemeApplier(std::function<void(const QString &)> applier)
{
    syntaxThemeApplier_ = std::move(applier);
}

bool ThemeManager::applyUiTheme(const QString &id, QString *error)
{
    const QString wanted = id.isEmpty() ? defaultProductTheme().id : id;
    const ProductTheme *theme = productThemeById(wanted);
    if (theme == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "unknown UI theme '%1'").arg(wanted);
        }
        return false;
    }

    // 样式表是 QApplication 的实例方法（调色板和窗口图标是静态的），没有 GUI
    // 应用实例时不能假装应用成功——ThemeManager 的前提本来就是 GUI 应用。
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (application == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                "VEdit", "cannot apply a UI theme without a QApplication");
        }
        return false;
    }

    // 副作用顺序是契约（§1.5）的一部分：先调色板，让 QSS 没覆盖到的内建控件
    // 立刻拿到最终颜色；再应用卡片样式表；最后换窗口图标——图标只依赖主题的
    // dark 标志，不依赖调色板，放最后可以保证前两步失败时它仍是确定状态。
    const QPalette palette = productPalette(*theme);
    QApplication::setPalette(palette);
    application->setStyleSheet(renderStyleSheet(*theme));
    QApplication::setWindowIcon(themed_icons::appIcon(theme->dark));

    palette_ = palette;
    uiTheme_ = theme->id;
    uiThemeActive_ = true;
    return true;
}

QString ThemeManager::currentUiTheme() const
{
    return uiTheme_.isEmpty() ? defaultProductTheme().id : uiTheme_;
}

QStringList ThemeManager::uiThemeIds() const
{
    QStringList ids;
    const QVector<ProductTheme> &themes = productThemes();
    ids.reserve(themes.size());
    for (const ProductTheme &theme : themes) {
        ids.append(theme.id);
    }
    return ids;
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
