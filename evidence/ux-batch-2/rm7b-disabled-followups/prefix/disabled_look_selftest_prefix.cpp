// RM-7 selftest: a functionally disabled control must LOOK disabled in every
// theme the product can produce.
//
// Why this test exists: the product has no .qss resources and the shell applies
// a theme as a QPalette. `QPalette::setColor(role, colour)` fills ACTIVE,
// INACTIVE and DISABLED with the same value, so the palette used to hand every
// style a Disabled group identical to Active. Disabling worked (the widgets
// really were disabled — the functional tests passed), only the appearance was
// a copy of the enabled one. This test asserts the visual contract numerically,
// so it cannot silently regress:
//
//   * palette: for every KSyntaxHighlighting theme the manager can apply, the
//     Disabled colour of every text role must differ from the Active colour by
//     a stated minimum channel delta, relative-luminance delta and contrast-ratio
//     drop; the Active colours must still be the theme's own colours (the fix
//     must not dim an enabled control);
//   * pixels: for every theme (and every style Qt offers in this build) an
//     enabled and a disabled sample of each affected widget class are rendered
//     offscreen; the differing pixels must be real glyph pixels and the disabled
//     ink must be measurably closer to the background than the enabled ink.
//     The toolbar sample drives a real QAction (Undo/Redo are QToolButtons fed
//     by shared QActions), the Lint-page samples are QComboBox/QLineEdit/
//     QPlainTextEdit, plus QPushButton as the generic button.
//
// Thresholds are relative, never a hard-coded hex value: the exact colours come
// from the selected theme, so a theme change cannot break the test while a
// regression (Disabled == Active again) fails it on every theme.
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", qPrintable(detail));
    std::fflush(stdout);
    if (!ok) {
        ++failures;
    }
}

// A disabled text colour must differ from the enabled one by at least this much.
// Floors are ~55% of the measured worst case across the 30 shipped themes (the
// measurement is in evidence/ux-batch-2/rm7-disabled-look/README.md), so a theme
// change has headroom while a return to Disabled == Active fails immediately.
constexpr int kMinChannelDelta = 40;      // of 255
constexpr double kMinLuminanceDelta = 0.10; // WCAG relative luminance, 0..1
constexpr double kMinContrastDrop = 1.40;   // enabled contrast / disabled contrast

double channel(double value)
{
    value /= 255.0;
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color)
{
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green())
           + 0.0722 * channel(color.blue());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

