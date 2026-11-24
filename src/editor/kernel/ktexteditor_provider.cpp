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

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QFileInfo>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <QCoreApplication>

namespace vedit::editor {
namespace {

constexpr auto kOpenFilePathProperty = "vedit.openFilePath";
constexpr auto kDocumentKeeperProperty = "vedit.documentKeeper";

// Batch 20261110d (split view): the lifetime of ONE document.
//
// The pre-split provider parented the document to its only view, so deleting
// the pane deleted the document. Split view gives a document several views, and
// that ownership becomes a data-loss bug: closing either pane would free the
// buffer (and its unsaved changes) the other pane is still showing. The keeper
// owns the document instead, counts its views, and deletes the document only
// when the LAST view is destroyed. It deliberately has NO QObject parent: the
// KTextEditor Editor singleton tears its children down while its own private
// state is already gone, and a live document deleted at that point segfaults
// inside DocumentPrivate (measured by vedit-native-fold-tests, which leaks its
// probe view). A parentless keeper also survives provider swaps, so a view
// destroyed later still reaches the right lifecycle hook.
class DocumentKeeper final : public QObject
{
public:
    explicit DocumentKeeper(KTextEditor::Document *document)
        : document_(document)
    {
    }

    void watchView(KTextEditor::View *view)
    {
        ++liveViews_;
        // The keeper is the receiver, not the provider: a view destroyed after
        // the provider swap still reaches the right lifecycle hook.
        QObject::connect(view, &QObject::destroyed, this, [this]() { viewDestroyed(); });
    }

private:
    void viewDestroyed()
    {
        --liveViews_;
        if (liveViews_ > 0 || document_.isNull()) {
            return;
        }
        // KTextEditor removes the view from document->views() before QObject's
        // destroyed() is emitted, so this really is the last view and deleting
        // the document cannot pull a live view out from under its pane.
        KTextEditor::Document *doomed = document_.data();
        document_ = nullptr;
        delete doomed;
        deleteLater();
    }

    QPointer<KTextEditor::Document> document_;
    int liveViews_ = 0;
};

// UI-Mosaic F2: the theme name KTextEditor can actually render, or an empty
// string when even the repository default is unusable (KTextEditor missing).
// KTextEditor renders with its OWN KSyntaxHighlighting repository
// (KateHlManager's singleton), which is NOT the repository ThemeManager
// enumerates (T127), so the name is validated against the editor's repository:
// an unknown name falls back to the editor's current global theme instead of
// leaving the view in an undefined state.
QString ktextEditorThemeName(const QString &requested)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return QString();
    }
    const KSyntaxHighlighting::Repository &repository = editor->repository();
    if (!requested.isEmpty() && repository.theme(requested).isValid()) {
        return requested;
    }
    const KSyntaxHighlighting::Theme current = editor->theme();
    if (current.isValid()) {
        return current.name();
    }
    return repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme).name();
}

// One view's syntax theme. The "theme" config key is the documented per-view
// switch (ViewPrivate applies the KSyntaxHighlighting theme to the renderer);
// it is what renders backgrounds, text and the per-token styles.
void applyThemeToView(KTextEditor::View *view, const QString &themeName)
{
    if (view == nullptr || themeName.isEmpty()) {
        return;
    }
    view->setConfigValue(QStringLiteral("theme"), themeName);
}

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
    // Batch 20261110d: since split view a document can have more than one View,
    // so it must NOT die with the view that happened to create it. The keeper
    // owns it and deletes it after the LAST view is destroyed.
    auto *keeper = new DocumentKeeper(document);
    document->setParent(keeper);
    document->setProperty(kDocumentKeeperProperty, QVariant::fromValue<QObject *>(keeper));
    KTextEditor::View *view = document->createView(parent);
    if (view == nullptr) {
        delete keeper;
        return unavailableEditor(parent);
    }
    keeper->watchView(view);
    configureView(view);
    return view;
}

QWidget *EditCoreProvider::createEditorForExistingDocument(QWidget *sourceEditor, QWidget *parent)
{
    KTextEditor::View *sourceView = viewFrom(sourceEditor);
    if (sourceView == nullptr) {
        return nullptr;
    }
    KTextEditor::Document *document = sourceView->document();
    if (document == nullptr) {
        return nullptr;
    }
    // RTTI, not qobject_cast: the keeper is an implementation detail of this
    // translation unit and deliberately has no Q_OBJECT macro (it exposes no
    // signals/slots of its own), so the Qt cast would not compile.
    auto *keeper = dynamic_cast<DocumentKeeper *>(
        document->property(kDocumentKeeperProperty).value<QObject *>());
    if (keeper == nullptr) {
        // Not a document this provider owns: sharing its buffer is not ours to
        // manage, so the caller keeps the empty-pane split behaviour.
        return nullptr;
    }
    KTextEditor::View *view = document->createView(parent);
    if (view == nullptr) {
        return nullptr;
    }
    keeper->watchView(view);
    // openFile() records the path on the VIEW; the second view must report the
    // same path so the tab label, Save and the session archive agree.
    const QVariant path = sourceEditor->property(kOpenFilePathProperty);
    if (path.isValid()) {
        view->setProperty(kOpenFilePathProperty, path);
    }
    configureView(view);
    return view;
}

