// RM-7: render one enabled and one disabled sample of each affected widget class
// offscreen, save the PNGs, and print the pixel facts for the evidence README
// (the same source runs against the fixed and the pre-fix theme_manager.cpp).
#include "theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int maxChannelDelta(const QColor &a, const QColor &b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

QColor dominant(const QImage &image)
{
    QHash<QRgb, int> histogram;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            ++histogram[line[x] & 0x00ffffffu];
        }
    }
    QRgb best = 0;
    int bestCount = -1;
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return QColor(0xff000000u | best);
}

// The pixel farthest from the background: the visible "ink" extreme. For a
// solid glyph core this is the text colour the user sees.
QColor inkExtreme(const QImage &image, const QColor &background)
{
    QColor best = background;
    int bestDistance = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel(line[x]);
            const int distance = maxChannelDelta(pixel, background);
            if (distance > bestDistance) {
                bestDistance = distance;
                best = pixel;
            }
        }
    }
    return best;
}

void writePair(const QString &directory, const QString &name, QWidget *widget,
               QAction *action)
{
    QImage enabled = widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (action != nullptr) {
        action->setEnabled(false);
    } else {
        widget->setEnabled(false);
    }
    QApplication::processEvents();
    QImage disabled = widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (action != nullptr) {
        action->setEnabled(true);
    } else {
        widget->setEnabled(true);
    }

    const QString enabledPath = directory + QLatin1Char('/') + name + QStringLiteral("-enabled.png");
    const QString disabledPath = directory + QLatin1Char('/') + name + QStringLiteral("-disabled.png");
    enabled.save(enabledPath);
    disabled.save(disabledPath);

    const QColor background = dominant(enabled);
    const QColor enabledInk = inkExtreme(enabled, background);
    const QColor disabledInk = inkExtreme(disabled, background);
    int differing = 0;
    for (int y = 0; y < enabled.height(); ++y) {
        const auto *enabledLine = reinterpret_cast<const QRgb *>(enabled.constScanLine(y));
        const auto *disabledLine = reinterpret_cast<const QRgb *>(disabled.constScanLine(y));
        for (int x = 0; x < enabled.width(); ++x) {
            if (maxChannelDelta(QColor(enabledLine[x]), QColor(disabledLine[x])) > 4) {
                ++differing;
            }
        }
    }
    std::printf("PAIR %-28s size=%dx%d differing_px=%d bg=%s enabled_ink=%s disabled_ink=%s "
                "ink_channel_delta=%d\n",
                qPrintable(name), enabled.width(), enabled.height(), differing,
                qPrintable(background.name()), qPrintable(enabledInk.name()),
                qPrintable(disabledInk.name()), maxChannelDelta(enabledInk, disabledInk));
    std::fflush(stdout);
}

QFont sampleFont()
{
    QFont font;
    font.setPointSizeF(14.0);
    font.setBold(true);
    return font;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("rm7-render-pixels"));
    if (argc < 2) {
        std::printf("usage: render-pixels <output-dir>\n");
        return 2;
    }
    const QString directory = QString::fromLocal8Bit(argv[1]);
    if (!QDir().mkpath(directory)) {
        std::printf("cannot create %s\n", qPrintable(directory));
        return 2;
    }

    vedit::app::ThemeManager manager;
    KSyntaxHighlighting::Repository repository;
    const QStringList themes{QStringLiteral("Breeze Light"), QStringLiteral("Breeze Dark"),
                             QStringLiteral("Nord")};
    for (const QString &themeName : themes) {
        const KSyntaxHighlighting::Theme theme = repository.theme(themeName);
        if (!theme.isValid()) {
            std::printf("SKIP invalid theme %s\n", qPrintable(themeName));
            continue;
        }
        const QPalette palette = vedit::app::ThemeManager::paletteForTheme(theme);
        const QString prefix = themeName.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
        std::printf("THEME %s active_text=%s disabled_text=%s\n", qPrintable(themeName),
                    qPrintable(palette.color(QPalette::Active, QPalette::Text).name()),
                    qPrintable(palette.color(QPalette::Disabled, QPalette::Text).name()));

        {
            auto *toolbar = new QToolBar;
            toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
            toolbar->setFont(sampleFont());
            auto *action = new QAction(QStringLiteral("Undo"), toolbar);
            toolbar->addAction(action);
            toolbar->setPalette(palette);
            if (QWidget *button = toolbar->widgetForAction(action)) {
                button->setPalette(palette);
                button->setFont(sampleFont());
            }
            toolbar->resize(180, 40);
            toolbar->show();
            QApplication::processEvents();
            writePair(directory, prefix + QStringLiteral("-toolbar-undo"), toolbar, action);
            delete toolbar;
        }
        {
            auto *combo = new QComboBox;
            combo->addItem(QStringLiteral("--ruleset"));
            combo->setFont(sampleFont());
            combo->setPalette(palette);
            combo->resize(220, 40);
            combo->show();
            QApplication::processEvents();
            writePair(directory, prefix + QStringLiteral("-combo-ruleset"), combo, nullptr);
            delete combo;
        }
        {
            auto *edit = new QLineEdit(QStringLiteral("rules.verible_lint"));
            edit->setFont(sampleFont());
            edit->setPalette(palette);
            edit->resize(260, 40);
            edit->show();
            QApplication::processEvents();
            writePair(directory, prefix + QStringLiteral("-lineedit-rules"), edit, nullptr);
            delete edit;
        }
        {
            auto *button = new QPushButton(QStringLiteral("Apply"));
            button->setFont(sampleFont());
            button->setPalette(palette);
            button->resize(180, 40);
            button->show();
            QApplication::processEvents();
            writePair(directory, prefix + QStringLiteral("-button-apply"), button, nullptr);
            delete button;
        }
    }
    return 0;
}