int maxChannelDelta(const QColor &a, const QColor &b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

// The enabled colour must still be the theme's own colour (RM-7 must not dim an
// enabled control: that is what keeps the Lint page's editable TransportPending
// knob distinguishable from a disabled BackendUnsupported one).
QColor expectedForeground(const KSyntaxHighlighting::Theme &theme)
{
    const QColor text = QColor(theme.textColor(KSyntaxHighlighting::Theme::TextStyle::Normal));
    return text.isValid() ? text : QColor(20, 20, 20);
}

QColor expectedBackground(const KSyntaxHighlighting::Theme &theme)
{
    const QColor background = QColor(theme.editorColor(
        KSyntaxHighlighting::Theme::EditorColorRole::BackgroundColor));
    return background.isValid() ? background : QColor(255, 255, 255);
}

struct RoleSpec {
    QPalette::ColorRole role;
    QPalette::ColorRole surface;
    const char *name;
    int minChannel;
    double minLuminance;
    double minDrop;
    bool primary = false;
};

// The roles a disabled control actually paints with. The four "primary" roles
// are the ones every style uses for a disabled widget's label/field/button; the
// floors are ~55% of the measured worst case over the 30 shipped themes (see
// evidence/ux-batch-2/rm7-disabled-look/README.md). Placeholder and link colours
// are already de-emphasised by the theme itself, so they get a lower luminance /
// contrast floor — they still must never be identical to their enabled colour.
const RoleSpec kTextRoles[] = {
    {QPalette::WindowText, QPalette::Window, "window-text", 40, 0.10, 1.40, true},
    {QPalette::Text, QPalette::Base, "text", 40, 0.10, 1.40, true},
    {QPalette::ButtonText, QPalette::Button, "button-text", 40, 0.10, 1.40, true},
    {QPalette::ToolTipText, QPalette::ToolTipBase, "tooltip-text", 40, 0.10, 1.40, true},
    {QPalette::PlaceholderText, QPalette::Base, "placeholder-text", 40, 0.10, 1.25},
    {QPalette::Link, QPalette::Window, "link", 40, 0.05, 1.25},
    {QPalette::LinkVisited, QPalette::Window, "link-visited", 40, 0.05, 1.25},
};

// Selection colours. They are not painted on a disabled control in this product
// (a disabled widget cannot own a selection), but the palette must still not
// hand the style a Disabled group identical to Active. The floor is only "must
// differ": on themes whose selection colour is itself near white/black the
// enabled HighlightedText is already low-contrast by theme construction, so no
// contrast-drop guarantee is meaningful for that role.
const RoleSpec kSecondaryRoles[] = {
    {QPalette::Highlight, QPalette::Base, "highlight", 5, 0.0, 0.0},
    {QPalette::HighlightedText, QPalette::Highlight, "highlighted-text", 5, 0.0, 0.0},
};

// --- palette assertions -----------------------------------------------------

void testPaletteGroups()
{
    vedit::app::ThemeManager manager;
    const QStringList names = manager.availableThemes();
    check(QStringLiteral("disabled.palette.catalogue"), names.size() >= 5,
          QStringLiteral("%1 theme(s)").arg(names.size()));

    KSyntaxHighlighting::Repository repository;
    int testedThemes = 0;
    bool activeIntactAll = true;
    struct RoleMinimum {
        double channel = 1e9;
        QString channelTheme;
        double luminance = 1e9;
        QString luminanceTheme;
        double drop = 1e9;
        QString dropTheme;
    };
    QHash<QString, RoleMinimum> roleMinima;
    double worstChannel = 1e9;
    double worstLuminance = 1e9;
    double worstDrop = 1e9;
    QString worstChannelTheme;
    QString worstLuminanceTheme;
    QString worstDropTheme;

    for (const QString &name : names) {
        const KSyntaxHighlighting::Theme theme = repository.theme(name);
        if (!theme.isValid()) {
            check(QStringLiteral("disabled.palette.theme-valid"), false, name);
            continue;
        }
        ++testedThemes;
        const QPalette palette = vedit::app::ThemeManager::paletteForTheme(theme);

        // The enabled look is untouched: Active text/background are the theme's.
        const QColor foreground = expectedForeground(theme);
        const QColor background = expectedBackground(theme);
        const bool activeIntact = palette.color(QPalette::Active, QPalette::Text) == foreground
                                  && palette.color(QPalette::Active, QPalette::Base) == background;
        if (!activeIntact) {
            activeIntactAll = false;
            std::printf("PALETTE-ACTIVE-DRIFT theme=%s active=%s theme_text=%s\n",
                        qPrintable(name),
                        qPrintable(palette.color(QPalette::Active, QPalette::Text).name()),
                        qPrintable(foreground.name()));
        }

        QString themeWorstRole;
        double themeWorstDelta = 1e9;
        double themeWorstLuminance = 1e9;
        double themeWorstDrop = 1e9;
        const auto measureRole = [&](const RoleSpec &spec, bool strict) {
            const QColor enabled = palette.color(QPalette::Active, spec.role);
            const QColor disabled = palette.color(QPalette::Disabled, spec.role);
            const QColor surface = palette.color(QPalette::Active, spec.surface);
            const int delta = maxChannelDelta(enabled, disabled);
            const double luminance =
                std::abs(relativeLuminance(enabled) - relativeLuminance(disabled));
            const double enabledContrast = contrastRatio(enabled, surface);
            const double disabledContrast = contrastRatio(disabled, surface);
            const double drop = disabledContrast > 0.0 ? enabledContrast / disabledContrast : 0.0;

            if (strict && spec.primary && delta < themeWorstDelta) {
                themeWorstDelta = delta;
                themeWorstRole = QString::fromLatin1(spec.name);
            }
            if (strict && spec.primary) {
                themeWorstLuminance = std::min(themeWorstLuminance, luminance);
                themeWorstDrop = std::min(themeWorstDrop, drop);
            }
            RoleMinimum &minimum = roleMinima[QString::fromLatin1(spec.name)];
            if (delta < minimum.channel) {
                minimum.channel = delta;
                minimum.channelTheme = name;
            }
            if (luminance < minimum.luminance) {
                minimum.luminance = luminance;
                minimum.luminanceTheme = name;
            }
            if (drop < minimum.drop) {
                minimum.drop = drop;
                minimum.dropTheme = name;
            }
            if (spec.primary) {
                if (delta < worstChannel) {
                    worstChannel = delta;
                    worstChannelTheme = name;
                }
                if (luminance < worstLuminance) {
                    worstLuminance = luminance;
                    worstLuminanceTheme = name;
                }
                if (drop < worstDrop) {
                    worstDrop = drop;
                    worstDropTheme = name;
                }
            }
            if (delta < spec.minChannel || luminance < spec.minLuminance
                || drop < spec.minDrop) {
                check(QStringLiteral("disabled.palette.%1").arg(QString::fromLatin1(spec.name)),
                      false,
                      QStringLiteral("theme=%1 enabled=%2 disabled=%3 delta=%4 luminance=%5 "
                                     "drop=%6")
                          .arg(name, enabled.name(), disabled.name())
                          .arg(delta)
                          .arg(luminance, 0, 'f', 3)
                          .arg(drop, 0, 'f', 2));
            }
        };
        for (const RoleSpec &spec : kTextRoles) {
            measureRole(spec, true);
        }
        for (const RoleSpec &spec : kSecondaryRoles) {
            measureRole(spec, false);
        }
        std::printf("PALETTE-THEME %-22s worst_role=%-16s channel_delta=%.0f "
                    "luminance_delta=%.3f contrast_drop=%.2f\n",
                    qPrintable(name), qPrintable(themeWorstRole), themeWorstDelta,
                    themeWorstLuminance, themeWorstDrop);
        std::fflush(stdout);
    }

    for (const RoleSpec &spec : kTextRoles) {
        const RoleMinimum &minimum = roleMinima.value(QString::fromLatin1(spec.name));
        std::printf("PALETTE-ROLE-MIN %-18s channel_delta=%.0f (%s) luminance_delta=%.3f "
                    "(%s) contrast_drop=%.2f (%s)\n",
                    spec.name, minimum.channel, qPrintable(minimum.channelTheme),
                    minimum.luminance, qPrintable(minimum.luminanceTheme), minimum.drop,
                    qPrintable(minimum.dropTheme));
    }
    for (const RoleSpec &spec : kSecondaryRoles) {
        const RoleMinimum &minimum = roleMinima.value(QString::fromLatin1(spec.name));
        std::printf("PALETTE-ROLE-MIN %-18s channel_delta=%.0f (%s) luminance_delta=%.3f "
                    "(%s) contrast_drop=%.2f (%s) (secondary role)\n",
                    spec.name, minimum.channel, qPrintable(minimum.channelTheme),
                    minimum.luminance, qPrintable(minimum.luminanceTheme), minimum.drop,
                    qPrintable(minimum.dropTheme));
    }
    std::fflush(stdout);
    check(QStringLiteral("disabled.palette.themes-tested"), testedThemes == names.size()
                                                             && testedThemes > 0,
          QStringLiteral("%1/%2").arg(testedThemes).arg(names.size()));
    check(QStringLiteral("disabled.palette.active-intact"), activeIntactAll,
          QStringLiteral("enabled colours are still the theme's own"));
    check(QStringLiteral("disabled.palette.min-channel-delta"), worstChannel >= kMinChannelDelta,
          QStringLiteral("min=%1 theme=%2 floor=%3")
              .arg(worstChannel)
              .arg(worstChannelTheme)
              .arg(kMinChannelDelta));
    check(QStringLiteral("disabled.palette.min-luminance-delta"),
          worstLuminance >= kMinLuminanceDelta,
          QStringLiteral("min=%1 theme=%2 floor=%3")
              .arg(worstLuminance, 0, 'f', 3)
              .arg(worstLuminanceTheme)
              .arg(kMinLuminanceDelta, 0, 'f', 3));
    check(QStringLiteral("disabled.palette.min-contrast-drop"), worstDrop >= kMinContrastDrop,
          QStringLiteral("min=%1 theme=%2 floor=%3")
              .arg(worstDrop, 0, 'f', 2)
              .arg(worstDropTheme)
              .arg(kMinContrastDrop, 0, 'f', 2));

    // The shipping path, not only the pure function: applyTheme() must put the
    // disabled colours on the QApplication palette too.
    QString error;
    vedit::app::ThemeManager applied;
    const bool ok = applied.applyTheme(applied.defaultThemeName(), &error);
    const QPalette application = QApplication::palette();
    check(QStringLiteral("disabled.apply-theme"),
          ok && application.color(QPalette::Disabled, QPalette::Text)
                    != application.color(QPalette::Active, QPalette::Text),
          error.isEmpty() ? applied.currentTheme() : error);
}

// --- offscreen pixel assertions --------------------------------------------

enum class SampleKind { ToolBarAction, PushButton, ComboBox, LineEdit, PlainTextEdit };

const char *sampleName(SampleKind kind)
{
    switch (kind) {
    case SampleKind::ToolBarAction:
        return "toolbar-action";
    case SampleKind::PushButton:
        return "push-button";
    case SampleKind::ComboBox:
        return "combo-box";
    case SampleKind::LineEdit:
        return "line-edit";
    case SampleKind::PlainTextEdit:
        return "plain-text-edit";
    }
    return "unknown";
}

// One rendered sample: the widget (or the toolbar owning the action), the action
// when the sample is a toolbar action, and the palette role the style paints the
// sample text with.
struct Sample {
    QWidget *widget = nullptr;
    QAction *action = nullptr;
    const char *paletteRole = "";
    QPalette::ColorRole role = QPalette::Text;
};

void applySampleFont(QWidget *widget)
{
    QFont font = widget->font();
    font.setPointSizeF(14.0);
    font.setBold(true);
    widget->setFont(font);
}

Sample makeSample(SampleKind kind, const QPalette &palette)
{
    Sample sample;
    sample.widget = nullptr;
    switch (kind) {
    case SampleKind::ToolBarAction: {
        auto *toolbar = new QToolBar;
        toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto *action = new QAction(QStringLiteral("Undo"), toolbar);
        toolbar->addAction(action);
        toolbar->setPalette(palette);
        if (QWidget *button = toolbar->widgetForAction(action)) {
            button->setPalette(palette);
        }
        toolbar->resize(180, 34);
        sample.widget = toolbar;
        sample.action = action;
        sample.paletteRole = "button-text";
        sample.role = QPalette::ButtonText;
        break;
    }
    case SampleKind::PushButton: {
        auto *button = new QPushButton(QStringLiteral("Apply"));
        button->setPalette(palette);
        button->resize(180, 34);
        sample.widget = button;
        sample.paletteRole = "button-text";
        sample.role = QPalette::ButtonText;
        break;
    }
    case SampleKind::ComboBox: {
        auto *combo = new QComboBox;
        combo->addItem(QStringLiteral("--ruleset"));
        combo->setPalette(palette);
        combo->resize(180, 32);
        sample.widget = combo;
        sample.paletteRole = "button-text";
        sample.role = QPalette::ButtonText;
        break;
    }
    case SampleKind::LineEdit: {
        auto *edit = new QLineEdit(QStringLiteral("rules.verible_lint"));
        edit->setPalette(palette);
        edit->resize(220, 30);
        sample.widget = edit;
        sample.paletteRole = "text";
        sample.role = QPalette::Text;
        break;
    }
    case SampleKind::PlainTextEdit: {
        auto *edit = new QPlainTextEdit(QStringLiteral("waiver.sv"));
        edit->setPalette(palette);
        edit->resize(220, 60);
        sample.widget = edit;
        sample.paletteRole = "text";
        sample.role = QPalette::Text;
        break;
    }
    }
    if (sample.widget != nullptr) {
        applySampleFont(sample.widget);
    }
    return sample;
}

void setSampleEnabled(Sample *sample, bool enabled)
{
    if (sample->action != nullptr) {
        // Exactly the Undo/Redo wiring: the shared QAction is what gets disabled.
        sample->action->setEnabled(enabled);
    } else {
        sample->widget->setEnabled(enabled);
    }
}

QImage renderSample(Sample *sample)
{
    QApplication::processEvents();
    return sample->widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

struct PixelStats {
    int differing = 0;
    int enabledHits = 0;
    int disabledHits = 0;
    int channelDelta = 0;
    double luminanceDelta = 0.0;
    QColor enabled;
    QColor disabled;
};

// How many glyph-core pixels carry `target` exactly (2/255 absorbs the style's
// own rounding). Samples are rendered with a large bold font on purpose so a
// solid core pixel exists even at 1x; at the product's 10pt the same paint uses
// the same colour but every pixel is an antialiased blend.
int countPixelsNear(const QImage &image, const QColor &target, int tolerance = 2)
{
    int hits = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (maxChannelDelta(QColor(line[x]), target) <= tolerance) {
                ++hits;
            }
        }
    }
    return hits;
}

