#include "document_io.h"

#include "atomic_save.h"
#include "highlight_mode.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QCoreApplication>

namespace vedit::editor {

bool loadFileIntoDocument(KTextEditor::Document *document, const QString &absolutePath,
                          QString *error)
{
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "openFile: no document");
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
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot read %1: %2").arg(absolutePath, file.errorString());
        }
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.contains('\0')) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot open %1: it looks like a binary file").arg(absolutePath);
        }
        return false;
    }
    // RM-5a: load through the part's OPEN entry, never Document::setText().
    // setText() records the whole load as ONE undoable step, so repeated Ctrl+Z
    // walked the buffer back to EMPTY: the editor area went blank and the undo
    // command greyed out, while the file on disk was intact (real-machine
    // report). openUrl() is the documented "load a document" path
    // (KTextEditor's own header: "To load a document call
    // KParts::ReadOnlyPart::openUrl()") and makes the loaded state the undo
    // FLOOR, so undo stops at the text the file was opened with.
    //
    // The earlier note in document_io.h warned that the KParts path can spin
    // under headless/offscreen. Re-measured on the pinned image for a LOCAL
    // file: openUrl() returns in 4-6 ms with the text already loaded, under both
    // offscreen and xcb (evidence/ux-batch-2/rm5-undo/probe/openurl_probe.cpp),
    // so the hazard does not reproduce; the pre-checks above still refuse a
    // missing/unreadable/binary file BEFORE any open is attempted.
    //
    // KParts::ReadWritePart::openUrl() calls closeUrl() first, and closeUrl()
    // QUERIES THE USER ("save changes?") when the document is modified — a modal
    // dialog, which is a HANG in every headless caller (measured: rc=124; the
    // kernel selftest opens a file into a document setText() had just marked
    // modified). A load REPLACES the buffer, so the flag is cleared first: the
    // pane only ever loads into a fresh/unmodified tab and this seam must never
    // enter a dialog or an event loop (evidence/.../probe/openurl_modified_probe.cpp).
    document->setModified(false);
    if (!document->openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "cannot open %1: the editor could not load the file")
                         .arg(absolutePath);
        }
        return false;
    }
    // T81: selecting the syntax definition is part of opening a document.
    // KTextEditor's own auto-detection is keyed on the document URL, and VEdit
    // pins an explicit definition per extension (highlight_mode.h); the explicit
    // selection is what guarantees a coloured view.
    QString highlightDiagnostic;
    if (!applyHighlightingMode(document, info.absoluteFilePath(), &highlightDiagnostic)) {
        qWarning().noquote() << "[edit-core] syntax highlighting:" << highlightDiagnostic;
    } else if (!highlightDiagnostic.isEmpty()) {
        qInfo().noquote() << "[edit-core] syntax highlighting:" << highlightDiagnostic;
    }
    return true;
}

std::unique_ptr<EditorDocument> EditorDocument::create(QString *error)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("KTextEditor::Editor::instance() is null "
                                    "(the katepart plugin is not available)");
        }
        return nullptr;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "KTextEditor::Editor::createDocument() returned null");
        }
        return nullptr;
    }
    std::unique_ptr<EditorDocument> wrapped(new EditorDocument());
    wrapped->document_ = document;
    return wrapped;
}

EditorDocument::~EditorDocument()
{
    delete document_;
    document_ = nullptr;
}

KTextEditor::Document *EditorDocument::document() const
{
    return document_;
}

bool EditorDocument::setText(const QString &text, QString *error)
{
    if (document_ == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "setText: no document");
        }
        return false;
    }
    if (!document_->setText(text)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "KTextEditor::Document::setText() failed");
        }
        return false;
    }
    return true;
}

QString EditorDocument::text() const
{
    return document_ != nullptr ? document_->text() : QString();
}

int EditorDocument::lines() const
{
    return document_ != nullptr ? document_->lines() : 0;
}

int EditorDocument::lineLength(int line) const
{
    return document_ != nullptr ? document_->lineLength(line) : 0;
}

bool EditorDocument::openFile(const QString &absolutePath, QString *error)
{
    const QFileInfo info(absolutePath);
    if (!loadFileIntoDocument(document_, info.absoluteFilePath(), error)) {
        return false;
    }
    filePath_ = info.absoluteFilePath();
    setModified(false);
    return true;
}

QString EditorDocument::filePath() const
{
    if (!filePath_.isEmpty()) {
        return filePath_;
    }
    if (document_ == nullptr) {
        return QString();
    }
    return document_->url().toLocalFile();
}

bool EditorDocument::isModified() const
{
    return document_ != nullptr && document_->isModified();
}

void EditorDocument::setModified(bool modified)
{
    if (document_ != nullptr) {
        document_->setModified(modified);
    }
}

bool EditorDocument::saveAtomic(QString *error, const QString &pathTo)
{
    const QString target = pathTo.isEmpty() ? filePath() : pathTo;
    if (target.isEmpty()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "saveAtomic: the document has no file path");
        }
        return false;
    }
    const AtomicSaveResult saved = atomicSaveText(target, text().toUtf8());
    if (!saved.ok) {
        if (error != nullptr) {
            *error = saved.error;
        }
        return false;
    }
    setModified(false);
    return true;
}

} // namespace vedit::editor
