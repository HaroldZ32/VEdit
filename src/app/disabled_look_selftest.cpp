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
//     must not dim an enabled control).
//   * pixels: every theme is rendered enabled and disabled for real widget
//     samples, for every style QStyleFactory offers. The assertions are:
//     the two renders must DIFFER, the enabled render must contain the palette's
//     Active text colour and the disabled render the palette's Disabled text
//     colour (exact-colour hits on glyph-core pixels; samples use a large bold
//     font so a solid core pixel exists), and the palette delta floors must hold
//     for the sample. The toolbar sample drives a real QAction (Undo/Redo are
//     QToolButtons fed by shared QActions); the Lint-page samples are
//     QComboBox/QLineEdit/QPlainTextEdit, plus QPushButton as the generic
//     button. The application's own style gets the full 5-class sample, every
//     other style plugin gets a reduced 2-class sample.
//   * style isolation: each style is rendered in a CHILD process with a hard
//     time bound (kStyleTimeoutMs). A style plugin that stalls (Breeze/Oxygen
//     were measured to stall the pre-RM-7b in-process loop for minutes on a
//     host that ships them) is killed, reported as an explicit SKIP and never
//     hangs the test; a style that produces no pixmap is a SKIP too. The parent
//     exits 0 unless a real assertion failed.
//   * file-status dot (M2): a disabled row's dot is dimmed by the delegate and
//     must differ from the enabled dot by the same numeric floors.
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
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#if defined(VEDIT_DISABLED_LOOK_HAVE_FILETREE)
#include "file_status.h"
#include "file_status_delegate.h"
#include "file_tree_model.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

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

// An explicit skip is part of the contract: a style the environment cannot
// render (or renders too slowly to be usable) must be reported, not silently
// treated as a pass and not allowed to hang the gate.
void skip(const QString &name, const QString &reason)
{
    std::printf("SKIP %s %s\n", qPrintable(name), qPrintable(reason));
    std::fflush(stdout);
}

// A style plugin must never be able to hang the gate: each style is rendered in
// a child process with this hard bound. On expiry the child is killed and the
// style is an explicit SKIP; the test itself still exits 0.
constexpr int kStyleTimeoutMs = 15000;
// The file-status dot paints through QApplication::style() (a hand-painted
// widget), so it is isolated in a child process with the same kind of bound.
constexpr int kDotTimeoutMs = 10000;

// A disabled text colour must differ from the enabled one by at least this much.
// Floors are ~55% of the measured worst case across the 30 shipped themes (the
// measurement is in evidence/ux-batch-2/rm7-disabled-look/README.md), so a theme
// change has headroom while a return to Disabled == Active fails immediately.
constexpr int kMinChannelDelta = 40;      // of 255
constexpr double kMinLuminanceDelta = 0.10; // WCAG relative luminance, 0..1
constexpr double kMinContrastDrop = 1.40;   // enabled contrast / disabled contrast
// The file-status dot is a 9px filled brush / hollow ring, not text. Its
// measured worst case is the grey NotAnalyzed ring on the lightest theme
// (channel delta 34), so the floor is 30/255 with the same luminance and a
// contrast-drop floor; PIXEL-DOT prints the measured values every run.
constexpr int kMinDotChannelDelta = 30;
constexpr double kMinDotLuminanceDelta = 0.06;
constexpr double kMinDotContrastDrop = 1.10;

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