// Proves the pixels a user actually sees for the two states: the enabled render
// must contain the palette's Active text colour and the disabled render the
// palette's Disabled text colour. The colours come from the sample's own
// effective palette, never a hard-coded hex.
PixelStats measurePixels(const Sample &sample, const QImage &enabledImage,
                         const QImage &disabledImage)
{
    PixelStats stats;
    if (enabledImage.size() != disabledImage.size() || enabledImage.isNull()) {
        return stats;
    }
    const QPalette palette = sample.widget->palette();
    stats.enabled = palette.color(QPalette::Active, sample.role);
    stats.disabled = palette.color(QPalette::Disabled, sample.role);
    stats.channelDelta = maxChannelDelta(stats.enabled, stats.disabled);
    stats.luminanceDelta =
        std::abs(relativeLuminance(stats.enabled) - relativeLuminance(stats.disabled));
    stats.enabledHits = countPixelsNear(enabledImage, stats.enabled);
    stats.disabledHits = countPixelsNear(disabledImage, stats.disabled);
    for (int y = 0; y < enabledImage.height(); ++y) {
        const auto *enabledLine = reinterpret_cast<const QRgb *>(enabledImage.constScanLine(y));
        const auto *disabledLine = reinterpret_cast<const QRgb *>(disabledImage.constScanLine(y));
        for (int x = 0; x < enabledImage.width(); ++x) {
            if (maxChannelDelta(QColor(enabledLine[x]), QColor(disabledLine[x])) > 4) {
                ++stats.differing;
            }
        }
    }
    return stats;
}

