#pragma once

// UX batch 2 (item 11): the built-in User Manual reader behind
// Help -> User Manual (F1).
//
// The manual is the product's own docs/USER_GUIDE.md, embedded into the binary
// as a Qt resource at build time (resources/help/user_manual.qrc -> :/help/
// USER_GUIDE.md). Using a resource means the manual is available in every
// packaging form without a second install rule: the AppDir/AppImage and the
// tarball carry it inside the executable. The renderer is QTextEdit::setMarkdown
// (Qt 6), so no external Markdown library and no network is involved.
#include <QString>
#include <QWidget>

class QTextEdit;

namespace vedit::app {

class HelpViewer final : public QWidget {
    Q_OBJECT

public:
    explicit HelpViewer(QWidget *parent = nullptr);

    // The rendered viewer's text widget (the assertion surface of the ctest).
    QTextEdit *textView() const;

    // The Markdown source shipped in the binary (empty when the resource is
    // missing, e.g. a build tree without the .qrc); the manual's first H1
    // heading ("VEdit 用户指南"); and the window title.
    static QString manualMarkdown();
    static QString manualTitle();

    // Opens one viewer window (non-modal, deleted when closed) and returns it.
    static HelpViewer *open(QWidget *parent = nullptr);

private:
    QTextEdit *text_ = nullptr;
};

} // namespace vedit::app
