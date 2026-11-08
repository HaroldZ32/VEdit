// UI-Mosaic T1 自测：产品主题表、卡片样式表、调色板对比度与内嵌图标。
//
// 为什么是手写 main() + 失败计数器：仓库既有的 app selftest（i18n / crash /
// disabled-look）都是这个形态，ctest 直接跑二进制；引入 QTest 会多一层依赖和
// 一套与仓库不一致的断言输出。断言全部来自冻结契约 §1.7，另加需求 A3 的
// 对比度判据（正文 ≥ 4.5:1、禁用态 ≥ 3:1）和 §1.5 的 ThemeManager 行为。
//
// 运行方式（ctest 已配置）：QT_QPA_PLATFORM=offscreen vedit-app-ui-theme-selftest
#include "product_theme.h"
#include "theme_manager.h"
#include "themed_icons.h"

#include <QApplication>
#include <QColor>
#include <QDockWidget>
#include <QFrame>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPalette>
#include <QPixmap>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    if (ok) {
        std::printf("ok   %s\n", name);
        return;
    }
    std::printf("FAIL %s%s%s\n", name, detail.isEmpty() ? "" : " — ",
                detail.toLocal8Bit().constData());
    ++failures;
}

// WCAG 相对亮度与对比度——A3 的“正文 ≥ 4.5:1 / 禁用态 ≥ 3:1”在这里变成可执行
// 判据。用相对亮度而不是肉眼判断，主题微调时回归会立刻失败。
double relativeLuminance(const QColor &color)
{
    const auto channel = [](int value) {
        const double c = value / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green())
           + 0.0722 * channel(color.blue());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// 不透明像素数：图标“真的画出了东西”的判据。`!pixmap.isNull()` 抓不到全透明
// pixmap——T6 的运行时取证里 11 个 action 全部 iconNull=0 但 opaquePixels=0
// （SVG 着色用了 #AARRGGBB，QSvgRenderer 渲染出空图），所以契约 §1.6 要求
// 自测断言“至少一个不透明像素”，而不是只断言非 null。
int opaquePixelCount(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() != 0) {
                ++count;
            }
        }
    }
    return count;
}

// 两张同尺寸图的差异像素数；尺寸不同返回 -1（调用方按失败处理）。
int differingPixelCount(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) {
        return -1;
    }
    int count = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

const QStringList &expectedIds()
{
    static const QStringList ids = {
        QStringLiteral("yunwu"),  QStringLiteral("xinghuang"), QStringLiteral("bohe"),
        QStringLiteral("qingkong"), QStringLiteral("yaoshi"), QStringLiteral("shenye"),
    };
    return ids;
}

