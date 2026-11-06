#include "document_sync.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QWidget>

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/atomic_save.h"
#include "kernel/document_io.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QApplication>
#endif

namespace vedit::workspace::search {

namespace {

OpenDocumentBridge *g_injected = nullptr;

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
// The same property the editor provider records when it opens a file
// (src/editor/kernel/ktexteditor_provider.cpp, kOpenFilePathProperty); the
// document URL is the fallback for a view opened some other way.
constexpr auto kOpenFilePathProperty = "vedit.openFilePath";

QHash<QString, KTextEditor::Document *> openDocuments()
{
    QHash<QString, KTextEditor::Document *> documents;
    QCoreApplication *application = QCoreApplication::instance();
    if (application == nullptr) {
        return documents;
    }
    QList<KTextEditor::View *> views;
    if (auto *widgets = qobject_cast<QApplication *>(application)) {
        for (QWidget *topLevel : widgets->topLevelWidgets()) {
            // A view created with no parent IS the top-level widget; findChildren
            // would not return it.
            if (auto *topLevelView = qobject_cast<KTextEditor::View *>(topLevel)) {
                views.append(topLevelView);
            }
            const QList<KTextEditor::View *> found =
                topLevel->findChildren<KTextEditor::View *>();
            for (KTextEditor::View *view : found) {
                if (view != nullptr) {
                    views.append(view);
                }
            }
        }
    }
    const QList<KTextEditor::View *> global = application->findChildren<KTextEditor::View *>();
    for (KTextEditor::View *view : global) {
        if (view != nullptr) {
            views.append(view);
        }
    }

    for (KTextEditor::View *view : views) {
        KTextEditor::Document *document = view->document();
        if (document == nullptr) {
            continue;
        }
        QString path = view->property(kOpenFilePathProperty).toString();
        if (path.isEmpty() && document->url().isLocalFile()) {
            path = document->url().toLocalFile();
        }
        if (path.isEmpty()) {
            continue;
        }
        documents.insert(QFileInfo(path).absoluteFilePath(), document);
    }
    return documents;
}

class EditorOpenDocumentBridge final : public OpenDocumentBridge
{
public:
    QStringList modifiedPaths() const override
    {
        QStringList modified;
        const QHash<QString, KTextEditor::Document *> documents = openDocuments();
        for (auto entry = documents.constBegin(); entry != documents.constEnd(); ++entry) {
            if (entry.value() != nullptr && entry.value()->isModified()) {
                modified.append(entry.key());
            }
        }
        modified.sort();
        return modified;
    }

    bool isOpen(const QString &absolutePath) const override
    {
        return openDocuments().contains(QFileInfo(absolutePath).absoluteFilePath());
    }

    bool save(const QString &absolutePath, QString *error) override
    {
        const QString path = QFileInfo(absolutePath).absoluteFilePath();
        KTextEditor::Document *document = openDocuments().value(path, nullptr);
        if (document == nullptr) {
            return true; // not open: the disk content is the only content
        }
        if (!document->isModified()) {
            // Nothing unsaved: the disk content already IS the buffer content.
            // Writing here would be a needless second write of the same file.
            return true;
        }
        const vedit::editor::AtomicSaveResult saved =
            vedit::editor::atomicSaveText(path, document->text().toUtf8());
        if (!saved.ok) {
            if (error != nullptr) {
                *error = saved.error;
            }
            return false;
        }
        document->setModified(false);
        return true;
    }

    QString contentToken(const QString &absolutePath) const override
    {
        const QString path = QFileInfo(absolutePath).absoluteFilePath();
        KTextEditor::Document *document = openDocuments().value(path, nullptr);
        if (document == nullptr) {
            return QString();
        }
        return tokenOf(document);
    }

    QString bufferText(const QString &absolutePath) const override
    {
        const QString path = QFileInfo(absolutePath).absoluteFilePath();
        KTextEditor::Document *document = openDocuments().value(path, nullptr);
        return document != nullptr ? document->text() : QString();
    }

    RefreshStatus refresh(const QString &absolutePath, const QString &expectedToken,
                          QString *error) override
    {
        const QString path = QFileInfo(absolutePath).absoluteFilePath();
        KTextEditor::Document *document = openDocuments().value(path, nullptr);
        if (document == nullptr) {
            return RefreshStatus::NotOpen;
        }
        // Blocker 1: a reload must never discard work typed after the
        // save-first step. A modified buffer is left untouched; so is a buffer
        // whose text no longer equals the token captured at save time.
        if (document->isModified()) {
            return RefreshStatus::Changed;
        }
        if (!expectedToken.isEmpty() && tokenOf(document) != expectedToken) {
            return RefreshStatus::Changed;
        }
        if (!vedit::editor::loadFileIntoDocument(document, path, error)) {
            return RefreshStatus::Changed;
        }
        document->setModified(false);
        return RefreshStatus::Reloaded;
    }

private:
    static QString tokenOf(KTextEditor::Document *document)
    {
        return QString::fromLatin1(
            QCryptographicHash::hash(document->text().toUtf8(), QCryptographicHash::Sha256)
                .toHex());
    }
};
#endif

} // namespace

OpenDocumentBridge *defaultOpenDocumentBridge()
{
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    static EditorOpenDocumentBridge bridge;
    return &bridge;
#else
    return nullptr;
#endif
}

void setOpenDocumentBridge(OpenDocumentBridge *bridge)
{
    g_injected = bridge;
}

OpenDocumentBridge *openDocumentBridge()
{
    return g_injected != nullptr ? g_injected : defaultOpenDocumentBridge();
}

} // namespace vedit::workspace::search
