#include "product_theme.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStringLiteral>

namespace vedit::app {

namespace {

// 派生规则（契约 §1.3 交给实现决定的三处，其余取值逐字照抄采样表）：
//
// * `text` 是唯一不参与混色的取值：深色主题 #E6E8EC、浅色主题 #1F2328。
//   两档中性色保证正文在六套主题上都有稳定对比度，也避免把强调色带进正文。
// * `textMuted` = text 向 surface 混合 55%：越靠近卡片底色越“退后”，所以它在
//   六套主题上都稳定地比正文淡、又不会淡到读不出来（占位符/状态文本用它）。
// * `surfaceAlt` = surface 向 canvas 混合 60%，`border` = surface 向 canvas 混合
//   88%。六套主题里 canvas 都比 surface 暗，因此两个派生色都比卡片底色更深：
//   hover/chip 看得见，1px 描边能在卡片边缘收出一圈细线，同时保持参考图那种
//   克制的层次感（不用纯黑/纯灰的硬边）。
constexpr int kTextMutedBlendPercent = 55;
constexpr int kSurfaceAltBlendPercent = 60;
constexpr int kBorderBlendPercent = 88;

// Disabled 文字向它所在的底色混合 40%。40% 是算出来的而不是猜的：语法主题
// 调色板用的 55% 在云雾白上只剩约 2.7:1，低于需求 A3 的 3:1 下限；40% 在六套
// 主题上实测均 ≥ 4:1。`vedit-app-ui-theme-selftest` 把它变成断言，而不是让这段
// 注释自我声明。
constexpr int kDisabledTextBlendPercent = 40;

// percent = `color` 向 `target` 移动的距离（0 保持原色，100 等于 target）。
// 逐通道四舍五入，保证同一主题每次生成相同的十六进制值。
QColor blendToward(const QColor &color, const QColor &target, int percent)
{
    const qreal weight = qBound(0, percent, 100) / 100.0;
    return QColor(qRound(color.red() * (1.0 - weight) + target.red() * weight),
                  qRound(color.green() * (1.0 - weight) + target.green() * weight),
                  qRound(color.blue() * (1.0 - weight) + target.blue() * weight));
}

ProductTheme makeTheme(const char *id, const char *displayName, bool dark, QRgb canvas,
                       QRgb surface, QRgb accent, QRgb accentText, const char *syntaxTheme)
{
    ProductTheme theme;
    theme.id = QString::fromLatin1(id);
    // M1：显示名是 UI 文本而不是数据。这里存的是英文源文本，按固定 context
    // "product_theme" 查已安装的 .qm（产品在构造 shell 之前就装好了翻译器），
    // 所以英文界面显示 "Cloud White"、中文界面显示 "云雾白"，而 `id` 仍是唯一
    // 持久化的值（翻译显示名绝不改变存档/设置里的 id）。
    theme.displayName = QCoreApplication::translate("product_theme", displayName);
    theme.dark = dark;
    theme.canvas = QColor::fromRgb(canvas);
    theme.surface = QColor::fromRgb(surface);
    theme.accent = QColor::fromRgb(accent);
    theme.accentText = QColor::fromRgb(accentText);
    theme.syntaxTheme = QString::fromLatin1(syntaxTheme);

    theme.surfaceAlt = blendToward(theme.surface, theme.canvas, kSurfaceAltBlendPercent);
    theme.border = blendToward(theme.surface, theme.canvas, kBorderBlendPercent);
    theme.text = dark ? QColor(0xE6, 0xE8, 0xEC) : QColor(0x1F, 0x23, 0x28);
    theme.textMuted = blendToward(theme.text, theme.surface, kTextMutedBlendPercent);
    return theme;
}

} // namespace

const QVector<ProductTheme> &productThemes()
{
    // 顺序即契约顺序：视图菜单、工具栏菜单和首选项下拉都按这个顺序渲染，
    // 持久化的只是 id，所以顺序变化不会破坏用户存档。
    static const QVector<ProductTheme> themes = [] {
        QVector<ProductTheme> list;
        list.reserve(6);
        list.append(makeTheme("yunwu", "Cloud White", false, 0xF4F4F2, 0xFFFFFF, 0x2A2A28,
                              0xFFFFFF, "Breeze Light"));
        list.append(makeTheme("xinghuang", "Warm Apricot", false, 0xF9F2E1, 0xFFFDF6, 0xC87A2E,
                              0xFFFFFF, "Solarized Light"));
        list.append(makeTheme("bohe", "Mint Green", false, 0xEDF4EE, 0xFFFFFF, 0x3E9A63,
                              0xFFFFFF, "Breeze Light"));
        list.append(makeTheme("qingkong", "Sky Blue", false, 0xEBF0F7, 0xFFFFFF, 0x4A7EF0,
                              0xFFFFFF, "Breeze Light"));
        list.append(makeTheme("yaoshi", "Obsidian", true, 0x131315, 0x1D1D20, 0xD5A027,
                              0x1D1D20, "Breeze Dark"));
        list.append(makeTheme("shenye", "Deep Navy", true, 0x0F1622, 0x161F2E, 0x518BE3,
                              0x0A1420, "Breeze Dark"));
        return list;
    }();
    return themes;
}

const ProductTheme *productThemeById(const QString &id)
{
    const QVector<ProductTheme> &themes = productThemes();
    for (const ProductTheme &theme : themes) {
        if (theme.id == id) {
            return &theme;
        }
    }
    return nullptr;
}

const ProductTheme &defaultProductTheme()
{
    return productThemes().first();
}

bool productThemeIsDark(const QString &id)
{
    const ProductTheme *theme = productThemeById(id);
    return theme != nullptr && theme->dark;
}

QPalette productPalette(const ProductTheme &theme)
{
    QPalette palette;
    // 窗口 chrome = 画布，卡片 = surface：与 cards.qss 的分工完全一致，否则
    // 没有 QSS 覆盖的内建控件（原生对话框、工具提示）会和卡片对不上。
    palette.setColor(QPalette::Window, theme.canvas);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.surface);
    palette.setColor(QPalette::AlternateBase, theme.surfaceAlt);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::Button, theme.surface);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.accentText);
    palette.setColor(QPalette::ToolTipBase, theme.surface);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.textMuted);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::LinkVisited, theme.accent);
    // 3D 边框色：QSS 覆盖不到的控件靠它画出与卡片一致的细边。
    palette.setColor(QPalette::Mid, theme.border);
    palette.setColor(QPalette::Midlight, theme.surfaceAlt);
    palette.setColor(QPalette::Dark, theme.border);
    palette.setColor(QPalette::Light, theme.surface);
    palette.setColor(QPalette::Shadow, theme.border);

    // setColor(role, colour) 会把 ACTIVE/INACTIVE/DISABLED 三组一起写满，所以
    // Disabled 组必须在最后逐个覆盖，否则禁用控件会画得和启用时一模一样
    // （RM-7 修过一次的缺陷，这里不能退回去）。每个文字角色向它实际被绘制在
    // 其上的底色混合，深浅两档主题共用同一条规则。
    palette.setColor(QPalette::Disabled, QPalette::WindowText,
                     blendToward(theme.text, theme.canvas, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     blendToward(theme.text, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     blendToward(theme.text, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::ToolTipText,
                     blendToward(theme.text, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText,
                     blendToward(theme.textMuted, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Highlight,
                     blendToward(theme.accent, theme.surface, 55));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     blendToward(theme.text, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::Link,
                     blendToward(theme.accent, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::LinkVisited,
                     blendToward(theme.accent, theme.surface, kDisabledTextBlendPercent));
    palette.setColor(QPalette::Disabled, QPalette::BrightText,
                     blendToward(QColor(Qt::white), theme.surface, kDisabledTextBlendPercent));
    return palette;
}

QString renderStyleSheet(const ProductTheme &theme)
{
    QFile templateFile(QStringLiteral(":/themes/cards.qss"));
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 资源没编进来（例如某个只链接静态库、没带 qrc 的测试二进制）：
        // 返回空串让调用方只应用调色板，绝不把字面量令牌喂给 Qt。
        return QString();
    }
    QString sheet = QString::fromUtf8(templateFile.readAll());

    struct TokenValue {
        const char *token;
        QString value;
    };
    const TokenValue tokens[] = {
        {"@canvas@", theme.canvas.name()},       {"@surface@", theme.surface.name()},
        {"@surfaceAlt@", theme.surfaceAlt.name()}, {"@border@", theme.border.name()},
        {"@text@", theme.text.name()},           {"@textMuted@", theme.textMuted.name()},
        {"@accent@", theme.accent.name()},       {"@accentText@", theme.accentText.name()},
        // 契约只要求“至少”这些令牌；@textDisabled@ 是额外的一个：QSS 里普通的
        // color 规则会连禁用态一起上色，所以每个带文字的控件都需要一个显式的
        // 禁用色，而它必须达到 A3 的 3:1 下限（textMuted 在浅色主题上约 2.9:1，
        // 不够，故单独按 Disabled 组同一条 40% 规则派生）。
        {"@textDisabled@", blendToward(theme.text, theme.surface, kDisabledTextBlendPercent).name()},
        {"@radius@", QStringLiteral("12px")},    {"@radiusSm@", QStringLiteral("8px")},
        {"@gap@", QStringLiteral("8px")},
    };
    for (const TokenValue &entry : tokens) {
        sheet.replace(QString::fromLatin1(entry.token), entry.value);
    }
    // 契约要求替换后不得残留任何 `@...@`。残留只可能是模板里出现了未登记的
    // 令牌（拼写错误或新增令牌忘了登记），此时返回空串让测试/调用方立刻发现，
    // 而不是让 Qt 静默忽略那条规则。
    if (sheet.contains(QLatin1Char('@'))) {
        qWarning("renderStyleSheet: unsubstituted token left in cards.qss");
        return QString();
    }
    return sheet;
}

} // namespace vedit::app
