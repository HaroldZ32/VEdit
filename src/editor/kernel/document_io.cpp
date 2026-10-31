#include "document_io.h"

#include "atomic_save.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <QFileInfo>
#include <QFile>
#include <QUrl>

namespace vedit::editor {

bool loadFileIntoDocument(KTextEditor::Document *document, const QString &absolutePath,
                          QString *error)
{
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("openFile: no document");
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
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read %1: %2").arg(absolutePath, file.errorString());
        }
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.contains('\0')) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1: it looks like a binary file").arg(absolutePath);
        }
        return false;
    }
    if (!document->setText(QString::fromUtf8(data))) {
        if (error != nullptr) {
            *error = QStringLiteral("KTextEditor::Document::setText() failed for %1")
                         .arg(absolutePath);
        }
        return false;
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
            *error = QStringLiteral("KTextEditor::Editor::createDocument() returned null");
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
            *error = QStringLiteral("setText: no document");
        }
        return false;
    }
    if (!document_->setText(text)) {
        if (error != nullptr) {
            *error = QStringLiteral("KTextEditor::Document::setText() failed");
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
            *error = QStringLiteral("saveAtomic: the document has no file path");
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
