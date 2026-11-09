#pragma once

// The editor seam of the application shell.
//
// The shell owns window, menus, docks, splits and theme; it does NOT own an
// editor. src/editor (edit-core) implements this interface and hands the shell
// one widget per editor pane. Until that lane lands, DemoEditorProvider supplies
// a QPlainTextEdit-based placeholder so the layout is runnable and demonstrable;
// it is explicitly not the product editor.
//
// The shell never includes a KTextEditor header, so adding the real editor does
// not change this lane's build.
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QSaveFile>
#include <QString>
#include <QStringLiteral>
#include <QTextDocument>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <QCoreApplication>

namespace vedit::app {

class EditorProvider {
public:
    virtual ~EditorProvider() = default;

    // Stable id, persisted in the session layout (e.g. "demo", "ktexteditor").
    virtual QString id() const = 0;

    // Human-readable description shown in pane headers and the About dialog.
    virtual QString displayName() const = 0;

    // Creates the editor widget for one pane. Ownership passes to the caller
    // (the shell reparents it into the pane). Must return a non-null widget.
    virtual QWidget *createEditor(QWidget *parent) = 0;

    // --- syntax-theme seam (UI-Mosaic F2) -----------------------------------
    // Hands the ACTIVE syntax theme name (ThemeManager::syntaxThemeName()) to
    // the editor implementation, which renders it with its OWN theme support.
    // This is the consumer the 30 KSyntaxHighlighting themes were always meant
    // to have: once a product theme owns the chrome, applyTheme() no longer
    // repaints the application palette (contract §1.5), so without this seam a
    // syntax theme change would have no visible effect at all.
    //
    // The shell calls it on every successful ThemeManager::applyTheme(), and
    // once with the already-current name when a provider is installed (the
    // product start applies the theme, then the editor lane swaps the
    // provider). An implementation must apply it to every view it created and
    // to every view created later; a provider whose widget simply follows the
    // application palette keeps the default no-op.
    virtual void applySyntaxTheme(const QString &themeName);

    // --- document seam (edit-core, Slice 1) --------------------------------
    // Open/save the file shown in one editor widget. Callers (the shell, the
    // workspace lanes) use this instead of casting the widget themselves; the
    // default implementation drives the demo QPlainTextEdit, and the edit-core
    // provider overrides it with a KTextEditor document. Returns false with
    // *error filled when the operation fails.
    virtual bool openFile(QWidget *editor, const QString &absolutePath,
                          QString *error = nullptr);
    virtual QString currentFilePath(QWidget *editor) const;
    virtual bool saveFile(QWidget *editor, QString *error = nullptr);
    // T130: closing a pane must not silently discard an unsaved document.
    virtual bool isModified(QWidget *editor) const;

    // --- breadcrumb/status seam (T94) --------------------------------------
    // One line of positional context for a pane (the semantic breadcrumb path).
    // The pane renders it under the header and hides it while empty. A provider
    // with nothing positional keeps the defaults; the shell never sees a
    // KTextEditor header.
    virtual QString statusText(QWidget *editor) const;
    virtual void setStatusListener(QWidget *editor, std::function<void()> listener);

    // --- session-restore view state (T19) -----------------------------------
    // Where the caret and the viewport sit, so a restart puts the user back where
    // they were. `scrollTop` is in the provider's OWN units: the vertical
    // scrollbar value of a plain-text fallback, the first displayed REAL line of
    // a KTextEditor view. A negative field means "unknown" and is not applied.
    // The archive remembers the provider id that wrote the values and never
    // replays them into a different one (line numbering is provider-specific).
    struct ViewState {
        int caretLine = -1;
        int caretColumn = -1;
        int scrollTop = -1;
        bool hasCaret() const { return caretLine >= 0; }
        bool hasScroll() const { return scrollTop >= 0; }
    };
    virtual ViewState viewState(QWidget *editor) const;
    virtual void setViewState(QWidget *editor, const ViewState &state);

