// 渲染证据探针（不属于产品代码）。
//
// 目的：证明 cards.qss 不只是“文本拼对了”，而是真的被 Qt 的 QStyleSheetStyle
// 解析并画到 chrome 车道使用的 QFrame#veditCard 上。做法是在 offscreen 下给
// 每种产品主题贴一次样式表，构造一个 objectName=veditCard 的 QFrame，grab() 后
// 取中心像素，必须等于该主题的 surface。
//
// 编译（pinned 镜像内，见 evidence/ui-mosaic/theme-kernel/REPORT.md 的命令）：
//   rcc resources/themes/theme.qss.qrc -o /tmp/qrc_theme.cpp
//   g++ -std=c++17 ... card_probe.cpp src/app/product_theme.cpp /tmp/qrc_theme.cpp ...
#include "product_theme.h"

#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QImage>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failures = 0;
    for (const char *id : {"yunwu", "xinghuang", "bohe", "qingkong", "yaoshi", "shenye"}) {
        const vedit::app::ProductTheme *theme =
            vedit::app::productThemeById(QString::fromLatin1(id));
        app.setStyleSheet(vedit::app::renderStyleSheet(*theme));

        QFrame card;
        card.setObjectName(QStringLiteral("veditCard"));
        card.resize(120, 80);
        card.ensurePolished();
        const QImage image = card.grab().toImage();
        const QColor centre = image.pixelColor(image.width() / 2, image.height() / 2);
        const bool ok = centre == theme->surface;
        std::printf("%-10s surface=%s rendered=%s %s\n", id,
                    theme->surface.name().toUtf8().constData(),
                    centre.name(QColor::HexArgb).toUtf8().constData(),
                    ok ? "OK" : "MISMATCH");
        if (!ok) {
            ++failures;
        }
    }
    std::printf("%s\n", failures == 0 ? "CARD-PROBE PASS" : "CARD-PROBE FAIL");
    return failures == 0 ? 0 : 1;
}