// Renders ONE style for the parent, in a child process. The parent kills this
// process when it exceeds kStyleTimeoutMs, so nothing here can hang the test.
// One style object is reused for every sample on purpose: creating a style per
// sample (the pre-RM-7b shape) is what made a heavy KDE style take minutes.
int runStyleChild(const QString &styleName)
{
    const QString applicationStyleClass =
        QString::fromLatin1(QApplication::style()->metaObject()->className());
    QStyle *created = QStyleFactory::create(styleName);
    if (created == nullptr) {
        std::printf("STYLE-UNRENDERABLE style=%s reason=create-failed\n",
                    qPrintable(styleName));
        std::fflush(stdout);
        return 3;
    }
    const QString styleClass = QString::fromLatin1(created->metaObject()->className());
    const bool exactColourStyle = styleClass == applicationStyleClass;
    // Make the style under test the CHILD's application style. Widgets created
    // below then paint with it even when the parent's own application style is
    // a stalling plugin (the synthetic worst case, and the KDE case where the
    // user's style is the application style): a child measuring Fusion must not
    // inherit Breeze/SlowStyle construction costs. QApplication takes ownership.
    QApplication::setStyle(created);
    vedit::app::ThemeManager manager;
    const QStringList names = manager.availableThemes();
    KSyntaxHighlighting::Repository repository;

    // Full sample for the application's own style (the product's paint path);
    // reduced sample for every other plugin, so the user's real style (Breeze
    // on KDE) still gets measured within the parent's hard bound.
    QList<SampleKind> kinds{SampleKind::ToolBarAction, SampleKind::PushButton,
                            SampleKind::ComboBox, SampleKind::LineEdit,
                            SampleKind::PlainTextEdit};
    if (!exactColourStyle) {
        kinds = {SampleKind::ToolBarAction, SampleKind::ComboBox};
    }
    std::printf("STYLE-SAMPLE style=%s class=%s kind=%s widgets=%d themes=%d exact_colour=%d\n",
                qPrintable(styleName),
                qPrintable(styleClass), exactColourStyle ? "full" : "reduced",
                int(kinds.size()), int(names.size()),
                exactColourStyle ? 1 : 0);
    std::fflush(stdout);

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
            // The child's application style is the style under test (set above),
            // so every widget in this process - the sample and its children -
            // paints with it without a per-widget style pointer.
            sample.widget->show();
            QApplication::processEvents();
            setSampleEnabled(&sample, true);
            const QImage enabled = renderSample(&sample);
            if (enabled.isNull() || enabled.width() == 0 || enabled.height() == 0) {
                std::printf("STYLE-UNRENDERABLE style=%s widget=%s reason=no-pixmap\n",
                            qPrintable(styleName), sampleName(kind));
                std::fflush(stdout);
                delete sample.widget;
                return 3;
            }
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
        }
        const char *roleName = (kind == SampleKind::ToolBarAction
                                || kind == SampleKind::PushButton
                                || kind == SampleKind::ComboBox)
                                   ? "button-text"
                                   : "text";
        std::printf("PIXELS style=%-10s widget=%-16s role=%-11s themes=%d "
                    "worst_theme=%-18s channel_delta=%d luminance_delta=%.3f "
                    "differing_px=%d enabled_hits=%d disabled_hits=%d exact_colour=%d\n",
                    qPrintable(styleName), sampleName(kind), roleName, tested,
                    qPrintable(worstTheme), worstDelta, worstLuminance, worstDiffering,
                    worstEnabledHits, worstDisabledHits, exactColourStyle ? 1 : 0);
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
    return failures == 0 ? 0 : 1;
}

