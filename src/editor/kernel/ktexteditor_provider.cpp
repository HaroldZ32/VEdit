#include "ktexteditor_provider.h"

#include "atomic_save.h"
#include "diagnostics_annotations.h"
#include "document_io.h"
#include "editor_advanced.h"
#include "editor_context_menu.h"
#include "find_replace.h"
#include "lsp_actions.h"
#include "multi_cursor.h"
#include "signature_help.h"
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
#include "fold_breadcrumb.h"
#endif
#if defined(VEDIT_EDIT_CORE_HAVE_FIM)
#include "fim_wiring.h"
#endif

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <QCoreApplication>

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
    return QCoreApplication::translate("VEdit", "KTextEditor (edit-core)");
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
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    // T94: the live wiring of the folding/breadcrumb lanes. The binding is a
    // child of the view, so it dies with it; the provider only forwards status
    // changes to the pane through the EditorProvider seam.
    // T128: the gutter fold markers come from KDE/KTextEditor itself; make sure
    // the folding bar is enabled instead of relying on a user config default.
    view->setConfigValue(QStringLiteral("folding-bar"), true);
    auto *binding = new FoldBreadcrumbBinding(view, view);
    binding->setStatusListener([this, view]() { notifyStatus(view); });
#endif
#if defined(VEDIT_EDIT_CORE_HAVE_FIM)
    // T104: one FIM binding per view, a child of the view so it dies with the
    // pane. Alt+\\ requests, Tab accepts, Esc cancels; with FIM disabled (the
    // product default) the state machine reports it and no socket is opened.
    auto *fimBinding = new FimBinding(view, view);
    Q_UNUSED(fimBinding);
#endif
    // T131: inline diagnostics (annotation border) on every new view, same
    // lifetime as the fold/breadcrumb binding above.
    vedit::editor::DiagnosticsAnnotations::install(view);
    // T13: the in-view find/replace bar (Ctrl+F / Ctrl+H / F3 / Shift+F3). One
    // bar per view, a child of it, so it dies with the pane.
    vedit::editor::FindReplaceBar::install(view);
    // T14: block/multi-cursor and the AC-4.4 editing actions. Install order
    // matters: the filter installed LAST runs FIRST, so MultiCursorController
    // (installed after EditorEnhancements) sees a keystroke before the
    // single-cursor enhancements and can swallow it while several cursors are
    // active; with one cursor it declines and the enhancements handle the key.
    vedit::editor::EditorEnhancements::install(view);
    vedit::editor::MultiCursorController::install(view);
    // T15 (AC-3.1/3.2): the editor's right-click menu (built from the LSP action
    // channel) and the F8 / Shift+F8 diagnostic navigation. The menu reads the
    // capability gate the diagnostics lane publishes on the shell, so an action
    // this backend cannot serve is greyed WITH its reason instead of silently
    // missing; the navigator is installed last so its key filter runs first.
    vedit::editor::LspActionChannel::install(view);
    vedit::editor::EditorContextMenu::install(view);
    vedit::editor::DiagnosticNavigator::install(view);
    // T16 (AC-3.3/3.4): the editor-synthesised signatureHelp popup and the
    // advanced-feature controller (Ctrl+Alt+A menu, honest capability gating).
    vedit::editor::SignatureHelp::install(view);
    vedit::editor::EditorAdvanced::install(view);
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
            *error = QCoreApplication::translate("VEdit", "cannot open %1: not a regular file").arg(absolutePath);
        }
        return false;
    }
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    // Releases the previous document's folds before setText() replaces the text:
    // a document carrying fold ranges into a text replacement is what crashed
    // the search-jump selftest on the second open.
    // T128: no fold injection, so there is no registry to release before the
    // text replacement; KDE-native folding is user-driven only.
#endif
    if (!loadFileIntoDocument(view->document(), info.absoluteFilePath(), error)) {
        return false;
    }
    view->document()->setModified(false);
    view->setProperty(kOpenFilePathProperty, info.absoluteFilePath());
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    // T94: FirstOpen is one of the three allowed fold-rebuild triggers; the
    // breadcrumb is refreshed for the initial cursor position too.
    if (auto *binding = FoldBreadcrumbBinding::instanceFor(view)) {
        binding->onDocumentOpened();
    }