// 契约 §3.3 的动作图标清单；顺序也是 themed_icons::actionNames() 的顺序。
const QStringList &expectedActionNames()
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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // --- 1. 主题表规模与顺序 -------------------------------------------------
    const QVector<vedit::app::ProductTheme> &themes = vedit::app::productThemes();
    check("product.themes.count", themes.size() == 6,
          QStringLiteral("count=%1").arg(themes.size()));

    QStringList ids;
    for (const vedit::app::ProductTheme &theme : themes) {
        ids.append(theme.id);
    }
    check("product.themes.order", ids == expectedIds(), ids.join(QLatin1Char(',')));

    // --- 2. 每条主题都能按 id 取回，且有显示名与配对语法主题 -----------------
    bool fieldsComplete = true;
    QString missingField;
    for (const QString &id : expectedIds()) {
        const vedit::app::ProductTheme *theme = vedit::app::productThemeById(id);
        if (theme == nullptr) {
            fieldsComplete = false;
            missingField = QStringLiteral("null theme for %1").arg(id);
            continue;
        }
        if (theme->id.isEmpty() || theme->displayName.isEmpty()
            || theme->syntaxTheme.isEmpty()) {
            fieldsComplete = false;
            missingField = QStringLiteral("empty field on %1").arg(id);
        }
    }
    check("product.themes.lookup", fieldsComplete, missingField);
    check("product.themes.unknown-null",
          vedit::app::productThemeById(QStringLiteral("__no-such-theme__")) == nullptr);

    // --- 3. 深色判定 ---------------------------------------------------------
    bool darknessOk = true;
    QString darknessDetail;
    for (const QString &id : expectedIds()) {
        const bool expected = id == QStringLiteral("yaoshi") || id == QStringLiteral("shenye");
        if (vedit::app::productThemeIsDark(id) != expected) {
            darknessOk = false;
            darknessDetail = QStringLiteral("%1 expected dark=%2").arg(id).arg(expected);
        }
    }
    check("product.themes.darkness", darknessOk, darknessDetail);
    check("product.themes.darkness-unknown-false",
          !vedit::app::productThemeIsDark(QStringLiteral("__no-such-theme__")));

    // --- 4. 样式表渲染：非空、无残留令牌、含本主题强调色 ---------------------
    bool stylesheetsOk = true;
    QString stylesheetDetail;
    for (const vedit::app::ProductTheme &theme : themes) {
        const QString sheet = vedit::app::renderStyleSheet(theme);
        if (sheet.isEmpty()) {
            stylesheetsOk = false;
            stylesheetDetail = QStringLiteral("empty sheet for %1").arg(theme.id);
        } else if (sheet.contains(QLatin1Char('@'))) {
            stylesheetsOk = false;
            stylesheetDetail = QStringLiteral("unsubstituted token for %1").arg(theme.id);
        } else if (!sheet.contains(theme.accent.name())) {
            stylesheetsOk = false;
            stylesheetDetail = QStringLiteral("accent %1 missing for %2")
                                   .arg(theme.accent.name(), theme.id);
        } else if (!sheet.contains(QStringLiteral("12px"))
                   || !sheet.contains(QStringLiteral("QFrame#veditCard"))) {
            stylesheetsOk = false;
            stylesheetDetail = QStringLiteral("card rule/radius missing for %1").arg(theme.id);
        }
    }
    check("product.stylesheet.render", stylesheetsOk, stylesheetDetail);

    // --- 5. 调色板：Window 固定取 canvas，亮度差 ≥ 90，A3 对比度达标 -----------
    bool paletteOk = true;
    QString paletteDetail;
    for (const vedit::app::ProductTheme &theme : themes) {
        const QPalette palette = vedit::app::productPalette(theme);
        const QColor window = palette.color(QPalette::Window);
        const QColor windowText = palette.color(QPalette::WindowText);
        const QColor base = palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);

        // 契约允许 Window 取 surface 或 canvas，实现固定选了 canvas；两者之一
        // 都算通过，但亮度差必须 ≥ 90（浅色/深色都不允许“灰底灰字”）。
        const bool windowChoice = window == theme.canvas || window == theme.surface;
        const int lightnessDelta = qAbs(windowText.lightness() - window.lightness());
        if (!windowChoice || lightnessDelta < 90) {
            paletteOk = false;
            paletteDetail = QStringLiteral("%1 window=%2 delta=%3")
                                .arg(theme.id, window.name())
                                .arg(lightnessDelta);
            continue;
        }

        const double body = contrastRatio(text, base);
        const double disabledWindow = contrastRatio(
            palette.color(QPalette::Disabled, QPalette::WindowText), window);
        const double disabledBase = contrastRatio(
            palette.color(QPalette::Disabled, QPalette::Text), base);
        if (body < 4.5) {
            paletteOk = false;
            paletteDetail = QStringLiteral("%1 body contrast %2")
                                .arg(theme.id)
                                .arg(body, 0, 'f', 2);
        } else if (std::min(disabledWindow, disabledBase) < 3.0) {
            paletteOk = false;
            paletteDetail = QStringLiteral("%1 disabled contrast %2/%3")
                                .arg(theme.id)
                                .arg(disabledWindow, 0, 'f', 2)
                                .arg(disabledBase, 0, 'f', 2);
        }
    }
    check("product.palette.contrast", paletteOk, paletteDetail);

    // --- 6. 内嵌图标：深浅窗口图标 + 重着色动作图标 ---------------------------
    const QIcon darkIcon = vedit::app::themed_icons::appIcon(true);
    const QIcon lightIcon = vedit::app::themed_icons::appIcon(false);
    const QIcon monoIcon = vedit::app::themed_icons::appIconMono();
    check("icons.app.dark",
          !darkIcon.isNull() && opaquePixelCount(darkIcon.pixmap(64, 64).toImage()) > 0);
    check("icons.app.light",
          !lightIcon.isNull() && opaquePixelCount(lightIcon.pixmap(64, 64).toImage()) > 0);
    check("icons.app.mono",
          !monoIcon.isNull() && opaquePixelCount(monoIcon.pixmap(64, 64).toImage()) > 0);

    const QStringList actions = vedit::app::themed_icons::actionNames();
    check("icons.actions.list", actions == expectedActionNames(),
          QStringLiteral("count=%1").arg(actions.size()));
    // 契约 §1.6：15 个 action 逐个断言「24px 下至少一个不透明像素」。
    bool actionsOk = true;
    QString actionDetail;
    for (const QString &name : expectedActionNames()) {
        const QIcon icon = vedit::app::themed_icons::actionIcon(
            name, themes.first().accent);
        if (icon.isNull()) {
            actionsOk = false;
            actionDetail = QStringLiteral("null icon for %1").arg(name);
            continue;
        }
        const QImage image = icon.pixmap(24, 24).toImage();
        const int opaque = image.isNull() ? 0 : opaquePixelCount(image);
        if (opaque == 0) {
            actionsOk = false;
            actionDetail = QStringLiteral("no opaque pixel for %1").arg(name);
        }
    }
    check("icons.actions.opaque", actionsOk, actionDetail);

    // 同一个图标换两种差异明显的颜色渲染，像素必须不同——否则“颜色替换”可能
    // 静默失效（例如又退回 #AARRGGBB，两张图都是全透明，像素完全相同）。
    const QImage darkRender =
        vedit::app::themed_icons::actionIcon(QStringLiteral("theme"), QColor(0x1F, 0x23, 0x28))
            .pixmap(24, 24)
            .toImage();
    const QImage lightRender =
        vedit::app::themed_icons::actionIcon(QStringLiteral("theme"), QColor(0xF4, 0xF4, 0xF2))
            .pixmap(24, 24)
            .toImage();
    const int differing = differingPixelCount(darkRender, lightRender);
    check("icons.actions.recolour", differing > 0,
          QStringLiteral("differing=%1").arg(differing));

    // --- §1.5 ThemeManager 行为 ---------------------------------------------
    vedit::app::ThemeManager manager;
    check("manager.default-ui-theme",
          manager.currentUiTheme() == QStringLiteral("yunwu"), manager.currentUiTheme());
    check("manager.ui-theme-ids", manager.uiThemeIds() == expectedIds(),
          manager.uiThemeIds().join(QLatin1Char(',')));

    const QString sheetBeforeInvalid = app.styleSheet();
    QString invalidError;
    const bool invalidApplied =
        manager.applyUiTheme(QStringLiteral("__no-such-theme__"), &invalidError);
    check("manager.unknown-rejected", !invalidApplied && !invalidError.isEmpty());
    check("manager.unknown-keeps-state",
          app.styleSheet() == sheetBeforeInvalid
              && manager.currentUiTheme() == QStringLiteral("yunwu"));

    QString applyError;
    const bool applied = manager.applyUiTheme(QStringLiteral("shenye"), &applyError);
    const vedit::app::ProductTheme *shenye =
        vedit::app::productThemeById(QStringLiteral("shenye"));
    check("manager.apply", applied && applyError.isEmpty(), applyError);
    check("manager.current", manager.currentUiTheme() == QStringLiteral("shenye"));
    check("manager.palette-applied",
          shenye != nullptr && app.palette().color(QPalette::Window) == shenye->canvas);
    check("manager.stylesheet-applied",
          shenye != nullptr && app.styleSheet().contains(shenye->accent.name()));
    check("manager.window-icon-applied", !QApplication::windowIcon().isNull());

    // §1.5：语法主题切换不得清掉产品主题的 palette/QSS（否则换语法配色会把
    // 整个窗口 chrome 重刷成语法主题的颜色）。
    const QString productSheet = app.styleSheet();
    const QColor productWindow = app.palette().color(QPalette::Window);
    QString syntaxError;
    const bool syntaxApplied = manager.applyTheme(manager.defaultThemeName(), &syntaxError);
    check("manager.syntax-keeps-product-theme",
          syntaxApplied && app.styleSheet() == productSheet
              && app.palette().color(QPalette::Window) == productWindow,
          syntaxError);

    // 空 id = “还没选过”，落到缺省主题；不存在的目录状态不会被写坏。
    QString defaultError;
    const bool defaultApplied = manager.applyUiTheme(QString(), &defaultError);
    check("manager.empty-id-default",
          defaultApplied && manager.currentUiTheme() == QStringLiteral("yunwu"),
          defaultError);

    // A2 的可执行判据：样式表不能只是“文本拼对了”——Qt 真正解析它，并且
    // QFrame#veditCard 画出来的底色必须是该主题的 surface（chrome 车道的卡片
    // 就是这个 objectName）。这条断言同时证明整份 cards.qss 没有解析失败。
    QFrame card;
    card.setObjectName(QStringLiteral("veditCard"));
    card.resize(120, 80);
    card.ensurePolished();
    const QImage cardImage = card.grab().toImage();
    const QColor cardColour =
        cardImage.pixelColor(cardImage.width() / 2, cardImage.height() / 2);
    check("manager.stylesheet-card-renders",
          cardColour == vedit::app::defaultProductTheme().surface,
          QStringLiteral("rendered=%1 expected=%2")
              .arg(cardColour.name(QColor::HexArgb),
                   vedit::app::defaultProductTheme().surface.name()));

    // A2（dock 卡片化）：接线车道的既有测试不允许把面板内容包进 QFrame，所以
    // 「面板是圆角卡片」只能由 QSS 画在 QDockWidget 上。这里用真实的
    // QMainWindow + QDockWidget 验证四件事，全部是像素判据：
    //   1. dock 与 central 之间真的留出 @gap@ 画布间隙（separator 宽度）；
    //   2. title 的上边框是 border 色（卡片头上半圈）；
    //   3. 内容层的左边框是 border 色、中心是 surface（卡片体）；
    //   4. 左上角是 canvas —— 12px 圆角让画布从卡片缺角透出来。
    // 这套拼法是实测出来的：QDockWidget 自身的 border/border-radius 在
    // Qt 6.10.2 上不被绘制，只有 title 子控件与内容层可画（见 REPORT §3.6）。
    {
        const vedit::app::ProductTheme &theme = vedit::app::defaultProductTheme();
        QMainWindow window;
        window.resize(800, 500);
        auto *central = new QWidget;
        window.setCentralWidget(central);
        auto *dock = new QDockWidget(QStringLiteral("probe"), &window);
        dock->setMinimumWidth(240);
        auto *content = new QWidget;
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->addWidget(new QLabel(QStringLiteral("probe")));
        contentLayout->addStretch(1);
        dock->setWidget(content);
        window.addDockWidget(Qt::LeftDockWidgetArea, dock);
        window.show();
        QCoreApplication::processEvents();

        const int gap = central->x() - dock->width();
        check("manager.dock-separator-gap", gap == 8, QStringLiteral("gap=%1").arg(gap));

        const QImage dockImage = dock->grab().toImage();
        const int midX = dockImage.width() / 2;
        const int midY = dockImage.height() / 2;
        check("manager.dock-card-title-border",
              dockImage.pixelColor(midX, 0) == theme.border,
              dockImage.pixelColor(midX, 0).name(QColor::HexArgb));
        check("manager.dock-card-body",
              dockImage.pixelColor(0, midY) == theme.border
                  && dockImage.pixelColor(midX, midY) == theme.surface,
              QStringLiteral("border=%1 body=%2")
                  .arg(dockImage.pixelColor(0, midY).name(QColor::HexArgb),
                       dockImage.pixelColor(midX, midY).name(QColor::HexArgb)));
        check("manager.dock-card-round-corner",
              dockImage.pixelColor(0, 0) == theme.canvas,
              dockImage.pixelColor(0, 0).name(QColor::HexArgb));
    }

    std::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