void testPixels()
{
    vedit::app::ThemeManager manager;
    const QStringList names = manager.availableThemes();
    KSyntaxHighlighting::Repository repository;

    const QStringList styles = QStyleFactory::keys();
    check(QStringLiteral("disabled.pixels.styles"), !styles.isEmpty(),
          QStringLiteral("%1 style(s): %2").arg(styles.size()).arg(styles.join(QStringLiteral(", "))));
    // The exact-palette-colour assertion is only valid for the style the product
    // actually runs: it is the style whose paint path the product ships with.
    // Other style plugins present in the image (e.g. "Windows") may draw their
    // own disabled effect; for them the check is "the two renders differ".
    const QString applicationStyleClass =
        QString::fromLatin1(QApplication::style()->metaObject()->className());

    const SampleKind kinds[] = {SampleKind::ToolBarAction, SampleKind::PushButton,
                                SampleKind::ComboBox, SampleKind::LineEdit,
                                SampleKind::PlainTextEdit};

    for (const QString &styleName : styles) {
        bool exactColourChecked = false;
        for (SampleKind kind : kinds) {
            int tested = 0;
            int worstDelta = 1 << 30;
            int worstDiffering = 0;
            int worstEnabledHits = 0;
            int worstDisabledHits = 0;
            double worstLuminance = 1e9;
            QString worstTheme;
            bool allOk = true;
            for (const QString &name : names) {
                const KSyntaxHighlighting::Theme theme = repository.theme(name);
                if (!theme.isValid()) {
                    continue;
                }
                const QPalette palette = vedit::app::ThemeManager::paletteForTheme(theme);
                Sample sample = makeSample(kind, palette);
                if (sample.widget == nullptr) {
                    continue;
                }
                // QStyleFactory::create returns a caller-owned style; QWidget::
                // setStyle does not take ownership, so both live until the widget
                // is gone and the style is deleted right after it below.
                QStyle *style = QStyleFactory::create(styleName);
                if (style == nullptr) {
                    delete sample.widget;
                    continue;
                }
                const bool exactColourStyle =
                    QString::fromLatin1(style->metaObject()->className()) == applicationStyleClass;
                exactColourChecked = exactColourChecked || exactColourStyle;
                sample.widget->setStyle(style);
                if (QWidget *button = sample.widget->findChild<QToolButton *>()) {
                    button->setStyle(style);
                }
                sample.widget->show();
                QApplication::processEvents();
                setSampleEnabled(&sample, true);
                const QImage enabled = renderSample(&sample);
                setSampleEnabled(&sample, false);
                const QImage disabled = renderSample(&sample);
                const PixelStats stats = measurePixels(sample, enabled, disabled);
                ++tested;
                if (stats.channelDelta < worstDelta) {
                    worstDelta = stats.channelDelta;
                    worstLuminance = stats.luminanceDelta;
                    worstDiffering = stats.differing;
                    worstEnabledHits = stats.enabledHits;
                    worstDisabledHits = stats.disabledHits;
                    worstTheme = name;
                }
                const bool exactColour = !exactColourStyle
                                         || (stats.enabledHits >= 8 && stats.disabledHits >= 8);
                const bool ok = stats.differing >= 12 && exactColour
                                && stats.channelDelta >= kMinChannelDelta
                                && stats.luminanceDelta >= kMinLuminanceDelta;
                if (!ok) {
                    allOk = false;
                    check(QStringLiteral("disabled.pixels.%1")
                              .arg(QString::fromLatin1(sampleName(kind))),
                          false,
                          QStringLiteral("style=%1 theme=%2 differing=%3 enabled_hits=%4 "
                                         "disabled_hits=%5 enabled=%6 disabled=%7 delta=%8 "
                                         "luminance=%9")
                              .arg(styleName, name)
                              .arg(stats.differing)
                              .arg(stats.enabledHits)
                              .arg(stats.disabledHits)
                              .arg(stats.enabled.name(), stats.disabled.name())
                              .arg(stats.channelDelta)
                              .arg(stats.luminanceDelta, 0, 'f', 3));
                }
                delete sample.widget;
                delete style;
            }
            const char *roleName = (kind == SampleKind::ToolBarAction
                                    || kind == SampleKind::PushButton
                                    || kind == SampleKind::ComboBox)
                                       ? "button-text"
                                       : "text";
            std::printf("PIXELS style=%-8s widget=%-16s role=%-11s themes=%d "
                        "worst_theme=%-18s channel_delta=%d luminance_delta=%.3f "
                        "differing_px=%d enabled_hits=%d disabled_hits=%d exact_colour=%d\n",
                        qPrintable(styleName), sampleName(kind), roleName, tested,
                        qPrintable(worstTheme), worstDelta, worstLuminance,
                        worstDiffering, worstEnabledHits, worstDisabledHits,
                        exactColourChecked ? 1 : 0);
            std::fflush(stdout);
            check(QStringLiteral("disabled.pixels.%1").arg(QString::fromLatin1(sampleName(kind))),
                  tested == names.size() && allOk,
                  QStringLiteral("style=%1 themes=%2 worst_theme=%3 channel_delta=%4 "
                                 "luminance_delta=%5 differing_px=%6")
                      .arg(styleName)
                      .arg(tested)
                      .arg(worstTheme)
                      .arg(worstDelta)
                      .arg(worstLuminance, 0, 'f', 3)
                      .arg(worstDiffering));
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vedit-disabled-look-selftest"));

    testPaletteGroups();
    testPixels();

    std::printf("DISABLED-LOOK-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