#endif
    return true;
}

QString EditCoreProvider::statusText(QWidget *editor) const
{
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    auto *binding = FoldBreadcrumbBinding::instanceFor(editor);
    return binding != nullptr ? binding->breadcrumbDisplay() : QString();
#else
    return EditorProvider::statusText(editor);
#endif
}

void EditCoreProvider::setStatusListener(QWidget *editor, std::function<void()> listener)
{
    if (editor == nullptr) {
        return;
    }
    if (!listener) {
        statusListeners_.remove(editor);
        return;
    }
    statusListeners_.insert(editor, std::move(listener));
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    if (auto *binding = FoldBreadcrumbBinding::instanceFor(editor)) {
        binding->setStatusListener([this, editor]() { notifyStatus(editor); });
    }
#endif
}

void EditCoreProvider::setModifiedListener(QWidget *editor, std::function<void(bool)> listener)
{
    if (editor == nullptr) {
        return;
    }
    const auto existing = modifiedConnections_.find(editor);
    if (existing != modifiedConnections_.end()) {
        QObject::disconnect(existing.value());
        modifiedConnections_.erase(existing);
    }
    KTextEditor::Document *document = documentFor(editor);
    if (document == nullptr || !listener) {
        // Not an edit-core widget (the demo/fallback pane) or an unsubscribe:
        // the base seam owns the QPlainTextEdit case.
        EditorProvider::setModifiedListener(editor, std::move(listener));
        return;
    }
    // The view is the connection's context: the document is its child, so both
    // die together and the subscription can never outlive the tab's editor.
    const QMetaObject::Connection connection = QObject::connect(
        document, &KTextEditor::Document::modifiedChanged, editor,
        [document, listener](KTextEditor::Document *) {
            if (listener) {
                listener(document->isModified());
            }
        });
    modifiedConnections_.insert(editor, connection);
}

void EditCoreProvider::notifyStatus(QWidget *editor)
{    const auto it = statusListeners_.constFind(editor);
    if (it != statusListeners_.constEnd() && it.value()) {
        it.value()();
    }
}

bool EditCoreProvider::isModified(QWidget *editor) const
{
    KTextEditor::Document *document = documentFor(editor);
    return document != nullptr ? document->isModified() : EditorProvider::isModified(editor);
}

vedit::app::EditorProvider::ViewState EditCoreProvider::viewState(QWidget *editor) const
{
    KTextEditor::View *view = viewFrom(editor);
    if (view == nullptr) {
        return EditorProvider::viewState(editor);
    }
    ViewState state;
    const KTextEditor::Cursor cursor = view->cursorPosition();
    state.caretLine = cursor.line();
    state.caretColumn = cursor.column();
    // RealLine (the document line), matched by setViewState's setScrollPosition:
    // one unit, so the archive value means the same thing on both sides.
    state.scrollTop = view->firstDisplayedLine(KTextEditor::View::RealLine);
    return state;
}

void EditCoreProvider::setViewState(QWidget *editor, const ViewState &state)
{
    KTextEditor::View *view = viewFrom(editor);
    if (view == nullptr) {
        EditorProvider::setViewState(editor, state);
        return;
    }
    if (state.hasCaret()) {
        view->setCursorPosition(
            KTextEditor::Cursor(std::max(0, state.caretLine), std::max(0, state.caretColumn)));
    }
    if (state.hasScroll()) {
        const int line = std::max(0, state.scrollTop);
        const QPointer<KTextEditor::View> guarded(view);
        const auto scroll = [guarded, line]() {
            if (guarded.isNull()) {
                return;
            }
            guarded->setScrollPosition(KTextEditor::Cursor(line, 0));
        };
        // setCursorPosition() scrolled to the caret; put the captured top line
        // back. The first call can be clamped while the page is not laid out yet
        // (a hidden tab has a zero-height viewport), so apply it now AND once more
        // on the next event-loop turn, exactly like the plain-text fallback.
        scroll();
        QTimer::singleShot(0, view, scroll);
    }
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
            *error = QCoreApplication::translate("VEdit", "save: the document has no file path yet");
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