    // --- dirty-document seam (T12) -----------------------------------------
    // The pane's tab bar paints a per-tab dirty marker and the shell paints the
    // window title / status bar from this, so the editor must REPORT the
    // document's modified flag instead of being polled. The listener fires with
    // the new state on every change the document itself records (a keystroke,
    // undo, a programmatic setModified, and the save that clears it). Passing an
    // empty listener unsubscribes; a widget the provider no longer owns simply
    // drops the registration. The pane is the only subscriber.
    virtual void setModifiedListener(QWidget *editor, std::function<void(bool)> listener);
};

// Placeholder provider: one read/write QPlainTextEdit per pane, labelled
// "demo" everywhere so nobody mistakes it for the product editor.
class DemoEditorProvider final : public EditorProvider {
public:
    QString id() const override;
    QString displayName() const override;
    QWidget *createEditor(QWidget *parent) override;
};

// ---------------------------------------------------------------------------
// default document seam (header-only on purpose)
// ---------------------------------------------------------------------------
// The edit-core provider implements this seam against a KTextEditor document
// and calls these defaults for its fallback widget. Defining them here keeps the
// kernel lane linkable on its own (`cmake -S src/editor`), without linking the
// shell implementation it only needs the interface from.

inline bool EditorProvider::openFile(QWidget *editor, const QString &absolutePath, QString *error)
{
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    if (text == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("open: the pane holds no text editor widget");
        }
        return false;
    }
    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot open %1: not a regular file").arg(absolutePath);
        }
        return false;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot read %1: %2").arg(absolutePath, file.errorString());
        }
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.contains('\0')) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 looks binary; not opened in the text editor")
                         .arg(absolutePath);
        }
        return false;
    }
    text->setPlainText(QString::fromUtf8(data));
    // A freshly opened file is not a modification: the tab marker and the
    // window-title star must be quiet until the user actually edits.
    text->document()->setModified(false);
    editor->setProperty("vedit.openFilePath", info.absoluteFilePath());
    return true;
}

inline QString EditorProvider::currentFilePath(QWidget *editor) const
{
    if (editor == nullptr) {
        return QString();
    }
    return editor->property("vedit.openFilePath").toString();
}

inline void EditorProvider::applySyntaxTheme(const QString &themeName)
{
    Q_UNUSED(themeName);
}

inline QString EditorProvider::statusText(QWidget *editor) const
{
    Q_UNUSED(editor);
    return {};
}

inline void EditorProvider::setStatusListener(QWidget *editor, std::function<void()> listener)
{
    Q_UNUSED(editor);
    Q_UNUSED(listener);
}

inline void EditorProvider::setModifiedListener(QWidget *editor, std::function<void(bool)> listener)
{
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    if (text == nullptr) {
        return;
    }
    // The widget is the connection's context, so destroying the tab's editor
    // drops the binding with it. Re-subscribing replaces the previous listener
    // instead of stacking a second one.
    QObject::disconnect(text->document(), &QTextDocument::modificationChanged, editor, nullptr);
    if (!listener) {
        return;
    }
    QObject::connect(text->document(), &QTextDocument::modificationChanged, editor,
                     [listener](bool modified) { listener(modified); });
}

inline EditorProvider::ViewState EditorProvider::viewState(QWidget *editor) const
{
    ViewState state;
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    if (text == nullptr) {
        return state;
    }
    const QTextCursor cursor = text->textCursor();
    state.caretLine = cursor.blockNumber();
    state.caretColumn = cursor.positionInBlock();
    if (const QScrollBar *bar = text->verticalScrollBar()) {
        state.scrollTop = bar->value();
    }
    return state;
}

inline void EditorProvider::setViewState(QWidget *editor, const ViewState &state)
{
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    if (text == nullptr) {
        return;
    }
    if (state.hasCaret()) {
        const QTextBlock block = text->document()->findBlockByNumber(state.caretLine);
        if (block.isValid()) {
            const int lastColumn = std::max(0, block.length() - 1);
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                                std::min(state.caretColumn, lastColumn));
            text->setTextCursor(cursor);
        }
    }
    if (state.hasScroll()) {
        // The scrollbar range depends on the viewport height, which is only final
        // after the window is shown and laid out: apply the offset on the next
        // event-loop turn instead of clamping it against a not-yet-laid-out range
        // (which would silently restore every file at the top).
        const QPointer<QPlainTextEdit> guarded(text);
        const int value = state.scrollTop;
        QTimer::singleShot(0, text, [guarded, value]() {
            if (guarded.isNull()) {
                return;
            }
            if (QScrollBar *bar = guarded->verticalScrollBar()) {
                bar->setValue(value);
            }
        });
    }
}

inline bool EditorProvider::isModified(QWidget *editor) const
{
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    return text != nullptr && text->document()->isModified();
}

inline bool EditorProvider::saveFile(QWidget *editor, QString *error)
{
    auto *text = qobject_cast<QPlainTextEdit *>(editor);
    const QString path = currentFilePath(editor);
    if (text == nullptr || path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("save: the pane has no text editor with a file path");
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    const QByteArray data = text->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("saving %1 failed: %2").arg(path, file.errorString());
        }
        return false;
    }
    // T12: a successful save clears the dirty marker (the document's own flag is
    // what the tab, the window title and the status bar are painted from).
    text->document()->setModified(false);
    return true;
}

} // namespace vedit::app
