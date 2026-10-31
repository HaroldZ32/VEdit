#include "ktexteditor_provider.h"

#include "atomic_save.h"
#include "document_io.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QFileInfo>
#include <QPlainTextEdit>
#include <QUrl>

namespace vedit::editor {
namespace {

constexpr auto kOpenFilePathProperty = "vedit.openFilePath";

KTextEditor::View *viewFrom(QWidget *widget)
{
    return qobject_cast<KTextEditor::View *>(widget);
}

QWidget *unavailableEditor(QWidget *parent)
{
    auto *fallback = new QPlainTextEdit(parent);
    fallback->setObjectName(QStringLiteral("edit-core-unavailable"));
    fallback->setReadOnly(true);
    fallback->setPlainText(QStringLiteral(
        "KTextEditor is not available in this build (katepart plugin missing).\n"
        "The edit-core provider could not create a document."));
    return fallback;
}

} // namespace

KTextEditor::View *viewFor(QWidget *editorWidget)
{
    return viewFrom(editorWidget);
}

KTextEditor::Document *documentFor(QWidget *editorWidget)
{
    KTextEditor::View *view = viewFrom(editorWidget);
    return view != nullptr ? view->document() : nullptr;
}

bool isEditCoreWidget(const QWidget *editorWidget)
{
    return qobject_cast<const KTextEditor::View *>(editorWidget) != nullptr;
}

QString EditCoreProvider::id() const
{
    return QStringLiteral("ktexteditor");
}

QString EditCoreProvider::displayName() const
{
    return QStringLiteral("KTextEditor (edit-core)");
}

QWidget *EditCoreProvider::createEditor(QWidget *parent)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return unavailableEditor(parent);
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        return unavailableEditor(parent);
    }
    KTextEditor::View *view = document->createView(parent);
    if (view == nullptr) {
        delete document;
        return unavailableEditor(parent);
    }
    // One document per pane, and the document dies with its view. The pane owns
    // the widget (EditorPane deletes it when the provider changes).
    document->setParent(view);
    view->setObjectName(QStringLiteral("ktexteditor-view"));
    return view;
}

bool EditCoreProvider::openFile(QWidget *editor, const QString &absolutePath, QString *error)
{
    KTextEditor::View *view = viewFrom(editor);
    if (view == nullptr) {
        // Not a KTextEditor widget (demo fallback or no widget): the base class
        // knows how to drive a QPlainTextEdit.
        return EditorProvider::openFile(editor, absolutePath, error);
    }
    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot open %1: not a regular file").arg(absolutePath);
        }
        return false;
    }
    if (!loadFileIntoDocument(view->document(), info.absoluteFilePath(), error)) {
        return false;
    }
    view->document()->setModified(false);
    view->setProperty(kOpenFilePathProperty, info.absoluteFilePath());
    return true;
}

QString EditCoreProvider::currentFilePath(QWidget *editor) const
{
    if (viewFrom(editor) == nullptr) {
        return EditorProvider::currentFilePath(editor);
    }
    const QVariant property = editor != nullptr ? editor->property(kOpenFilePathProperty) : QVariant();
    if (property.isValid() && !property.toString().isEmpty()) {
        return property.toString();
    }
    KTextEditor::Document *document = documentFor(editor);
    return document != nullptr ? document->url().toLocalFile() : QString();
}

bool EditCoreProvider::saveFile(QWidget *editor, QString *error)
{
    KTextEditor::Document *document = documentFor(editor);
    if (document == nullptr) {
        return EditorProvider::saveFile(editor, error);
    }
    const QString path = currentFilePath(editor);
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("save: the document has no file path yet");
        }
        return false;
    }
    const AtomicSaveResult saved = atomicSaveText(path, document->text().toUtf8());
    if (!saved.ok) {
        if (error != nullptr) {
            *error = saved.error;
        }
        return false;
    }
    document->setModified(false);
    return true;
}

std::unique_ptr<vedit::app::EditorProvider> makeEditCoreProvider()
{
    return std::make_unique<EditCoreProvider>();
}

} // namespace vedit::editor