// Parent side: every style is isolated in a child process so an unrenderable or
// stalling style plugin can only ever cost kStyleTimeoutMs, never the test. A
// skipped style is reported explicitly and is NOT a failure; the palette half
// of the test (which is style-independent) still guards the fix.
void testPixelStyles()
{
    const QStringList styles = QStyleFactory::keys();
    check(QStringLiteral("disabled.pixels.styles"), !styles.isEmpty(),
          QStringLiteral("%1 style(s): %2")
              .arg(styles.size())
              .arg(styles.join(QStringLiteral(", "))));

    QStringList measured;
    QStringList skipped;
    bool appStyleMeasured = false;
    for (const QString &styleName : styles) {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--render-style"), styleName});
        if (!child.waitForStarted(5000)) {
            skipped.append(QStringLiteral("%1:start-failed").arg(styleName));
            skip(QStringLiteral("disabled.pixels.style.%1").arg(styleName),
                 QStringLiteral("child process could not start"));
            continue;
        }
        const bool finished = child.waitForFinished(kStyleTimeoutMs);
        const QByteArray output = child.readAllStandardOutput();
        if (!output.isEmpty()) {
            std::fwrite(output.constData(), 1, std::size_t(output.size()), stdout);
            std::fflush(stdout);
        }
        if (!finished) {
            child.kill();
            child.waitForFinished(3000);
            skipped.append(QStringLiteral("%1:timeout").arg(styleName));
            skip(QStringLiteral("disabled.pixels.style.%1").arg(styleName),
                 QStringLiteral("no render within %1 ms -> style skipped, NOT failed")
                     .arg(kStyleTimeoutMs));
            continue;
        }
        const int code = child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
        if (code == 3) {
            skipped.append(QStringLiteral("%1:unrenderable").arg(styleName));
            skip(QStringLiteral("disabled.pixels.style.%1").arg(styleName),
                 QStringLiteral("style exists but produced no pixmap"));
        } else if (code == 0) {
            measured.append(styleName);
            appStyleMeasured = appStyleMeasured
                               || output.contains("exact_colour=1");
        } else {
            ++failures;
            std::printf("CHECK disabled.pixels.style.%s FAIL style=%s child_exit=%d\n",
                        qPrintable(styleName), qPrintable(styleName), code);
            std::fflush(stdout);
        }
    }
    std::printf("PIXEL-STYLES app_style_class=%s app_style_measured=%d measured=[%s] "
                "skipped=[%s]\n",
                qPrintable(QString::fromLatin1(QApplication::style()->metaObject()->className())),
                appStyleMeasured ? 1 : 0, qPrintable(measured.join(QStringLiteral(", "))),
                qPrintable(skipped.join(QStringLiteral(", "))));
    std::fflush(stdout);
    // The user's desktop styles are not guaranteed to exist in the gate image.
    // Say so explicitly: "Breeze unmeasured here" must be visible, never
    // implicit. Where Qt does expose the style (the user's KDE host), the same
    // bounded child measures it.
    const QStringList desktopStyles{QStringLiteral("Breeze"), QStringLiteral("Oxygen")};
    for (const QString &desktopStyle : desktopStyles) {
        if (!styles.contains(desktopStyle, Qt::CaseInsensitive)) {
            skip(QStringLiteral("disabled.pixels.style.%1").arg(desktopStyle),
                 QStringLiteral("not offered by this image; the bounded child measures it where "
                                "Qt exposes it (user-style unmeasured here)"));
        }
    }
    // A style the environment cannot render is an explicit SKIP, never a gate
    // failure (the palette assertions, which are style-independent, still guard
    // the fix). If NO style could be rendered at all, say that loudly too.
    if (measured.isEmpty()) {
        skip(QStringLiteral("disabled.pixels.all-styles"),
             QStringLiteral("no style could be rendered within the bound; palette assertions "
                            "still ran (environment problem, not a disabled-look regression)"));
    } else {
        check(QStringLiteral("disabled.pixels.measured-at-least-one-style"), true,
              QStringLiteral("measured=%1 skipped=%2")
                  .arg(measured.size())
                  .arg(skipped.size()));
    }
}

