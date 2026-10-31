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
#include <QSaveFile>
#include <QString>
#include <QStringLiteral>
#include <QWidget>

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
            *error = QStringLiteral("cannot open %1: not a regular file").arg(absolutePath);
        }
        return false;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read %1: %2").arg(absolutePath, file.errorString());
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
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
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
    return true;
}

} // namespace vedit::app
