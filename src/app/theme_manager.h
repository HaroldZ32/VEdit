#pragma once

// Theme loading for the application shell.
//
// The catalogue is KSyntaxHighlighting's theme list (the product's syntax
// engine), and the shell applies the selected theme as the Qt application
// palette so the window, docks, menus and the demo editor all change at
// startup. The selected theme *name* is also what the editor lane hands to
// KTextEditor when it applies its own schema, so both layers agree.
//
// An unknown/malformed theme name is a recoverable configuration error: the
// palette is left untouched and the error is returned to the caller (the
// settings lane turns this into a diagnostic UI later).
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringList>

#include <functional>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

namespace vedit::app {

class ThemeManager {
public:
    ThemeManager();

    QStringList availableThemes() const;
    QString defaultThemeName() const;

    // Applies `name` as the application palette. Empty name => default theme.
    bool applyTheme(const QString &name, QString *error = nullptr);

    // --- UI-Mosaic F2: 语法主题 -> 编辑器 -----------------------------------
    // applyTheme() 成功后把生效的主题名交给编辑器渲染层（KTextEditor 的
    // View 主题是独立于应用调色板的一条线）。产品主题接管 chrome 之后
    // applyTheme() 不再重刷调色板，没有这个消费方，语法主题切换就没有任何
    // 视觉效果。空回调 = 没人消费（demo provider / 尚未安装编辑器）。
    void setSyntaxThemeApplier(std::function<void(const QString &)> applier);

    // --- UI-Mosaic T1: 产品（chrome）主题 ------------------------------------
    // 应用产品主题：调色板 -> 卡片样式表 -> 窗口图标，顺序是契约 §1.5 的一部分。
    // id 为空表示“还没选过”，落到 defaultProductTheme()（云雾白）；未知 id 返回
    // false 并保持当前状态不变（与 applyTheme 的失败语义一致）。
    bool applyUiTheme(const QString &id, QString *error = nullptr);
    QString currentUiTheme() const; // 应用过之前返回缺省 "yunwu"
    QStringList uiThemeIds() const; // 冻结顺序的 6 个 id

    QString currentTheme() const;
    QColor windowColor() const;
    QColor textColor() const;
    QString syntaxThemeName() const; // alias of currentTheme(), for the editor lane

    static QPalette paletteForTheme(const KSyntaxHighlighting::Theme &theme);

private:
    bool resolveTheme(const QString &name, KSyntaxHighlighting::Theme *theme,
                      QString *error) const;

    KSyntaxHighlighting::Repository repository_;
    QString current_;
    QPalette palette_;
    // UI-Mosaic F2: 语法主题的渲染消费方（编辑器 lane 接线后才有值）。
    std::function<void(const QString &)> syntaxThemeApplier_;
    // 产品主题状态：`uiTheme_` 是用户选中的 id，`uiThemeActive_` 表示调色板/QSS
    // 已由产品主题接管——语法主题的 applyTheme() 据此不再覆盖应用调色板。
    QString uiTheme_;
    bool uiThemeActive_ = false;
};

} // namespace vedit::app
