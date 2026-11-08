#pragma once

// 产品界面主题（UI-Mosaic T1）。
//
// 为什么需要这个文件：在这条车道之前，产品只有一条外观轴——把选中的
// KSyntaxHighlighting 语法主题换算成 QPalette，于是窗口 chrome 只能跟着语法配色
// 走，而且产品没有任何样式表。参考图要求六套明确采样过的产品配色，再加一套统一
// 的卡片样式语言，所以把「数据」单独放在这里。
//
// 本文件的数据就是契约（`.mpd/plans/ui-mosaic/CONTRACT.md` §1.2/§1.3）：id、顺序、
// 六套采样色与派生规则都已冻结。改颜色属于改契约，不属于改实现——评审时不要
// “顺手调一下”，这张表故意保持枯燥。
//
// displayName 是**可翻译的 UI 文本**，不是数据：本结构存英文源文本
// （"Cloud White" ...），`product_theme.cpp` 构造时经
// QCoreApplication::translate("product_theme", ...) 取当前语言，译文在
// translations/vedit_zh_CN.ts 的 `product_theme` 上下文里。持久化只用 id，
// 所以翻译显示名不会改变存档/设置里的值（M1）。
#include <QColor>
#include <QPalette>
#include <QString>
#include <QVector>

namespace vedit::app {

struct ProductTheme {
    QString id;           // "yunwu" ...
    QString displayName;  // 已翻译的显示名（源文本 "Cloud White" / 译文 云雾白）
    bool    dark;
    QColor  canvas;       // 窗口画布（卡片之间的间隙）
    QColor  surface;      // 卡片底色
    QColor  surfaceAlt;   // 次级卡片 / chip / hover
    QColor  border;       // 1px 描边
    QColor  text;         // 正文
    QColor  textMuted;    // 次要文本
    QColor  accent;       // 强调色（按钮/选中/焦点）
    QColor  accentText;   // 强调色之上的文字
    QString syntaxTheme;  // 配对的 KSyntaxHighlighting 主题名
};

// The six product themes in the frozen order
// (yunwu, xinghuang, bohe, qingkong, yaoshi, shenye).
const QVector<ProductTheme> &productThemes();

// 未知 id 返回 nullptr；空 id 不在这里兜底——接受“还没选过”的调用方应走
// defaultProductTheme()。
const ProductTheme *productThemeById(const QString &id);

// 云雾白 — the contract's default.
const ProductTheme &defaultProductTheme();

// 未知 id 返回 false（绝不猜）。
bool productThemeIsDark(const QString &id);

// 某套产品主题对应的 Qt 调色板，**包含**派生的 Disabled 组，这样被禁用的控件
// 看起来就是禁用的（产品既有的视觉规则）。`QPalette::Window` 取主题的 `canvas`：
// 窗口 chrome 是卡片之间的画布，卡片本身才是 surface。
QPalette productPalette(const ProductTheme &theme);

// `resources/themes/cards.qss` 把所有 `@token@` 替换成该主题的值之后的结果。
// 模板资源缺失、或仍有令牌没被替换时返回空串——调用方可以只应用调色板
// （优雅降级），而不是把字面量 `@token@` 喂给 Qt。
QString renderStyleSheet(const ProductTheme &theme);

} // namespace vedit::app
