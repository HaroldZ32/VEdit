#include "ui_scale.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFont>
#include <QStringList>
#include <QWidget>

#include <algorithm>

namespace vedit::workspace::settings::ui_scale {

namespace {

QFont &startupFont()
{
    static QFont font;
    return font;
}

bool &haveStartupFont()
{
    static bool captured = false;
    return captured;
}

// The one flat-store value reader this module owns: `key = "value"` (the Rust
// store writes dotted keys QUOTED, like `"ui.scale_percent" = "150"`, but a
// hand-edited bare key is accepted too). Only scalars are read; escaping is not
// interpreted, which is why this reader is restricted to the two numeric keys.
QString storedValue(const QString &text, const char *key)
{
    const QString wanted = QString::fromLatin1(key);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        QString name = line.left(equals).trimmed();
        if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) {
            name = name.mid(1, name.size() - 2);
        }
        if (name != wanted) {
            continue;
        }
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }
    return QString();
}

} // namespace

bool validScalePercent(int percent)
{
    return std::find(std::begin(kScalePercents), std::end(kScalePercents), percent)
        != std::end(kScalePercents);
}

bool validFontPointSize(int pointSize)
{
    return pointSize == 0 || (pointSize >= kMinFontPointSize && pointSize <= kMaxFontPointSize);
}

QString scaleFactor(int percent)
{
    switch (percent) {
    case 125:
        return QStringLiteral("1.25");
    case 150:
        return QStringLiteral("1.5");
    case 200:
        return QStringLiteral("2");
    default:
        return QString();
    }
}

UiValues readValues(const QString &settingsPath)
{
    UiValues values;
    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return values;
    }
    const QString text = QString::fromUtf8(file.readAll());
    bool ok = false;
    const int font = storedValue(text, kFontPointSizeKey).toInt(&ok);
    if (ok && validFontPointSize(font)) {
        values.fontPointSize = font;
    }
    const int scale = storedValue(text, kScalePercentKey).toInt(&ok);
    if (ok && validScalePercent(scale)) {
        values.scalePercent = scale;
    }
    return values;
}

namespace {
QApplication *guiApplication()
{
    return qobject_cast<QApplication *>(QCoreApplication::instance());
}
} // namespace

QString applyScaleEnvironment(int percent)
{
    if (!validScalePercent(percent) || percent == 100) {
        // No override: leave an ambient HiDPI value alone rather than forcing 1.
        return QString::fromLatin1(qgetenv("QT_SCALE_FACTOR"));
    }
    const QByteArray factor = scaleFactor(percent).toLatin1();
    qputenv("QT_SCALE_FACTOR", factor);
    return QString::fromLatin1(factor);
}

void rememberStartupFont()
{
    if (haveStartupFont() || guiApplication() == nullptr) {
        return;
    }
    startupFont() = QApplication::font();
    haveStartupFont() = true;
}

int applyFontPointSize(int pointSize)
{
    if (!validFontPointSize(pointSize) || guiApplication() == nullptr) {
        return -1;
    }
    QFont font;
    if (pointSize == 0) {
        if (!haveStartupFont()) {
            return QApplication::font().pointSize();
        }
        font = startupFont();
    } else {
        font = QApplication::font();
        font.setPointSize(pointSize);
    }
    // setFont() sends ApplicationFontChange to every existing widget, which is
    // the "necessary style refresh" for widgets that never set a font of their
    // own; top-level windows also need their geometry recomputed.
    QApplication::setFont(font);
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (window != nullptr) {
            window->updateGeometry();
            window->update();
        }
    }
    return QApplication::font().pointSize();
}

} // namespace vedit::workspace::settings::ui_scale