#if defined(VEDIT_DISABLED_LOOK_HAVE_FILETREE)
// M2: the file-status dot is a hand-painted widget, so it needs its own
// numeric proof. A disabled row must dim the dot brush/pen exactly like the
// palette's Disabled group dims text; the RM-9 NotAnalyzed ring must keep its
// hollow shape in both states.
void testStatusDotDisabled()
{
    using vedit::workspace::FileStatusDotDelegate;
    vedit::app::ThemeManager manager;
    KSyntaxHighlighting::Repository repository;
    const QStringList names = manager.availableThemes();

    struct StatusSpec {
        int status;
        const char *name;
        bool filled;
    };
    const StatusSpec statuses[] = {
        {static_cast<int>(vedit::common::FileStatus::Error), "error", true},
        {static_cast<int>(vedit::common::FileStatus::Warning), "warning", true},
        {static_cast<int>(vedit::common::FileStatus::Clean), "clean", true},
        {static_cast<int>(vedit::common::FileStatus::NotAnalyzed), "not-analyzed", false},
    };

    QStandardItemModel model;
    model.appendRow(new QStandardItem(QStringLiteral("top.sv")));
    const QModelIndex index = model.index(0, 0);
    FileStatusDotDelegate delegate;

    const auto paint = [&](int status, const QPalette &palette, bool enabled,
                           QColor *dot, QColor *centrePixel, bool *hollow) {
        model.item(0)->setData(status, vedit::workspace::FileTreeModel::FileStatusRole);
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 240, 26);
        option.palette = palette;
        option.state = QStyle::State_Active;
        if (enabled) {
            option.state |= QStyle::State_Enabled;
        }
        QImage image(option.rect.size(), QImage::Format_RGB32);
        image.fill(palette.color(QPalette::Base));
        QPainter painter(&image);
        delegate.paint(&painter, option, index);
        painter.end();
        *dot = delegate.lastDotColor();
        const QRect dotRect = delegate.lastDotRect();
        *centrePixel = QColor(image.pixel(dotRect.center()));
        *hollow = maxChannelDelta(*centrePixel, *dot) > 24;
    };

    int tested = 0;
    int worstChannel = 1 << 30;
    double worstLuminance = 1e9;
    double worstDrop = 1e9;
    QString worstTheme;
    QString worstLuminanceTheme;
    QString worstDropTheme;
    for (const QString &name : names) {
        const KSyntaxHighlighting::Theme theme = repository.theme(name);
        if (!theme.isValid()) {
            continue;
        }
        const QPalette palette = vedit::app::ThemeManager::paletteForTheme(theme);
        for (const StatusSpec &spec : statuses) {
            QColor enabledDot;
            QColor disabledDot;
            QColor enabledPixel;
            QColor disabledPixel;
            bool enabledHollow = false;
            bool disabledHollow = false;
            paint(spec.status, palette, true, &enabledDot, &enabledPixel, &enabledHollow);
            paint(spec.status, palette, false, &disabledDot, &disabledPixel, &disabledHollow);
            ++tested;

            const int delta = maxChannelDelta(enabledDot, disabledDot);
            const double luminance =
                std::abs(relativeLuminance(enabledDot) - relativeLuminance(disabledDot));
            const QColor surface = palette.color(QPalette::Base);
            const double enabledContrast = contrastRatio(enabledDot, surface);
            const double disabledContrast = contrastRatio(disabledDot, surface);
            const double drop =
                disabledContrast > 0.0 ? enabledContrast / disabledContrast : 0.0;
            if (delta < worstChannel) {
                worstChannel = delta;
                worstTheme = name;
            }
            if (luminance < worstLuminance) {
                worstLuminance = luminance;
                worstLuminanceTheme = name;
            }
            if (drop < worstDrop) {
                worstDrop = drop;
                worstDropTheme = name;
            }
            const bool filledShapeOk = !spec.filled
                                       || (!enabledHollow && !disabledHollow);
            const bool ringShapeOk = spec.filled || (enabledHollow && disabledHollow);
            // A filled dot's centre pixel must be the brush colour; a hollow
            // ring's centre is the row background by design, so it is not
            // compared to the pen colour.
            const bool centrePixelOk =
                !spec.filled
                || (maxChannelDelta(enabledPixel, enabledDot) <= 12
                    && maxChannelDelta(disabledPixel, disabledDot) <= 12);
            const bool ok = delta >= kMinDotChannelDelta
                            && luminance >= kMinDotLuminanceDelta
                            && drop >= kMinDotContrastDrop && filledShapeOk && ringShapeOk
                            && centrePixelOk;
            if (!ok) {
                check(QStringLiteral("disabled.dot.%1").arg(QString::fromLatin1(spec.name)),
                      false,
                      QStringLiteral("theme=%1 enabled=%2 disabled=%3 delta=%4 luminance=%5 "
                                     "drop=%6 enabled_hollow=%7 disabled_hollow=%8")
                          .arg(name, enabledDot.name(), disabledDot.name())
                          .arg(delta)
                          .arg(luminance, 0, 'f', 3)
                          .arg(drop, 0, 'f', 2)
                          .arg(enabledHollow ? 1 : 0)
                          .arg(disabledHollow ? 1 : 0));
            }
        }
    }
    std::printf("PIXEL-DOT themes=%d samples=%d channel_delta=%d (%s) luminance_delta=%.3f "
                "(%s) contrast_drop=%.2f (%s)\n",
                int(names.size()), tested, worstChannel, qPrintable(worstTheme),
                worstLuminance, qPrintable(worstLuminanceTheme), worstDrop,
                qPrintable(worstDropTheme));
    std::fflush(stdout);
    check(QStringLiteral("disabled.dot.disabled-differs-from-enabled"),
          tested == names.size() * 4 && worstChannel >= kMinDotChannelDelta
              && worstLuminance >= kMinDotLuminanceDelta
              && worstDrop >= kMinDotContrastDrop,
          QStringLiteral("samples=%1 channel_delta=%2 luminance_delta=%3 contrast_drop=%4")
              .arg(tested)
              .arg(worstChannel)
              .arg(worstLuminance, 0, 'f', 3)
              .arg(worstDrop, 0, 'f', 2));
    // Unknown has no dot at all: a disabled row must not grow one either.
    model.item(0)->setData(static_cast<int>(vedit::common::FileStatus::Unknown),
                           vedit::workspace::FileTreeModel::FileStatusRole);
    const int before = delegate.paintedDots();
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 240, 26);
    option.palette = QApplication::palette();
    QImage image(option.rect.size(), QImage::Format_RGB32);
    QPainter painter(&image);
    delegate.paint(&painter, option, index);
    painter.end();
    check(QStringLiteral("disabled.dot.unknown-draws-nothing"),
          delegate.paintedDots() == before, QStringLiteral("no dot for Unknown"));
}