void EditCoreProvider::configureView(KTextEditor::View *view)
{
    if (view == nullptr) {
        return;
    }
    view->setObjectName(QStringLiteral("ktexteditor-view"));
    // UI-Mosaic F2: this view renders the syntax theme the shell selected. The
    // setting is per VIEW (KTextEditor keeps one theme per view, not per
    // document), so every newly created view has to be told; a later switch is
    // pushed into all open views by applySyntaxTheme().
    applyThemeToView(view, syntaxTheme_);
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    // T94: the live wiring of the folding/breadcrumb lanes. The binding is a
    // child of the view, so it dies with it; the provider only forwards status
    // changes to the pane through the EditorProvider seam.
    // T128: the gutter fold markers come from KDE/KTextEditor itself; make sure
    // the folding bar is enabled instead of relying on a user config default.
    view->setConfigValue(QStringLiteral("folding-bar"), true);
    auto *binding = new FoldBreadcrumbBinding(view, view);
    // batch-20261113 T3: the binding is a child of the VIEW and outlives the
    // provider on window close, so its lambda must not capture `this`. It holds
    // a weak relay plus the view; after the provider is gone the lock fails and
    // the call is a no-op.
    binding->setStatusListener(
        [weakRelay = std::weak_ptr<StatusRelay>(statusRelay_), view]() {
            if (const std::shared_ptr<StatusRelay> relay = weakRelay.lock()) {
                relay->notify(view);
            }
        });
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
}

void EditCoreProvider::applySyntaxTheme(const QString &themeName)
{
    const QString resolved = ktextEditorThemeName(themeName);
    if (resolved.isEmpty()) {
        // KTextEditor is unavailable in this build: createEditor() already
        // falls back to the read-only plain-text stub, so there is no renderer
        // to configure and nothing to report as a failure.
        return;
    }
    syntaxTheme_ = resolved;
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return;
    }
    const QList<KTextEditor::Document *> documents = editor->documents();
    for (KTextEditor::Document *document : documents) {
        if (document == nullptr) {
            continue;
        }
        const QList<KTextEditor::View *> views = document->views();
        for (KTextEditor::View *view : views) {
            applyThemeToView(view, syntaxTheme_);
        }
    }
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
        statusRelay_->remove(editor);
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
        // batch-20261113 T3: clearing the registry entry is only half of an
        // unsubscribe. The view's binding still stores the forwarding lambda,
        // and the pre-fix code returned right here, leaving it installed so the
        // pane kept receiving updates it had already unsubscribed from.
        if (auto *binding = FoldBreadcrumbBinding::instanceFor(editor)) {
            binding->setStatusListener({});
        }
#endif
        return;
    }
    statusRelay_->set(editor, std::move(listener));
#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING)
    if (auto *binding = FoldBreadcrumbBinding::instanceFor(editor)) {
        const std::weak_ptr<StatusRelay> weakRelay = statusRelay_;
        binding->setStatusListener([weakRelay, editor]() {
            if (const std::shared_ptr<StatusRelay> relay = weakRelay.lock()) {
                relay->notify(editor);
            }
        });
    }
#endif
    // batch-20261113 T3: a view that Qt destroys WITHOUT an unsubscribe must not
    // leave a dangling key in the registry. The connection's context is the view
    // itself, so it disappears with the view; the captured pointer is only used
    // as a key, never dereferenced.
    QObject::connect(editor, &QObject::destroyed, editor,
                     [weakRelay = std::weak_ptr<StatusRelay>(statusRelay_), editor]() {
                         if (const std::shared_ptr<StatusRelay> relay = weakRelay.lock()) {
                             relay->remove(editor);
                         }
                     });
}

int EditCoreProvider::statusListenerCount() const
{
    return statusRelay_ != nullptr ? statusRelay_->size() : 0;
}

EditCoreProvider::~EditCoreProvider()
{
    // batch-20261113 T3: this runs BEFORE the views die (app_shell.h declaration
    // order). Dropping the provider's reference is the whole teardown: the
    // bindings' weak_ptrs expire, so any later invocation is a silent no-op.
    statusRelay_.reset();
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
    // The view is the connection's context: destroying this pane's view drops
    // its subscription even though the shared document outlives it (split view).
    const QMetaObject::Connection connection = QObject::connect(
        document, &KTextEditor::Document::modifiedChanged, editor,
        [document, listener](KTextEditor::Document *) {
            if (listener) {
                listener(document->isModified());
            }
        });
    modifiedConnections_.insert(editor, connection);
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
