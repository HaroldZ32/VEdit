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
};

} // namespace vedit::app
