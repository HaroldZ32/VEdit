#pragma once

// 主题图标（UI-Mosaic T1）。
//
// 为什么需要这一层：图标资源是死的 PNG/SVG，而产品有六套主题（两套深色）。
// 如果把图标颜色写死在 SVG 里，深色主题下工具栏会出现一组“黑笔画在黑背景上”
// 的图标。所以 SVG 统一用 `currentColor` 画，调用方把当前主题的强调色/正文色
// 传进来，这里在运行时重着色并缓存；窗口图标则按主题的深/浅选 light/dark 变体。
//
// 资源缺失时必须优雅降级：返回空 QIcon（Qt 会渲染成无图标），绝不崩溃、绝不
// 在运行时生成占位图。资源清单见 resources/app_icon.qrc。
#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>

namespace vedit::app::themed_icons {

QIcon appIcon(bool dark);                       // :/icons/app/vedit-dark.png 或 vedit-light.png
QIcon appIconMono();                            // :/icons/app/vedit-mono.png
QIcon actionIcon(const QString &name, const QColor &color); // :/icons/actions/<name>.svg，按 color 重新着色
QStringList actionNames();                      // 可用的 action 图标名

} // namespace vedit::app::themed_icons
