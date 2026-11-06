// RM-7 probe: measure the CURRENT palette's Disabled group and candidate
// disabled colours across every KSyntaxHighlighting theme the manager ships.
#include "theme_manager.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <cmath>
#include <cstdio>

using vedit::app::ThemeManager;

static double channel(double v)
{
    v /= 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double relativeLuminance(const QColor &c)
{
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

static double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

static QColor mix(const QColor &from, const QColor &to, int percent)
{
    const double t = percent / 100.0;
    return QColor(qRound(from.red() * (1.0 - t) + to.red() * t),
                  qRound(from.green() * (1.0 - t) + to.green() * t),
                  qRound(from.blue() * (1.0 - t) + to.blue() * t));
}

static int maxChannelDelta(const QColor &a, const QColor &b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("rm7-palette-probe"));
    std::printf("STYLES");
    for (const QString &key : QStyleFactory::keys()) {
        std::printf(" [%s]", qPrintable(key));
    }
    std::printf("\ndefault-style=%s\n", qPrintable(QApplication::style()->objectName()));

    ThemeManager manager;
    const QStringList names = manager.availableThemes();
    std::printf("themes=%d default=%s\n", int(names.size()),
                qPrintable(manager.defaultThemeName()));

    KSyntaxHighlighting::Repository repository;
    const int ratios[] = {40, 45, 50, 55, 60, 65};
    for (int ratio : ratios) {
        double minDelta = 1e9;
        double minLum = 1e9;
        double minRatio = 1e9;
        QString worst;
        for (const QString &name : names) {
            const KSyntaxHighlighting::Theme theme = repository.theme(name);
            if (!theme.isValid()) {
                continue;
            }
            const QPalette palette = ThemeManager::paletteForTheme(theme);
            const QColor fg = palette.color(QPalette::Active, QPalette::Text);
            const QColor bg = palette.color(QPalette::Active, QPalette::Base);
            const QColor candidate = mix(fg, bg, ratio);
            const double enabledContrast = contrastRatio(fg, bg);
            const double disabledContrast = contrastRatio(candidate, bg);
            const double lumDelta = std::abs(relativeLuminance(fg) - relativeLuminance(candidate));
            const double ratioDrop = disabledContrast > 0.0 ? enabledContrast / disabledContrast : 0.0;
            const double delta = maxChannelDelta(fg, candidate);
            if (delta < minDelta) {
                minDelta = delta;
                worst = name;
            }
            minLum = std::min(minLum, lumDelta);
            minRatio = std::min(minRatio, ratioDrop);
        }
        std::printf("RATIO %d minChannelDelta=%.0f minLuminanceDelta=%.4f minContrastRatioDrop=%.3f worst=%s\n",
                    ratio, minDelta, minLum, minRatio, qPrintable(worst));
    }

    // Current state: Disabled group vs Active group, per theme.
    int equalThemes = 0;
    for (const QString &name : names) {
        const KSyntaxHighlighting::Theme theme = repository.theme(name);
        if (!theme.isValid()) {
            continue;
        }
        const QPalette palette = ThemeManager::paletteForTheme(theme);
        const QColor active = palette.color(QPalette::Active, QPalette::Text);
        const QColor disabled = palette.color(QPalette::Disabled, QPalette::Text);
        if (active == disabled) {
            ++equalThemes;
        }
        std::printf("CURRENT %-24s active=%s disabled=%s equal=%d\n", qPrintable(name),
                    qPrintable(active.name()), qPrintable(disabled.name()),
                    active == disabled ? 1 : 0);
    }
    std::printf("CURRENT-EQUAL themes=%d/%d\n", equalThemes, int(names.size()));
    return 0;
}
