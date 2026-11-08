#include "themed_icons.h"

#include <QByteArray>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSvgRenderer>
#include <QStringLiteral>
#include <QtGlobal>

namespace vedit::app::themed_icons {

namespace {

// 动作图标集的固定顺序 = 契约 §3.3 的文件名清单。这里用数据表而不是目录扫描：
// 漏文件时表现为“这一个名字拿到空 QIcon”，而不是工具栏悄悄少一个按钮、并且
// 测试还能数出 15 个名字。
const QStringList &knownActionNames()
{
    static const QStringList names = {
        QStringLiteral("new"),      QStringLiteral("open"),   QStringLiteral("save"),
        QStringLiteral("save-as"),  QStringLiteral("undo"),   QStringLiteral("redo"),
        QStringLiteral("find"),     QStringLiteral("replace"), QStringLiteral("sidebar"),
        QStringLiteral("terminal"), QStringLiteral("build"),  QStringLiteral("vcs"),
        QStringLiteral("settings"), QStringLiteral("theme"),  QStringLiteral("help"),
    };
    return names;
}

// 工具串图标按 24×24 逻辑像素绘制（SVG 源尺寸也是 24×24），再乘设备像素比出图。
constexpr int kActionIconSize = 24;

// 当前进程使用的设备像素比。没有 QGuiApplication（纯 QCoreApplication 的
// 单元测试/命令行工具）时按 1.0 处理，避免访问空指针。
qreal devicePixelRatio()
{
    return qGuiApp != nullptr ? qGuiApp->devicePixelRatio() : 1.0;
}

QByteArray readResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// PNG 窗口图标。资源缺失或解码失败返回空 QIcon——调用方（ThemeManager）照常
// 完成主题切换，只是窗口没有图标，绝不因此失败。
QIcon loadPngIcon(const QString &path)
{
    if (QGuiApplication::instance() == nullptr) {
        return QIcon();
    }
    const QByteArray bytes = readResource(path);
    if (bytes.isEmpty()) {
        return QIcon();
    }
    QPixmap pixmap;
    if (!pixmap.loadFromData(bytes, "PNG")) {
        return QIcon();
    }
    return QIcon(pixmap);
}

// 把 SVG 文本里的所有 `currentColor` 换成给定颜色再渲染。
//
// 必须是全量替换：theme.svg 的圆点是 fill="currentColor"、半环是
// stroke="currentColor"，只替第一个会让圆点保持黑色。QByteArray::replace 替换
// 所有匹配，正是这里需要的语义。
//
// **必须用 HexRgb（#RRGGBB），不能用 HexArgb（#AARRGGBB）**：QSvgRenderer 不
// 接受 Qt 扩展色序 #AARRGGBB——运行时取证显示 #ff1f2328 渲染出 0 个不透明像素
// （整条 path 变全透明），而 #1f2328 渲染出 195 个。线稿图标不承载 alpha 语义，
// 所以调用方传入的 alpha 在这里被有意丢弃；契约 §1.6 已把原稿的 HexArgb 标注为
// 错误（见 .mpd/plans/ui-mosaic/CONTRACT.md）。
QIcon renderSvg(const QByteArray &svg, const QColor &color)
{
    if (QGuiApplication::instance() == nullptr) {
        return QIcon();
    }
    QByteArray recoloured = svg;
    recoloured.replace(QByteArrayLiteral("currentColor"),
                       color.name(QColor::HexRgb).toUtf8());
    QSvgRenderer renderer(recoloured);
    if (!renderer.isValid()) {
        return QIcon();
    }

    const qreal ratio = devicePixelRatio();
    const int pixels = qMax(1, qRound(kActionIconSize * ratio));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, kActionIconSize, kActionIconSize));
    painter.end();
    return QIcon(pixmap);
}

// 缓存：主题切换只改颜色，几何不变，所以按 (名字, 颜色, DPR) 缓存渲染结果。
// 缓存键包含 DPR，窗口被拖到另一块缩放比例不同的屏幕上时不会拿到旧图。
QHash<QString, QIcon> &actionCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

} // namespace

QIcon appIcon(bool dark)
{
    if (dark) {
        static const QIcon icon = loadPngIcon(QStringLiteral(":/icons/app/vedit-dark.png"));
        return icon;
    }
    static const QIcon icon = loadPngIcon(QStringLiteral(":/icons/app/vedit-light.png"));
    return icon;
}

QIcon appIconMono()
{
    static const QIcon icon = loadPngIcon(QStringLiteral(":/icons/app/vedit-mono.png"));
    return icon;
}

QIcon actionIcon(const QString &name, const QColor &color)
{
    if (name.isEmpty() || !color.isValid()) {
        return QIcon();
    }
    // 缓存键与渲染用的色值一致（HexRgb）：alpha 被丢弃，若还按 HexArgb 做键，
    // 两个只差 alpha 的调用会拿到互相矛盾的缓存条目。
    const QString key = name + QLatin1Char('\x1f') + color.name(QColor::HexRgb)
                        + QLatin1Char('\x1f') + QString::number(devicePixelRatio());
    QHash<QString, QIcon> &cache = actionCache();
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) {
        return cached.value();
    }

    const QIcon icon = renderSvg(
        readResource(QStringLiteral(":/icons/actions/%1.svg").arg(name)), color);
    // 空图标也缓存：缺失资源是打包错误，重试读盘不会变好；测试会独立断言
    // 15 个名字都有非空图标，所以这里的缓存不会掩盖问题。
    cache.insert(key, icon);
    return icon;
}

QStringList actionNames()
{
    QStringList available;
    for (const QString &name : knownActionNames()) {
        if (QFile::exists(QStringLiteral(":/icons/actions/%1.svg").arg(name))) {
            available.append(name);
        }
    }
    return available;
}

} // namespace vedit::app::themed_icons
