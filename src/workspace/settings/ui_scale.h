#pragma once

// Appearance settings (UX batch 2 / A4, user ruling "字体即时生效 + 整体缩放重启
// 生效").
//
// Two values, two DIFFERENT application times, and that difference is the whole
// reason this module exists:
//
//   * `ui.font_point_size` — applied the moment the user changes it
//     (`QApplication::setFont`), and again at startup before the shell is built;
//   * `ui.scale_percent` — a RESTART switch. Qt reads `QT_SCALE_FACTOR` once,
//     when the QApplication is constructed, so the variable must be in the
//     environment BEFORE `QApplication app(argc, argv)` (src/app/main.cpp does
//     exactly that, before anything else in `main`).
//
// Reading the two values at that moment cannot go through the Rust store's job
// layer (it is polled from a QTimer and needs a running event loop), so this
// module owns a deliberately tiny reader for the flat TOML store: `key =
// "value"` pairs, first match wins, anything unparsable is the default. It reads
// ONLY the two scalar keys below — every other setting still goes through the
// Rust store.
#include <QString>

namespace vedit::workspace::settings::ui_scale {

// Store keys (identical to `vd_settings::schema::keys`).
inline constexpr char kFontPointSizeKey[] = "ui.font_point_size";
inline constexpr char kScalePercentKey[] = "ui.scale_percent";

// The accepted scale choices, in the order the Preferences combo shows them.
inline constexpr int kScalePercents[] = {100, 125, 150, 200};
// The accepted font sizes, in points (inclusive). 0 means "follow the system".
inline constexpr int kMinFontPointSize = 6;
inline constexpr int kMaxFontPointSize = 72;

struct UiValues {
    int fontPointSize = 0;  // 0 = follow the system font
    int scalePercent = 100; // one of kScalePercents
};

bool validScalePercent(int percent);
bool validFontPointSize(int pointSize); // 0 (system) or kMin..kMax

// The value this scale needs for `QT_SCALE_FACTOR`, or an empty string for
// 100 % (the default: no override at all).
QString scaleFactor(int percent);

// Reads the two keys straight out of the flat store file. A missing file, an
// unreadable one and an unparsable value all yield the defaults — this is the
// startup path, it must never fail or warn.
UiValues readValues(const QString &settingsPath);

// Sets `QT_SCALE_FACTOR` for the NEXT start. 100 % leaves an ambient
// `QT_SCALE_FACTOR` (a HiDPI environment) alone instead of overwriting it with
// "1"; any other accepted value normalises the variable to `scaleFactor()`.
// Returns the value the variable now has (empty when it was left unset).
QString applyScaleEnvironment(int percent);

// Captures the font the process started with, so "follow the system" can put it
// back after the user tried an explicit size. Idempotent.
void rememberStartupFont();
// Applies a font size to the whole application: `QApplication::setFont` (which
// Qt propagates to existing widgets as an ApplicationFontChange) with the
// captured startup font when `pointSize` is 0. Returns the point size the
// application font now has, or -1 when there is no QApplication yet.
int applyFontPointSize(int pointSize);

} // namespace vedit::workspace::settings::ui_scale
