// RM-7b evidence probe: a real Qt style plugin whose painting stalls. Breeze /
// Oxygen are NOT installed in the pinned image, so this plugin reproduces the
// class of style the old selftest hung on (the reviewer reproduced the real one
// with Breeze/Oxygen on a host: 03-host-style-timeout.log). It is only used by
// the evidence script; nothing in the product or the test links it.
#include <QPainter>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleOption>
#include <QStylePlugin>
#include <QThread>
#include <QtPlugin>

namespace {

constexpr int kSleepMs = 200;

// Every primitive/control paint sleeps 200 ms: a full widget grab is several
// calls, so a per-sample style render takes ~1 s. The OLD test constructed a
// style per sample (150 per style) and could not finish; the NEW test bounds
// each style in a child process and skips it.
class SlowStyle final : public QProxyStyle {
public:
    SlowStyle()
        : QProxyStyle(QStringLiteral("Fusion"))
    {
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override
    {
        QThread::msleep(kSleepMs);
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override
    {
        QThread::msleep(kSleepMs);
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

} // namespace

class SlowStylePlugin final : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "slowstyle.json")
public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QStringLiteral("SlowStyle"), Qt::CaseInsensitive) == 0) {
            return new SlowStyle;
        }
        return nullptr;
    }
};

#include "slowstyle.moc"