// Parent side: the dot delegate paints through QApplication::style(), so it is
// isolated in a child process exactly like a style sample. A stalling style can
// only cost kDotTimeoutMs and is reported as an explicit SKIP.
void testStatusDot()
{
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--probe-dot")});
    if (!child.waitForStarted(5000)) {
        skip(QStringLiteral("disabled.dot"), QStringLiteral("child process could not start"));
        return;
    }
    const bool finished = child.waitForFinished(kDotTimeoutMs);
    const QByteArray output = child.readAllStandardOutput();
    if (!output.isEmpty()) {
        std::fwrite(output.constData(), 1, std::size_t(output.size()), stdout);
        std::fflush(stdout);
    }
    if (!finished) {
        child.kill();
        child.waitForFinished(3000);
        skip(QStringLiteral("disabled.dot"),
             QStringLiteral("no dot render within %1 ms -> dot check skipped (style too slow)")
                 .arg(kDotTimeoutMs));
        return;
    }
    const int code = child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
    if (code != 0) {
        ++failures;
        std::printf("CHECK disabled.dot FAIL dot-probe child_exit=%d\n", code);
        std::fflush(stdout);
    }
}
#endif

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vedit-disabled-look-selftest"));

    // Child mode: the parent renders each style in its own process, so a style
    // plugin that stalls can only be killed, never hang the test.
    const QStringList arguments = QCoreApplication::arguments();
    const int styleArgument = arguments.indexOf(QStringLiteral("--render-style"));
    if (styleArgument >= 0 && styleArgument + 1 < arguments.size()) {
        return runStyleChild(arguments.at(styleArgument + 1));
    }
    // Child mode for the dot probe: the parent bounds it like a style sample.
    if (arguments.contains(QStringLiteral("--probe-dot"))) {
#if defined(VEDIT_DISABLED_LOOK_HAVE_FILETREE)
        testStatusDotDisabled();
#endif
        return failures == 0 ? 0 : 1;
    }

    testPaletteGroups();
    testPixelStyles();
#if defined(VEDIT_DISABLED_LOOK_HAVE_FILETREE)
    testStatusDot();
#else
    skip(QStringLiteral("disabled.dot"),
         QStringLiteral("build has no filetree lane (VEDIT_DISABLED_LOOK_HAVE_FILETREE)"));
#endif

    std::printf("DISABLED-LOOK-SELFTEST failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
