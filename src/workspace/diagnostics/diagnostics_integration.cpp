#include "diagnostics_integration.h"

#include "../top_scope.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)

#include "autofix.h"
#include "badge_text.h"
#include "diagnostic_hover.h"
#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "project_lint_panel.h"
#include "lsp_backend.h"
#include "project_lint_notice.h"
#include "realtime_file_scope.h"
#include "symbol_scan.h"
#include "slang_backend.h"
#include "waiver_actions.h"
#include "../project/language_registry.h"
#include "toolchain.h"

#include "../settings/settings_keys.h"
// UX batch 2 / U6: the lint-knob composition seam is header-only, so this lane
// gains no link dependency on the settings library.
#if __has_include("../settings/lint_options.h")
#include "../settings/lint_options.h"
#define VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS 1
#endif

#include "app_shell.h"
#include "command_registry.h"
#include "i18n.h"
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
#include "diagnostics_annotations.h"
#endif
#include "editor_area.h"
#include "panel_registry.h"
#include "jump/jump_registry.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
// Batch 2 (AC-2.1/AC-2.2): the editor lane owns the WorkspaceEdit application
// and the references rows; this lane is the one that talks to the LSP session.
#include "lsp_actions.h"
#endif

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QSet>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QObject>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextDocument>
#include <QStringList>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include <algorithm>
#include <cstdio>

#include <functional>
#include <memory>
#include <vector>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
#include <vd_ffi.h>
#endif

#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

namespace vedit::workspace {
namespace {

/// Owns the controller for the shell's lifetime (the controller is not a
/// QObject on purpose: its headless core has no signal surface).
class ControllerHolder final : public QObject {
public:
    explicit ControllerHolder(std::unique_ptr<diagnostics::DiagnosticsController> controller,
                              QObject *parent)
        : QObject(parent)
        , controller_(std::move(controller))
    {
    }
    diagnostics::DiagnosticsController *controller() const { return controller_.get(); }

private:
    std::unique_ptr<diagnostics::DiagnosticsController> controller_;
};

/// T118: follow the ACTIVE editor (open + edit), not only the project file set.
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
/// T131/T11: push the analysed file's diagnostics into the ACTIVE editor view as
/// inline annotations (KTextEditor::AnnotationModel + MovingRange squiggles).
/// T11 fills the exact span (column/endLine/endColumn) and, on an empty result,
/// pushes an EMPTY set so the previous backend's markers cannot linger after a
/// switch.
class AnnotationBridge final : public QObject {
public:
    AnnotationBridge(QObject *parent, vedit::app::ApplicationShell *shell,
                     diagnostics::DiagnosticsController *controller)
        : QObject(parent)
        , shell_(shell)
        , controller_(controller)
        , timer_(new QTimer(this))
    {
        connect(timer_, &QTimer::timeout, this, [this]() { push(); });
        timer_->start(300);
    }

private:
    void push()
    {
        vedit::app::EditorPane *pane = shell_->editorArea() != nullptr
                                           ? shell_->editorArea()->activePane()
                                           : nullptr;
        if (pane == nullptr) {
            return;
        }
        auto *view = dynamic_cast<KTextEditor::View *>(pane->editorWidget());
        if (view == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        if (path.isEmpty()) {
            return;
        }
        QList<vedit::editor::DiagnosticAnnotation> entries;
        // R2: the squiggles/gutter come from the ONE merged view. A file with a
        // live project-pass verdict shows ONLY that verdict; an edited file has
        // already fallen back to the per-file/LSP layer.
        for (const diagnostics::DiagnosticEntry &entry : controller_->mergedDiagnosticsForFile(path)) {
            vedit::editor::DiagnosticAnnotation annotation;
            annotation.line = entry.line;
            // T11/AC-5.1: the exact span, so the squiggle covers the token, not
            // the whole line. Negative end information falls back to the line.
            annotation.column = entry.column;
            annotation.endLine = entry.endLine;
            annotation.endColumn = entry.endColumn;
            annotation.severity = entry.severity;
            annotation.message = entry.message;
            annotation.rule = entry.code.isEmpty() ? entry.source : entry.code;
            entries.append(annotation);
        }
        const QString signature = signatureOf(entries);
        if (signature == lastSignature_ && path == lastPath_) {
            return;
        }
        lastSignature_ = signature;
        lastPath_ = path;
        vedit::editor::DiagnosticsAnnotations::install(view);
        // An empty list is pushed on purpose: after a backend switch (or after a
        // fix) the old backend's markers must disappear, not linger mislabelled.
        vedit::editor::DiagnosticsAnnotations::setForView(view, entries);
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            qInfo("diagnostics: %lld inline annotation(s) on %s",
                  static_cast<long long>(entries.size()), qPrintable(path));
        }
    }

    static QString signatureOf(const QList<vedit::editor::DiagnosticAnnotation> &entries)
    {
        QStringList parts;
        for (const vedit::editor::DiagnosticAnnotation &entry : entries) {
            // The rule and the message are part of the annotation: a project pass
            // that replaces a row at the same span/severity with a different
            // rule/message must re-push, or the old tooltip lingers.
            parts.append(QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
                             .arg(entry.line).arg(entry.column)
                             .arg(entry.endLine).arg(entry.endColumn).arg(entry.severity)
                             .arg(entry.rule, QString::number(qHash(entry.message))));
        }
        return parts.join(QLatin1Char('\n'));
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    diagnostics::DiagnosticsController *controller_ = nullptr;
    QTimer *timer_ = nullptr;
    QString lastSignature_;
    QString lastPath_;
};
#endif

class ActiveFileWatcher final : public QObject {
public:
    ActiveFileWatcher(QObject *parent, vedit::app::ApplicationShell *shell,
                      diagnostics::DiagnosticsController *controller)
        : QObject(parent)
        , shell_(shell)
        , controller_(controller)
        , tick_(new QTimer(this))
        , debounce_(new QTimer(this))
    {
        if (qEnvironmentVariableIsSet("VEDIT_NO_ACTIVE_FILE_WATCHER")) {
            return;
        }
        // BLOCKER fix: publish the cap and the counters even before the first
        // file, so "the watcher is on and applies this cap" is observable state
        // rather than something a test has to infer from memory use.
        publishWatchStats();
        debounce_->setSingleShot(true);
        debounce_->setInterval(700);
        connect(debounce_, &QTimer::timeout, this, [this]() { analyze(); });
        connect(tick_, &QTimer::timeout, this, [this]() { poll(); });
        tick_->start(400);
        // N2-r: subscribe on the ONE open seam (ApplicationShell::fileOpened), so
        // the document is already tracked when the user's FIRST keystroke lands.
        // The 400 ms poll alone can only subscribe a tab after the fact, which is
        // the pre-subscription window the review proved.
        QObject::connect(shell_, &vedit::app::ApplicationShell::fileOpened, this,
                         [this](const QString &path) { trackOpenedPath(path); });
    }

    /// T11: a backend switch re-runs the analysis of the SAME active buffer
    /// immediately (no application restart, no panel rebuild).
    ///
    /// It also invalidates the watcher's change baseline for that buffer: the
    /// verdict on screen was produced by the OLD engine, so the next 400 ms tick
    /// must treat the buffer as CHANGED (`markFileEdited` + the debounce). The
    /// pre-cap code did that by clearing `lastText_`, which made the next poll's
    /// text comparison differ and take the edit branch; this flag keeps the same
    /// one-shot contract without copying the document to express it.
    void reanalyzeNow()
    {
        analyze();
        invalidateNextChange_ = true;
    }

private:
    /// BLOCKER fix (batch 20261110d): the watcher's hard cap. Above it the
    /// document is NEVER copied and the file is NEVER read (`analyze()` checks
    /// metadata first); one named line reports why the file is not analysed.
    static constexpr qint64 kCapBytes = vedit::workspace::kActiveFileWatchByteCap;
    /// AC-3.4 (batch-20261115): the level-1 closure of ONE file is bounded, and
    /// the union the session covers (every open document's closure) is bounded
    /// again. Both bounds are published, and a truncated set says so in ONE
    /// status line - an oversized closure never blocks or crashes the watcher.
    static constexpr int kRealtimeClosureMaxFiles = 64;
    static constexpr int kRealtimeSessionMaxFiles = 128;

    /// Copy the buffer's text. The ONLY caller is `analyze()`, after the cap
    /// check passed, so this allocation is bounded by the cap for every document
    /// size. The counters (published as `vedit.diagnostics.activeWatch`) make
    /// "the watcher did not copy the document" a measured fact, not an opinion.
    QString bufferText(QWidget *widget)
    {
        QString text;
        if (widget != nullptr) {
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
            auto *view = qobject_cast<KTextEditor::View *>(widget);
            if (view != nullptr) {
                if (view->document() != nullptr) {
                    text = view->document()->text();
                }
            } else if (auto *plain = qobject_cast<QPlainTextEdit *>(widget)) {
                text = plain->toPlainText();
            }
#else
            if (auto *plain = qobject_cast<QPlainTextEdit *>(widget)) {
                text = plain->toPlainText();
            }
#endif
        }
        ++bufferReads_;
        bufferBytes_ += static_cast<quint64>(text.size() * 2); // UTF-16 in memory
        publishWatchStats();
        return text;
    }

    /// Characters in the buffer WITHOUT copying it. KTextEditor exposes no
    /// document-size getter, so the count is summed from line lengths with an
    /// early exit as soon as the cap is certainly exceeded (a document with more
    /// lines than the cap is over it by the line separators alone). QTextDocument
    /// has an O(1) `characterCount()`. UTF-16 code units are used for both; for
    /// ASCII HDL source they equal UTF-8 bytes, and a non-ASCII document is
    /// bounded the same way because the copy is UTF-16 too.
    static qint64 bufferCharacterCount(QWidget *widget)
    {
        if (widget == nullptr) {
            return 0;
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(widget)) {
            KTextEditor::Document *document = view->document();
            if (document == nullptr) {
                return 0;
            }
            const int lines = document->lines();
            if (static_cast<qint64>(lines) > kCapBytes) {
                return kCapBytes + 1;
            }
            qint64 total = 0;
            for (int line = 0; line < lines; ++line) {
                const int lineLength = document->lineLength(line);
                if (lineLength < 0) {
                    return kCapBytes + 1; // unmeasurable: never copy on a guess
                }
                total += static_cast<qint64>(lineLength) + 1;
                if (total > kCapBytes) {
                    return total;
                }
            }
            return total;
        }
#endif
        if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
            QTextDocument *document = text->document();
            return document != nullptr ? static_cast<qint64>(document->characterCount()) : 0;
        }
        return 0;
    }

    /// O(1) change counter for the 400 ms tick - NO document copy. Edits arrive
    /// through each document's own `textChanged` signal (`trackDocument`); this
    /// counter is the fallback for a change whose signal was missed, and it is
    /// only acted on while the buffer is dirty (a save invalidates no verdict).
    static qint64 bufferRevision(QWidget *widget)
    {
        if (widget == nullptr) {
            return -1;
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(widget)) {
            return view->document() != nullptr ? view->document()->revision() : -1;
        }
#endif
        if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
            return text->document() != nullptr ? text->document()->revision() : -1;
        }
        return -1;
    }

    /// The watcher's observable state through the same shell-property seam as
    /// `vedit.diagnostics.analysisAvailability`: the cap in force and how much
    /// work this path really did. A support log or a test reads this instead of
    /// guessing from process memory.
    void publishWatchStats() const
    {
        QVariantMap stats;
        stats.insert(QStringLiteral("capBytes"), kCapBytes);
        stats.insert(QStringLiteral("bufferReads"), bufferReads_);
        stats.insert(QStringLiteral("bufferBytes"), bufferBytes_);
        stats.insert(QStringLiteral("analyzeCalls"), analyzeCalls_);
        stats.insert(QStringLiteral("tooLargeSkips"), tooLargeSkips_);
        stats.insert(QStringLiteral("tooLargeNotices"), tooLargeNotices_);
        stats.insert(QStringLiteral("disabledPath"), disabledPath_);
        shell_->setProperty("vedit.diagnostics.activeWatch", stats);
    }

    static QString humanBytes(qint64 bytes)
    {
        return QStringLiteral("%1 MiB")
            .arg(QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1));
    }

    /// BLOCKER fix: the cap skipped this file. A skipped analysis is a named
    /// STATE - one status-bar/shell line plus the availability property - never
    /// a silent "no diagnostics" that a crashed engine would also produce. The
    /// line is emitted ONCE per file (the watcher also de-duplicates by path and
    /// the shell de-duplicates identical text), so an idle document cannot spam.
    void noteAnalysisDisabled(const QString &path, qint64 bytes)
    {
        ++tooLargeSkips_;
        const QString reason =
            QCoreApplication::translate(
                "VEdit",
                "Analysis disabled: file too large — %1 is %2, above the %3 active-file "
                "analysis limit")
                .arg(QDir::toNativeSeparators(path), humanBytes(bytes), humanBytes(kCapBytes));
        QVariantMap availability;
        availability.insert(QStringLiteral("path"), path);
        availability.insert(QStringLiteral("available"), false);
        availability.insert(QStringLiteral("reason"), reason);
        shell_->setProperty("vedit.diagnostics.analysisAvailability", availability);
        // L1.2: the same reason reaches the Outline's empty state (the panel
        // used to show a blank "No diagnostics" for a capped file).
        controller_->setDocumentAnalysisAvailability(path, false, reason);
        if (path != disabledPath_) {
            disabledPath_ = path;
            ++tooLargeNotices_;
            shell_->addShellDiagnostic(reason);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(reason, 8000);
            }
        }
        publishWatchStats();
    }

    /// The buffer's own dirty flag. N2: a tab that was edited BEFORE the first
    /// poll subscribed to it emitted its `textChanged` while nobody listened, so
    /// the path-change branch below must consult this flag instead of assuming
    /// "new path = untouched" (the old code set `lastText_` and returned).
    static bool bufferModified(QWidget *widget)
    {
        if (widget == nullptr) {
            return false;
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(widget)) {
            return view->document() != nullptr && view->document()->isModified();
        }
#endif
        if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
            return text->document() != nullptr && text->document()->isModified();
        }
        return false;
    }

    /// N2-r: the open seam hands us a path AFTER the tab became active. Track
    /// that tab's document NOW instead of waiting for the next 400 ms poll, so a
    /// first edit is heard even when the tab is switched away immediately.
    void trackOpenedPath(const QString &path)
    {
        if (path.isEmpty() || shell_->editorArea() == nullptr) {
            return;
        }
        for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
            if (pane == nullptr) {
                continue;
            }
            for (int index = 0; index < pane->tabCount(); ++index) {
                if (pane->tabFilePath(index) == path) {
                    trackDocument(pane->tabEditorWidget(index), path);
                }
            }
        }
    }

    /// Subscribe to one open document's own change signal. This is what makes
    /// R2 single-sourced across ALL open files: a BACKGROUND tab edited while
    /// another tab is active invalidates ITS file immediately, not when (or if)
    /// the 400 ms poll happens to look at it. `documents_` maps the document to
    /// its CURRENT path (an untitled tab that is saved-as updates the map).
    void trackDocument(QWidget *editor, const QString &path)
    {
        if (editor == nullptr || path.isEmpty()) {
            return;
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
            KTextEditor::Document *document = view->document();
            if (document == nullptr) {
                return;
            }
            if (documents_.contains(document)) {
                documents_.insert(document, path); // save-as / rename
                return;
            }
            documents_.insert(document, path);
            QObject::connect(document, &KTextEditor::Document::textChanged, this,
                             [this, document]() { noteDocumentEdited(document); });
            QObject::connect(document, &QObject::destroyed, this,
                             [this, document]() { documents_.remove(document); });
            // N2-r: a tab can be edited BEFORE anything subscribed to it (opened
            // and edited inside one event-loop turn, then switched away). That
            // first edit emitted no signal we could hear; the buffer's dirty flag
            // is the only evidence left, and an edited buffer's upper (project)
            // verdict is void. Checking EVERY newly tracked document - not just
            // the active file at poll time - closes that window.
            if (bufferModified(editor)) {
                controller_->markFileEdited(path);
            }
            return;
        }
#endif
        if (auto *text = qobject_cast<QPlainTextEdit *>(editor)) {
            QTextDocument *document = text->document();
            if (document == nullptr) {
                return;
            }
            if (documents_.contains(document)) {
                documents_.insert(document, path);
                return;
            }
            documents_.insert(document, path);
            QObject::connect(document, &QTextDocument::contentsChanged, this,
                             [this, document]() { noteDocumentEdited(document); });
            QObject::connect(document, &QObject::destroyed, this,
                             [this, document]() { documents_.remove(document); });
            if (bufferModified(editor)) {
                controller_->markFileEdited(path);
            }
        }
    }

    void noteDocumentEdited(QObject *document)
    {
        const QString file = documents_.value(document);
        if (file.isEmpty()) {
            return;
        }
        // R2: a file edited after the project pass loses its upper-layer verdict
        // at ONCE, whether or not it is the active tab.
        controller_->markFileEdited(file);
        vedit::app::EditorPane *pane = shell_->editorArea() != nullptr
                                           ? shell_->editorArea()->activePane()
                                           : nullptr;
        if (pane != nullptr && pane->currentFilePath() == file) {
            debounce_->start(); // the active buffer still gets its file-scope analysis
        }
    }

    void poll()
    {
        // Enumerate EVERY pane and EVERY tab: an edit in a background tab is
        // real and must invalidate that file's upper layer too.
        if (shell_->editorArea() != nullptr) {
            for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
                if (pane == nullptr) {
                    continue;
                }
                for (int index = 0; index < pane->tabCount(); ++index) {
                    trackDocument(pane->tabEditorWidget(index), pane->tabFilePath(index));
                }
            }
        }
        vedit::app::EditorPane *pane = shell_->editorArea() != nullptr
                                           ? shell_->editorArea()->activePane()
                                           : nullptr;
        if (pane == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        QWidget *widget = pane->editorWidget();
        if (path.isEmpty() || widget == nullptr) {
            lastPath_.clear();
            lastRevision_ = -1;
            return;
        }
        // BLOCKER fix: the tick NEVER copies the document. Edits are heard
        // through each document's own textChanged signal (trackDocument); this
        // poll only follows the active PATH and keeps an O(1) revision counter
        // as the signal-miss fallback. Before the fix this branch called
        // document()->text() every 400 ms and kept a second full copy in
        // lastText_, so an idle 300 MB file asked for ~1.2 GB per second.
        //
        // G1-a: a project file set that ARRIVES or changes while the user is
        // idle (import a `.f`, switch the root) re-scopes the running session on
        // THIS tick - otherwise a document opened before the import would keep
        // its place in the compile set until the next keystroke.
        if (refreshProjectScope()) {
            lastRevision_ = bufferRevision(widget);
            analyze();
            return;
        }
        if (path != lastPath_) {
            lastPath_ = path;
            lastRevision_ = bufferRevision(widget);
            invalidateNextChange_ = false; // a path switch supersedes the forced edit
            // N2: the first edit of a newly opened file can land before this
            // poll subscribed to its document (the tab is enumerated only here).
            // That edit emitted no signal we could hear, so the buffer's dirty
            // flag is the only evidence left - and it must invalidate the old
            // project verdict now, or the upper layer suppresses the fresh
            // file-scope analysis until some later keystroke.
            if (bufferModified(widget)) {
                controller_->markFileEdited(path);
            }
            analyze();
            return;
        }
        if (invalidateNextChange_) {
            // `reanalyzeNow()`'s one-shot contract (see its comment): the backend
            // switch already re-analysed the buffer, but the verdict from the old
            // engine must be invalidated by the same edit path a real change
            // takes. No text is copied to decide this.
            invalidateNextChange_ = false;
            lastRevision_ = bufferRevision(widget);
            controller_->markFileEdited(path);
            debounce_->start();
            return;
        }
        const qint64 revision = bufferRevision(widget);
        if (revision != lastRevision_) {
            lastRevision_ = revision;
            // The document is dirty: an edit happened without a signal we could
            // hear. A save changes no verdict the watcher must invalidate, so a
            // clean buffer is left alone.
            if (bufferModified(widget)) {
                controller_->markFileEdited(path);
                debounce_->start();
            }
        }
    }

    /// The module closure the module-tree lane publishes (AC-3.1): file ->
    /// definition files of the modules it DIRECTLY instantiates. Plain QVariant
    /// data on the shell property seam, so this lane keeps building standalone;
    /// an empty map means "no module catalog" and the analysis stays single-file.
    QVariantMap moduleClosure() const
    {
        return shell_->property("vedit.moduleTree.closure").toMap();
    }

    // The file-list expansion and the catalog arrive independently. Either can
    // change the realtime scope while the active document remains open.
    bool refreshProjectScope()
    {
        const QVariant revision = shell_->property("vedit.diagnostics.projectFilesRevision");
        const qulonglong catalogRevision = shell_->property("vedit.moduleTree.scopeRevision").toULongLong();
        const QStringList shadowed = shell_->property("vedit.moduleTree.shadowedFiles").toStringList();
        const bool project = shell_->property("vedit.project.hasFilelist").toBool();
        if (revision == goldenRevision_ && catalogRevision == catalogRevision_
            && shadowed == shadowedFiles_ && project == hasFilelist_) {
            return false;
        }
        goldenRevision_ = revision;
        catalogRevision_ = catalogRevision;
        hasFilelist_ = project;
        shadowedFiles_ = shadowed;
        const QStringList members = shell_->property("vedit.diagnostics.projectFiles").toStringList();
        goldenFiles_ = diagnostics::realtime_scope::goldenFileSetFrom(members);
        projectIndex_ = diagnostics::realtime_scope::dropShadowedFiles(
            members, diagnostics::realtime_scope::shadowedFileSetFrom(shadowed));
        projectIndex_.removeDuplicates();
        projectIndex_.sort();
        return true;
    }

    /// AC-3.1 + G1-a: the realtime analysis file set of ONE file. Inside the
    /// golden set it is the file itself plus its one-level closure (capped);
    /// OUTSIDE it, only the file itself - the user keeps single-file diagnostics
    /// on a file that is not in the project, but its definitions can never enter
    /// the project-wide compile set. The rule lives in `realtime_file_scope.h`
    /// so the self-test asserts it without a shell.
    QStringList realtimeFileSetFor(const QString &path, bool *truncated = nullptr) const
    {
        return diagnostics::realtime_scope::realtimeFileSetFor(
            goldenFiles_, moduleClosure(), path, kRealtimeClosureMaxFiles, truncated);
    }

    /// A61.3 + G1-a: the session covers the union of the GOLDEN open documents'
    /// closures. A document outside the project file set contributes NOTHING -
    /// before G1-a every open tab did, so merely opening a file pulled its module
    /// definitions into the project-wide compile set (the reported defect: slang
    /// and goto landed on a file no `.f` ever named). The union only moves when a
    /// document is opened or closed, so switching between documents that are
    /// already open reuses the running session. `outsideFileCount_` publishes how
    /// many distinct open documents were excluded.
    QStringList openDocumentSessionSet(bool *truncated = nullptr)
    {
        QStringList openDocuments;
        if (shell_->editorArea() != nullptr) {
            for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
                if (pane == nullptr) {
                    continue;
                }
                for (int index = 0; index < pane->tabCount(); ++index) {
                    const QString tab = pane->tabFilePath(index);
                    if (!tab.isEmpty()) {
                        openDocuments.append(tab);
                    }
                }
            }
        }
        return diagnostics::realtime_scope::openDocumentSessionSet(
            goldenFiles_, moduleClosure(), openDocuments, kRealtimeClosureMaxFiles,
            kRealtimeSessionMaxFiles, &outsideFileCount_, truncated);
    }

    /// The AC-3/AC-5 probe surface: what the realtime analysis covers right now.
    void publishRealtimeSets(const QString &path)
    {
        // G1-a: membership is decided against the CURRENT golden set - a file set
        // that arrives (or changes) after a document was opened applies on this
        // tick.
        refreshProjectScope();
        // G1-c: a file the catalog dropped from the compiler input never enters
        // the realtime compile set either. In practice the closure cannot name
        // one (it resolves a module to its ADOPTED definition file, and a
        // shadowed file defines no adopted module), so this is the DEFENSIVE
        // half - the same rule the project pass applies.
        const diagnostics::realtime_scope::ShadowedFileSet shadowed =
            diagnostics::realtime_scope::shadowedFileSetFrom(
                shell_->property("vedit.moduleTree.shadowedFiles").toStringList());
        // G1-a/G1-c: ONE decision about the ACTIVE document, and it decides the
        // SESSION (the compiler input), not just the published analysis set:
        //   * a golden member the catalog did not shadow -> it joins the golden
        //     session and gets `{path} ∪ closure[path]` requested;
        //   * anything else (outside the file list, or a shadowed loser) -> NO:
        //     it is analysed ALONE, so no project definition and no winning twin
        //     of its own module can share its build context.
        const bool activeJoins = diagnostics::realtime_scope::activeDocumentJoinsProjectSession(
            goldenFiles_, shadowed, path);
        bool closureTruncated = false;
        const QStringList analysedSet =
            activeJoins ? realtimeFileSetFor(path, &closureTruncated) : QStringList{path};
        bool sessionTruncated = false;
        const QStringList sessionUnion = openDocumentSessionSet(&sessionTruncated);
        // The ACTIVE document is kept in the ANALYSIS set even when shadowed: a
        // user who opens the losing file still gets single-file diagnostics for
        // it (`dropShadowedFiles`'s `keep`). Its SESSION is the separate,
        // stricter decision above.
        const QStringList analysisSet =
            diagnostics::realtime_scope::dropShadowedFiles(analysedSet, shadowed, path);
        const QStringList sessionSet =
            diagnostics::realtime_scope::dropShadowedFiles(sessionUnion, shadowed);
        // The controller starts the language server over this answer, so it is
        // published BEFORE the properties below (analyse() calls this method
        // before analyzeFile()).
        controller_->setActiveDocumentJoinsProjectSession(activeJoins);
        controller_->setRealtimeProjectIndex(hasFilelist_ && activeJoins
            ? (projectIndex_.isEmpty() ? QStringList{path} : projectIndex_) : QStringList{});
        shell_->setProperty("vedit.diagnostics.realtimeFileSet", analysisSet);
        shell_->setProperty("vedit.diagnostics.sessionFileSet", sessionSet);
        QVariantMap sets;
        sets.insert(QStringLiteral("files"), analysisSet.size());
        sets.insert(QStringLiteral("sessionFiles"), sessionSet.size());
        sets.insert(QStringLiteral("closureCap"), kRealtimeClosureMaxFiles);
        sets.insert(QStringLiteral("sessionCap"), kRealtimeSessionMaxFiles);
        sets.insert(QStringLiteral("closureTruncated"), closureTruncated);
        sets.insert(QStringLiteral("sessionTruncated"), sessionTruncated);
        // G1-a observability: the size of the golden project file set and how
        // many open documents were excluded from the session because they are
        // not in it (0 + 0 = single-file mode, where no project set exists).
        sets.insert(QStringLiteral("goldenFileCount"), goldenFiles_.size());
        sets.insert(QStringLiteral("outsideFileCount"), outsideFileCount_);
        // G1-a/G1-c observability: the active document is analysed alone, i.e.
        // the compile input is `{active}` even though a project session exists.
        sets.insert(QStringLiteral("activeFileAlone"), !activeJoins);
        // G1-c observability: how many of the session's files the catalog's
        // shadowed list removed (0 when nothing is shadowed, which is the normal
        // case - the closure only names ADOPTED definition files).
        sets.insert(QStringLiteral("shadowedDropped"),
                    sessionUnion.size() - sessionSet.size());
        shell_->setProperty("vedit.diagnostics.realtimeSets", sets);
        if ((closureTruncated || sessionTruncated) && !truncationNoted_) {
            truncationNoted_ = true;
            shell_->addShellDiagnostic(
                QCoreApplication::translate(
                    "VEdit",
                    "Realtime analysis covers at most %1 file(s) per document and %2 files in "
                    "total; the rest are not analysed until fewer documents are open")
                    .arg(kRealtimeClosureMaxFiles)
                    .arg(kRealtimeSessionMaxFiles));
        } else if (!closureTruncated && !sessionTruncated) {
            truncationNoted_ = false;
        }
        controller_->setRealtimeSessionFiles(sessionSet);
        // batch-20261118 AC-5: the REQUEST/analysis scope is the active
        // document's one-level closure, never the union of every open tab. The
        // session keeps the union above for reuse; only this set is asked for
        // and considered.
        controller_->setRealtimeRequestFiles(analysisSet);
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            // One observable line: users and verifiers can confirm the scope
            // without reading implementation.
            std::printf("REALTIME-SET active='%s' files=%d session=%d\n",
                        qPrintable(path), static_cast<int>(analysisSet.size()),
                        static_cast<int>(sessionSet.size()));
            std::fflush(stdout);
        }
        QVariantMap sessions;
        sessions.insert(QStringLiteral("starts"),
                        QVariant::fromValue<qulonglong>(controller_->lspStartCount()));
        sessions.insert(QStringLiteral("stops"),
                        QVariant::fromValue<qulonglong>(controller_->lspStopCount()));
        shell_->setProperty("vedit.diagnostics.lspSessions", sessions);
    }

    void analyze()
    {
        vedit::app::EditorPane *pane = shell_->editorArea() != nullptr
                                           ? shell_->editorArea()->activePane()
                                           : nullptr;
        if (pane == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        QWidget *widget = pane->editorWidget();
        if (path.isEmpty() || widget == nullptr) {
            return;
        }
        // RM-11: a document is analysed only when this build has an ANALYZER
        // for its language. A `.f` manifest, a `.md` note, a `.tcl` script has
        // no language entry, so it is never handed to a Verilog server - the
        // user's "syntax error on my filelist" was this call analysing the
        // manifest as HDL. The outcome is a STATE (published below), not a
        // skip log line and never a diagnostic row. The check runs BEFORE any
        // buffer copy: a language this build cannot analyse needs no text.
        const project::LanguageRegistry &languages = project::LanguageRegistry::instance();
        if (!languages.supports(path, project::Capability::Diagnostics)) {
            const QString reason = languages.unavailableReason(path, project::Capability::Diagnostics);
            QVariantMap availability;
            availability.insert(QStringLiteral("path"), path);
            availability.insert(QStringLiteral("available"), false);
            availability.insert(QStringLiteral("reason"), reason);
            shell_->setProperty("vedit.diagnostics.analysisAvailability", availability);
            // L1.2: the panel reads this same one line. Without it the Outline
            // showed a blank "no diagnostics" for a file this build can never
            // analyse (the user's "no squiggles and no hint at all").
            controller_->setDocumentAnalysisAvailability(path, false, reason);
            return;
        }
        // BLOCKER fix: decide from METADATA before copying anything. The on-disk
        // size is O(1); the document size is summed without a copy. Above the cap
        // neither the document nor the file is read, and the skip is named once.
        const qint64 fileBytes = QFileInfo(path).size();
        const qint64 documentBytes = bufferCharacterCount(widget);
        const qint64 watchedBytes = qMax(fileBytes, documentBytes);
        if (watchedBytes > kCapBytes) {
            noteAnalysisDisabled(path, watchedBytes);
            return;
        }
        disabledPath_.clear(); // small again: a future skip is a new state
        const QString text = bufferText(widget);
        if (text.isEmpty()) {
            controller_->setDocumentAnalysisAvailability(
                path, false,
                QCoreApplication::translate("VEdit", "the editor buffer is empty"));
            return;
        }
        // A bounded read, not readAll(): even if the file GREW between the stat
        // above and this line, the allocation cannot exceed the cap (plus one
        // byte, which is how the growth is detected) - the guarantee does not
        // rest on a racy stat.
        QFile file(path);
        QByteArray diskBytes;
        if (file.open(QIODevice::ReadOnly)) {
            diskBytes = file.read(kCapBytes + 1);
        }
        if (diskBytes.size() > kCapBytes) {
            noteAnalysisDisabled(path, diskBytes.size());
            return;
        }
        const QString disk = QString::fromUtf8(diskBytes);
        QVariantMap availability;
        availability.insert(QStringLiteral("path"), path);
        availability.insert(QStringLiteral("available"), true);
        availability.insert(QStringLiteral("reason"), QString());
        shell_->setProperty("vedit.diagnostics.analysisAvailability", availability);
        controller_->setDocumentAnalysisAvailability(path, true, QString());
        ++analyzeCalls_;
        publishWatchStats();
        // AC-3.1/AC-5.1: publish the analysis closure and hand the stable session
        // set to the controller BEFORE the analysis, so the switch that triggered
        // this call finds the session it needs already covered.
        publishRealtimeSets(path);
        controller_->analyzeFile(path, text, disk != text);
        publishRealtimeSets(path);
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    diagnostics::DiagnosticsController *controller_ = nullptr;
    QTimer *tick_ = nullptr;
    QTimer *debounce_ = nullptr;
    QString lastPath_;
    /// O(1) document change counter; -1 when the widget has no document.
    qint64 lastRevision_ = -1;
    /// Set by `reanalyzeNow()`: the next tick treats the same path as changed
    /// once (the pre-cap `lastText_.clear()` contract), then clears this.
    bool invalidateNextChange_ = false;
    /// The last file the cap skipped (the one-line notice is emitted per file).
    QString disabledPath_;
    /// G1-a: the golden project file set (cleanPath + canonical-directory
    /// spellings) and the revision it was built from. Empty = this lane has no
    /// project file set (single-file mode), where the membership rule does not
    /// apply; the module tree is gated on a filelist, so no closure exists there
    /// either and the realtime set stays `{path}`.
    diagnostics::realtime_scope::GoldenFileSet goldenFiles_;
    QVariant goldenRevision_;
    qulonglong catalogRevision_ = 0;
    bool hasFilelist_ = false;
    QStringList projectIndex_;
    QStringList shadowedFiles_;
    /// Open documents excluded from the session by the golden rule (published).
    int outsideFileCount_ = 0;
    /// One notice per truncation episode, not one per 400 ms tick.
    bool truncationNoted_ = false;
    quint64 bufferReads_ = 0;     ///< bufferText() calls made by this watcher
    quint64 bufferBytes_ = 0;     ///< their UTF-16 bytes (the copies it made)
    quint64 analyzeCalls_ = 0;    ///< analyzeFile() calls reached the controller
    quint64 tooLargeSkips_ = 0;   ///< analyze() calls suppressed by the cap
    quint64 tooLargeNotices_ = 0; ///< named one-line notices emitted
    QHash<QObject *, QString> documents_; ///< every open document -> its current path
};

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
/// Borrowed UTF-8 view of a QStringList for ONE ABI call. The ABI copies the
/// strings while the call runs; `items` only points into `storage`, so the
/// owner must outlive the call (never the job).
struct BorrowedStringList {
    std::vector<QByteArray> storage;
    std::vector<char *> items;
    VdStringList view{0, nullptr};

    explicit BorrowedStringList(const QStringList &values)
    {
        storage.reserve(static_cast<std::size_t>(values.size()));
        for (const QString &value : values) {
            storage.push_back(value.toUtf8());
        }
        items.reserve(storage.size());
        for (QByteArray &value : storage) {
            items.push_back(value.data());
        }
        view.count = items.size();
        view.items = items.empty() ? nullptr : items.data();
    }

    const VdStringList *pointer() const
    {
        return items.empty() ? nullptr : &view;
    }
};

/// The panel's project-expansion INPUTS, published by the project lane as the
/// shell property `vedit.project.inputs` (root, imported filelists, variables
/// table, settings store path).
///
/// RM-2: the diagnostics file set and the project panel used to expand the
/// same root with DIFFERENT inputs - this lane passed `nullptr, nullptr`, so a
/// project's imported `.f` lists and the tools page's `-I`/`-D` never reached
/// lint/LSP, and the panel and the analysis disagreed about the file set. The
/// inputs are re-read from the shell-property seam (the same decoupling the
/// toolchain path uses) instead of linking the project lane, so this lane still
/// builds standalone. A property for a DIFFERENT root (another project still
/// loading) contributes nothing: the fallback settings path is used and the
/// imported lists stay empty - a previous project's inputs are never carried.
struct ProjectExpansionInputs {
    QStringList filelists;
    QStringList variables;
    /// batch-20261115 AC-2.1/AC-2.5: the project lane's ONE answer to "is this
    /// folder a project (root + filelist)?". False means single-file mode: this
    /// lane expands NOTHING and therefore never builds a project file set by
    /// scanning the directory. A shell without the project lane publishes no
    /// inputs at all, and the historical expansion stays the default there.
    bool hasFilelist = true;
    // AC-1.5: the project panel's own editable `-I`/`-D` entries. They are part
    // of the input identity, so editing one re-expands this lane too — the lint /
    // LSP / rename child argv is composed from the expansion, and without this
    // comparison an include-only edit would leave it stale.
    QStringList projectIncludeDirs;
    QStringList projectDefines;
    QString settingsPath;
};

ProjectExpansionInputs projectExpansionInputs(const vedit::app::ApplicationShell &shell,
                                              const QString &root,
                                              const QString &fallbackSettingsPath)
{
    ProjectExpansionInputs inputs;
    inputs.settingsPath = fallbackSettingsPath;
    const QVariantMap published = shell.property("vedit.project.inputs").toMap();
    if (published.isEmpty() || published.value(QStringLiteral("root")).toString() != root) {
        return inputs;
    }
    inputs.hasFilelist = published.value(QStringLiteral("hasFilelist"), true).toBool();
    inputs.filelists = published.value(QStringLiteral("filelists")).toStringList();
    inputs.variables = published.value(QStringLiteral("variables")).toStringList();
    inputs.projectIncludeDirs =
        published.value(QStringLiteral("projectIncludeDirs")).toStringList();
    inputs.projectDefines = published.value(QStringLiteral("projectDefines")).toStringList();
    const QString settingsPath = published.value(QStringLiteral("settingsPath")).toString();
    if (!settingsPath.isEmpty()) {
        inputs.settingsPath = settingsPath;
    }
    return inputs;
}

/// L2.1 test seam: the poller's first-poll cadence (production value 50 ms).
/// The soft-timeout regression sets it to 1 ms (with
/// `VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS=0`) so the `expanding -> slow`
/// migration is observed immediately instead of after a real 300 s wait; the
/// production default and the 1 s slow backoff are unchanged.
int projectExpandPollIntervalMs()
{
    bool ok = false;
    const int overrideMs =
        qEnvironmentVariableIntValue("VEDIT_DIAGNOSTICS_EXPAND_POLL_INTERVAL_MS", &ok);
    return ok && overrideMs > 0 ? overrideMs : 50;
}

/// Ask the project model for the effective file set and hand it to the
/// controller. The poll is timer-driven, so `installPanels()` never blocks.
class ProjectFileSetPoller final : public QObject {
public:
    using ReadyCallback =
        std::function<void(const QStringList &, const QStringList &, const QStringList &)>;

    ProjectFileSetPoller(QObject *parent,
                         vedit::app::ApplicationShell *shell,
                         diagnostics::DiagnosticsController *controller,
                         const QString &projectRoot,
                         const ProjectExpansionInputs &inputs,
                         ReadyCallback ready)
        : QObject(parent)
        , controller_(controller)
        , shell_(shell)
        , projectRoot_(projectRoot)
        , ready_(std::move(ready))
        , timer_(new QTimer(this))
        , watchTimer_(new QTimer(this))
    {
        if (projectRoot_.isEmpty()) {
            publishState(QStringLiteral("no-filelist"));
            return;
        }
        service_ = vd_service_create();
        if (service_ == nullptr) {
            qWarning("diagnostics: cannot create the Rust service handle for the project model");
            return;
        }
        connect(timer_, &QTimer::timeout, this, [this]() { poll(); });
        // RM-9: the panel's expansion inputs are LIVE (Import Filelist…, the
        // variables table). A `.f` imported AFTER startup must join the analysis
        // set too - otherwise its files have no verdict and the tree's filelist
        // group cannot show them. Poll the published inputs (a cheap compare);
        // expand only on a CHANGE and never while another job is in flight.
        connect(watchTimer_, &QTimer::timeout, this, [this]() { watchInputs(); });
        watchTimer_->start(800);
        // G1-a: when the project lane is installed it owns the ONE expansion and
        // this lane only reads its answer. Only a shell WITHOUT a project lane
        // (a standalone diagnostics harness) falls through to the historical
        // self-expansion - there, this lane's expansion is the only one.
        if (!followPublishedFileSet()) {
            beginExpansion(inputs);
        }
    }

    ~ProjectFileSetPoller() override
    {
        if (service_ != nullptr) {
            vd_service_destroy(service_);
        }
    }

    const QStringList &files() const { return files_; }
    const QStringList &includeDirs() const { return includeDirs_; }
    const QStringList &defines() const { return defines_; }

    /// batch-20261116 L2.2: the user's Run (or a failed job) may ask for the
    /// expansion again. A job already in flight is left alone - one expansion at
    /// a time, never the old 2-3 concurrent slang runs. With a project lane
    /// installed (G1-a) there is no job of ours to retry, so the retry ASKS THE
    /// LANE THAT OWNS THE ONE EXPANSION to run it again (`vedit.project.model`,
    /// published by the project lane). G1-a review finding 1: merely republishing
    /// our copy of its state ("expanding") did not start anything, so a failed
    /// expansion could never be retried from this dock.
    void retryExpansion()
    {
        if (publishedAvailable()) {
            QObject *const model =
                shell_->property("vedit.project.model").value<QObject *>();
            if (model != nullptr && QMetaObject::invokeMethod(model, "refresh")) {
                // The project lane republishes its state synchronously from
                // `stateChanged`; adopt the new state in the same turn (pending
                // -> "expanding", an instant refusal -> "failed" again).
                followPublishedFileSet();
                return;
            }
            // A publisher from an older build has no model seam: fall back to
            // re-reading its current answer instead of pretending to expand.
            publishState(files_.isEmpty()
                             ? (shell_->property("vedit.project.hasFilelist").toBool()
                                    ? QStringLiteral("expanding")
                                    : QStringLiteral("no-filelist"))
                             : QStringLiteral("ready"));
            return;
        }
        if (job_ != 0 || service_ == nullptr) {
            return;
        }
        beginExpansion(inputs_);
    }

private:
    /// L2.1: the slow backoff after the soft timeout. The fast cadence comes
    /// from `projectExpandPollIntervalMs()` (50 ms, env-overridable for tests);
    /// the slow poll keeps reading the SAME job.
    static constexpr int kExpandSlowPollIntervalMs = 1000;

    /// L2.2: the poller's state, published for the Project Lint dock and for the
    /// tests. `phase` is stable (`no-filelist` / `expanding` / `slow` / `ready`
    /// / `failed`); the user-facing line is composed by
    /// `diagnostics::projectFileSetStateText()` from the same map, so the dock
    /// and the test can never disagree about the wording.
    void publishState(const QString &phase, const QString &message = QString())
    {
        if (shell_ == nullptr) {
            return;
        }
        QVariantMap state;
        state.insert(QStringLiteral("phase"), phase);
        state.insert(QStringLiteral("files"), files_.size());
        state.insert(QStringLiteral("ticks"), ticks_);
        // Keep the id after the terminal poll (`job_` becomes 0 there), so a
        // test can prove the READY state belongs to the SAME job that was
        // observed slow - the migration the old give-up discarded.
        state.insert(QStringLiteral("job"),
                     QVariant::fromValue<qulonglong>(job_ != 0 ? job_ : lastJob_));
        state.insert(QStringLiteral("pollIntervalMs"),
                     timer_ != nullptr ? timer_->interval() : 0);
        state.insert(QStringLiteral("softTimeoutMs"), projectExpandSoftTimeoutMs());
        state.insert(QStringLiteral("message"), message);
        state.insert(QStringLiteral("hasFilelist"), inputs_.hasFilelist);
        // G1-a: WHERE the set came from. `project-lane` = this lane read the ONE
        // expansion the project lane published and never expanded anything
        // itself; `self-expansion` = no project lane exists in this shell, so
        // this lane's own `vd_project_expand_v3` job is the only expansion there
        // is. Published so a test (and a verifier) can tell the two apart
        // without reading a log line.
        state.insert(QStringLiteral("source"),
                     publishedAvailable() ? QStringLiteral("project-lane")
                                          : QStringLiteral("self-expansion"));
        state.insert(QStringLiteral("elapsedMs"),
                     startedAtMs_ > 0 ? QDateTime::currentMSecsSinceEpoch() - startedAtMs_ : 0);
        shell_->setProperty("vedit.diagnostics.projectFilesState", state);
    }

    /// G1-a (contract §10): does a PROJECT LANE publish the file set into this
    /// shell? Its `vedit.project.filesRevision` is the marker - it is written
    /// from revision 1 on, and only by that lane.
    bool publishedAvailable() const
    {
        return shell_ != nullptr && shell_->property("vedit.project.filesRevision").isValid();
    }

    /// G1-a: read the ONE expansion the project lane published and adopt it.
    ///
    /// Returns false when no project lane exists, and the caller then keeps the
    /// historical self-expansion (a standalone diagnostics harness has no other
    /// source of truth; its expansion is the only one). This is the whole of the
    /// "expand once" rule: `vd_project_expand_v3` is reached from this lane only
    /// in that fallback.
    ///
    /// The revision is the change token. A revision that has not moved costs one
    /// property read and nothing else; a `filesPending` answer means an expansion
    /// is in flight, and the PREVIOUS set stays in force (the same contract the
    /// old in-lane job had: adopt only on arrival).
    bool followPublishedFileSet()
    {
        if (!publishedAvailable()) {
            return false;
        }
        const qulonglong revision =
            shell_->property("vedit.project.filesRevision").toULongLong();
        // The publisher may still describe ANOTHER root for one tick (the root
        // switch reaches both lanes through the same signal). Never adopt it.
        const QString publishedRoot = shell_->property("vedit.project.filesRoot").toString();
        if (publishedRoot != projectRoot_) {
            return true;
        }
        const bool hasFilelist = shell_->property("vedit.project.hasFilelist").toBool();
        const bool pending = shell_->property("vedit.project.filesPending").toBool();
        const QString error = shell_->property("vedit.project.filesError").toString();
        inputs_.hasFilelist = hasFilelist;
        // G1-a review finding 1: an expansion that FAILED is a state of its own
        // and must reach this dock with its reason. The project lane publishes
        // `vedit.project.filesError` and moves the revision on the failure, so
        // this branch is entered on the same tick a `ready` answer would be.
        // The previous answer is kept (a failed re-expansion must not silently
        // empty a set that is still valid), exactly like the historical in-lane
        // job: `poll()` published "failed" and left `files_` alone.
        if (!error.isEmpty()) {
            if (!publishedValid_ || revision != publishedRevision_) {
                publishedRevision_ = revision;
                publishedValid_ = true;
                publishState(QStringLiteral("failed"), error);
            }
            return true;
        }
        if (!hasFilelist) {
            const bool answerMoved = !publishedValid_ || !files_.isEmpty();
            publishedRevision_ = revision;
            publishedValid_ = true;
            if (answerMoved) {
                files_.clear();
                includeDirs_.clear();
                defines_.clear();
                publishState(QStringLiteral("no-filelist"));
                // AC-2.5: the answer is PUBLISHED, not merely held - a consumer
                // must be able to tell "this project has no file set" from "the
                // property was never set at all".
                if (ready_) {
                    ready_(QStringList{}, QStringList{}, QStringList{});
                } else {
                    controller_->setFileSet(QStringList{});
                }
            }
            return true;
        }
        if (pending) {
            // An expansion is running: name it, keep the previous answer. Not a
            // single file is re-expanded here - the project lane is doing it.
            if (files_.isEmpty()) {
                publishState(QStringLiteral("expanding"));
            }
            return true;
        }
        if (publishedValid_ && revision == publishedRevision_) {
            return true;
        }
        const QStringList files = shell_->property("vedit.project.files").toStringList();
        const QStringList includeDirs =
            shell_->property("vedit.project.includeDirs").toStringList();
        const QStringList defines = shell_->property("vedit.project.defines").toStringList();
        publishedRevision_ = revision;
        publishedValid_ = true;
        files_ = files;
        includeDirs_ = includeDirs;
        defines_ = defines;
        publishState(QStringLiteral("ready"));
        if (ready_) {
            ready_(files_, includeDirs_, defines_);
        } else {
            controller_->setFileSet(files);
        }
        return true;
    }

    /// RM-9: one expansion with exactly `inputs`. Re-entrant by design: the
    /// RUNNING job's completion calls it again when the panel's inputs moved.
    void beginExpansion(const ProjectExpansionInputs &inputs)
    {
        inputs_ = inputs;
        settingsPath_ = inputs.settingsPath;
        if (service_ == nullptr || job_ != 0) {
            return; // the running job's completion re-checks the inputs
        }
        if (!inputs.hasFilelist) {
            // AC-2.1/AC-2.5: no filelist, so there is no project file set to
            // build. Specifically NOT "expand the root and let the scan decide":
            // a folder nobody declared a project must not be walked, and the
            // analysis stays on the open file (the active-file watcher).
            files_.clear();
            includeDirs_.clear();
            defines_.clear();
            startedAtMs_ = 0;
            lastJob_ = 0;
            if (ready_) {
                ready_(files_, includeDirs_, defines_);
            } else if (controller_ != nullptr) {
                controller_->setFileSet(files_);
            }
            // L2.2: the dock's persistent reason names exactly this state.
            publishState(QStringLiteral("no-filelist"));
            return;
        }
        const QByteArray root = projectRoot_.toUtf8();
        const QByteArray settingsPath = inputs.settingsPath.toUtf8();
        const BorrowedStringList filelists(inputs.filelists);
        const BorrowedStringList variables(inputs.variables);
        const BorrowedStringList projectIncludeDirs(inputs.projectIncludeDirs);
        const BorrowedStringList projectDefines(inputs.projectDefines);
        // R2/R3: the file set must be the C7 project model - the `.f` expansion
        // UNION a recursive scan of the folder - not the legacy filelist-only
        // `vd_project_parse`. Measured: with the legacy call a folder of .sv
        // files and no `.f` produced ZERO files, so "Lint Project" had nothing to
        // lint and reported "No project file set" - the feature was dead for
        // exactly the projects a new user opens. Since A61.2 the expansion itself
        // is gated on an IMPORTED filelist (AC-2.5), so the scan below only runs
        // for a folder the user declared a project.
        //
        // RM-2: the full project model with the PANEL's inputs (imported `.f`
        // lists, variables table, settings store) -> the file set, the `-I`/`-D`
        // and the discovered rules path are exactly the panel's: one project
        // model, not two.
        // AC-1.5: the ADDITIVE v3 entry point (`vd_project_expand_v3`) also
        // carries the project panel's own `-I`/`-D` entries. They are part of the
        // input identity compared above, so an edit reaches this lane's
        // expansion — and from it the lint/LSP/rename argv — without a second
        // source of truth and without a restart.
        job_ = vd_project_expand_v3(service_, root.constData(), filelists.pointer(),
                                    settingsPath.isEmpty() ? nullptr : settingsPath.constData(),
                                    variables.pointer(), projectIncludeDirs.pointer(),
                                    projectDefines.pointer());
        std::printf("PROJECT-EXPAND-INPUTS root='%s' filelists=%d variables=%d "
                    "project-include=%d project-defines=%d settings='%s'\n",
                    qPrintable(projectRoot_), static_cast<int>(inputs.filelists.size()),
                    static_cast<int>(inputs.variables.size()),
                    static_cast<int>(inputs.projectIncludeDirs.size()),
                    static_cast<int>(inputs.projectDefines.size()),
                    settingsPath.isEmpty() ? "<none>" : qPrintable(inputs.settingsPath));
        std::fflush(stdout);
        if (job_ == 0) {
            qWarning("diagnostics: project expansion was rejected: %s",
                     qPrintable(serviceError()));
            lastJob_ = 0;
            publishState(QStringLiteral("failed"), serviceError());
            return;
        }
        lastJob_ = job_;
        ticks_ = 0;
        slowNoted_ = false;
        startedAtMs_ = QDateTime::currentMSecsSinceEpoch();
        timer_->setInterval(projectExpandPollIntervalMs());
        timer_->start();
        publishState(QStringLiteral("expanding"));
    }

    /// The panel's inputs changed (a `.f` was imported/removed, a variable was
    /// set): follow it. No-op while a job runs - its completion re-checks.
    void watchInputs()
    {
        if (shell_ == nullptr) {
            return;
        }
        // G1-a: with a project lane installed this lane never expands - it only
        // follows the revision of the ONE published answer.
        if (followPublishedFileSet()) {
            return;
        }
        const ProjectExpansionInputs next =
            projectExpansionInputs(*shell_, projectRoot_, settingsPath_);
        if (next.hasFilelist == inputs_.hasFilelist
            && next.filelists == inputs_.filelists && next.variables == inputs_.variables
            && next.projectIncludeDirs == inputs_.projectIncludeDirs
            && next.projectDefines == inputs_.projectDefines) {
            return;
        }
        beginExpansion(next);
    }

    QString serviceError() const
    {
        VdError error{};
        if (service_ != nullptr) {
            vd_service_error(service_, &error);
        }
        return QString::fromUtf8(error.message);
    }

    void poll()
    {
        if (job_ == 0 || service_ == nullptr) {
            timer_->stop();
            return;
        }
        VdProjectInfo info{};
        const int status = vd_project_poll(service_, job_, &info);
        if (status == VD_STATUS_PENDING) {
            ++ticks_;
            // batch-20261116 L2.1: the old code DROPPED the job after 200 ticks
            // (10 s), so a large project was an empty set for the rest of the
            // session (project LINT permanently unavailable). At the soft
            // timeout the poller now keeps polling the SAME job at a 1 s backoff
            // and NAMES the state, so a slow expansion still delivers and the
            // dock can say why it is empty meanwhile.
            const qint64 elapsedMs = startedAtMs_ > 0
                ? QDateTime::currentMSecsSinceEpoch() - startedAtMs_
                : 0;
            if (!slowNoted_ && elapsedMs > projectExpandSoftTimeoutMs()) {
                slowNoted_ = true;
                timer_->setInterval(kExpandSlowPollIntervalMs);
                qWarning("diagnostics: project expansion is still running after %lld ms "
                         "(soft timeout %d ms); polling the same job at %d ms",
                         static_cast<long long>(elapsedMs), projectExpandSoftTimeoutMs(),
                         kExpandSlowPollIntervalMs);
                publishState(QStringLiteral("slow"));
            }
            return;
        }
        timer_->stop();
        job_ = 0;
        if (status != VD_STATUS_READY) {
            const QString error = serviceError();
            qWarning("diagnostics: the project job failed: %s", qPrintable(error));
            publishState(QStringLiteral("failed"), error);
            return;
        }
        QStringList files;
        for (size_t index = 0; index < info.files.count; ++index) {
            if (info.files.items[index] != nullptr) {
                files.append(QString::fromUtf8(info.files.items[index]));
            }
        }
        QStringList includeDirs;
        for (size_t index = 0; index < info.include_dirs.count; ++index) {
            if (info.include_dirs.items[index] != nullptr) {
                includeDirs.append(QString::fromUtf8(info.include_dirs.items[index]));
            }
        }
        QStringList defines;
        for (size_t index = 0; index < info.defines.count; ++index) {
            if (info.defines.items[index] != nullptr) {
                defines.append(QString::fromUtf8(info.defines.items[index]));
            }
        }
        // UX batch 2 / U6: the project loader DISCOVERS the `.rules.verible_lint`
        // (`VdProjectInfo.rules_path`). It is the lint page's THIRD rules-
        // precedence level (hand-typed rules_config > tools.verible_rules >
        // discovered > no flag), so publish it where the lint backend composes
        // the batch options. Empty = none was found (the normal case).
        const QString discoveredRulesPath =
            info.rules_path != nullptr ? QString::fromUtf8(info.rules_path) : QString();
        vd_project_info_free(&info);
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS)
        settings::setCurrentDiscoveredRulesPath(discoveredRulesPath);
#endif
        files_ = files;
        includeDirs_ = includeDirs;
        defines_ = defines;
        // L2.2: the dock reads this map for its reason; `ready` also carries the
        // exact file count a large project finally produced.
        publishState(QStringLiteral("ready"));
        if (ready_) {
            // The sink owns the controller (it re-pins the request and starts the
            // ONE session for this file set). Calling setFileSet() here as well
            // reparsed twice per Open Folder - two LSP sessions for one root.
            ready_(files_, includeDirs_, defines_);
        } else {
            controller_->setFileSet(files);
        }
        // RM-9: the inputs may have moved WHILE this expansion ran (a `.f`
        // imported during startup). Follow them now instead of ignoring them.
        if (shell_ != nullptr) {
            const ProjectExpansionInputs next =
                projectExpansionInputs(*shell_, projectRoot_, settingsPath_);
            if (next.hasFilelist != inputs_.hasFilelist
                || next.filelists != inputs_.filelists || next.variables != inputs_.variables
                || next.projectIncludeDirs != inputs_.projectIncludeDirs
                || next.projectDefines != inputs_.projectDefines) {
                beginExpansion(next);
            }
        }
    }

    diagnostics::DiagnosticsController *controller_ = nullptr;
    vedit::app::ApplicationShell *shell_ = nullptr;
    QString projectRoot_;
    ReadyCallback ready_;
    QTimer *timer_ = nullptr;
    QTimer *watchTimer_ = nullptr;
    VdService *service_ = nullptr;
    quint64 job_ = 0;
    /// L2.1/M1: the last started job id, kept after the terminal poll so the
    /// `ready`/`failed` state still names WHICH job produced it (the migration
    /// the old 10 s give-up discarded cannot be observed without this).
    quint64 lastJob_ = 0;
    int ticks_ = 0;
    qint64 startedAtMs_ = 0;
    bool slowNoted_ = false;
    ProjectExpansionInputs inputs_;
    QString settingsPath_;
    QStringList files_;
    QStringList includeDirs_;
    QStringList defines_;
    /// G1-a: the project lane's published revision this lane last adopted (and
    /// whether one was adopted at all). No job id: with a publisher there is no
    /// job of ours to name.
    qulonglong publishedRevision_ = 0;
    bool publishedValid_ = false;
};

/// T11: read <stateDir>/settings.toml through the SAME C ABI the Preferences
/// dialog writes with, and report the key/value map when the file changes. The
/// mtime poll makes the online switch independent of another lane's class; the
/// read itself is the ABI's asynchronous job (no disk IO on the GUI stack).
class SettingsWatcher final : public QObject {
public:
    /// T9/AC-T9.1: the store's warnings travel WITH the values. They used to be
    /// dropped here, so a store the Rust loader rejected (a bare dotted key) or
    /// that named a value it refused (`emacs`) silently meant defaults.
    using ChangedCallback =
        std::function<void(const QHash<QString, QString> &, const QStringList &)>;

    SettingsWatcher(QObject *parent, QString path, ChangedCallback changed)
        : QObject(parent)
        , path_(std::move(path))
        , changed_(std::move(changed))
    {
        service_ = vd_service_create();
        timer_ = new QTimer(this);
        timer_->setInterval(300);
        connect(timer_, &QTimer::timeout, this, [this]() { tick(); });
        timer_->start();
        reload();
    }

    void reload()
    {
        if (service_ == nullptr) {
            return;
        }
        if (!QFileInfo::exists(path_)) {
            // The store may not exist yet (defaults apply); remember the state
            // so creating it later is noticed.
            lastStamp_ = QDateTime();
            changed_({}, {});
            return;
        }
        lastStamp_ = QFileInfo(path_).lastModified();
        const QByteArray path = path_.toUtf8();
        job_ = vd_settings_load(service_, path.constData());
        if (job_ == 0) {
            qWarning("diagnostics: settings read was rejected: %s", qPrintable(lastError()));
            return;
        }
        pollJob();
    }

private:
    QString lastError() const
    {
        VdError error{};
        if (service_ != nullptr) {
            vd_service_error(service_, &error);
        }
        return QString::fromUtf8(error.message);
    }

    void tick()
    {
        const QFileInfo info(path_);
        const QDateTime stamp = info.exists() ? info.lastModified() : QDateTime();
        if (job_ == 0 && stamp != lastStamp_) {
            lastStamp_ = stamp;
            reload();
        }
        if (job_ != 0) {
            pollJob();
        }
    }

    void pollJob()
    {
        if (job_ == 0 || service_ == nullptr) {
            return;
        }
        VdSettings settings{};
        const int status = vd_settings_poll(service_, job_, &settings);
        if (status == VD_STATUS_PENDING) {
            return;
        }
        job_ = 0;
        if (status != VD_STATUS_READY) {
            qWarning("diagnostics: settings read failed: %s", qPrintable(lastError()));
            return;
        }
        QHash<QString, QString> values;
        for (size_t index = 0; index < settings.count; ++index) {
            const VdSetting &item = settings.items[index];
            if (item.key != nullptr && item.value != nullptr) {
                values.insert(QString::fromUtf8(item.key), QString::fromUtf8(item.value));
            }
        }
        // AC-T9.1: copy the warnings BEFORE the ABI struct is freed; the strings
        // are owned by `settings`. Nothing is filtered here - the store's own
        // sentence is what the user needs to fix the file.
        QStringList warnings;
        warnings.reserve(static_cast<int>(settings.warnings.count));
        for (size_t index = 0; index < settings.warnings.count; ++index) {
            if (settings.warnings.items[index] != nullptr) {
                warnings.append(QString::fromUtf8(settings.warnings.items[index]));
            }
        }
        vd_settings_free(&settings);
        if (changed_) {
            changed_(values, warnings);
        }
    }

    QString path_;
    ChangedCallback changed_;
    VdService *service_ = nullptr;
    QTimer *timer_ = nullptr;
    quint64 job_ = 0;
    QDateTime lastStamp_;
};
#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

} // namespace

#if defined(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)
namespace {

/// T11: the shell wiring of the backend switch, capability gating and the
/// actions that route to the selected backend. It owns no backend: the
/// controller owns the two seams and this class selects/restarts them.

#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
// AC-2.2 (batch 2): the references list. It renders exactly the rows the editor
// lane built (file:line:column + the source line) and never invents a location;
// double-clicking a row jumps through the ONE application-wide jump seam. The
// empty answer is a visible empty state, not a silent no-op.
class ReferencesDialog final : public QDialog {
public:
    explicit ReferencesDialog(QWidget *parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("veditReferencesDialog"));
        setWindowTitle(QCoreApplication::translate("VEdit", "References"));
        setModal(false);
        resize(760, 420);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(4);
        header_ = new QLabel(this);
        header_->setObjectName(QStringLiteral("veditReferencesHeader"));
        layout->addWidget(header_);
        tree_ = new QTreeWidget(this);
        tree_->setObjectName(QStringLiteral("veditReferencesTree"));
        tree_->setColumnCount(2);
        tree_->setHeaderLabels({QCoreApplication::translate("VEdit", "Location"),
                                QCoreApplication::translate("VEdit", "Snippet")});
        tree_->setRootIsDecorated(false);
        tree_->setAlternatingRowColors(true);
        tree_->setUniformRowHeights(true);
        tree_->header()->setStretchLastSection(true);
        layout->addWidget(tree_, 1);
        empty_ = new QLabel(this);
        empty_->setObjectName(QStringLiteral("veditReferencesEmpty"));
        empty_->setWordWrap(true);
        layout->addWidget(empty_);
    }

    void setAnswer(const QString &symbol, const QList<vedit::editor::ReferenceRow> &rows)
    {
        rows_ = rows;
        tree_->clear();
        const QString subject = symbol.isEmpty()
                                    ? QCoreApplication::translate("VEdit", "the symbol under the cursor")
                                    : symbol;
        header_->setText(QCoreApplication::translate("VEdit", "%1 reference(s) for %2")
                             .arg(rows.size())
                             .arg(subject));
        empty_->setText(QCoreApplication::translate(
                            "VEdit", "No references found for %1 — the backend returned no location.")
                            .arg(subject));
        empty_->setVisible(rows.isEmpty());
        tree_->setVisible(!rows.isEmpty());
        for (int index = 0; index < rows.size(); ++index) {
            auto *item = new QTreeWidgetItem(tree_);
            item->setText(0, rows.at(index).location());
            item->setText(1, rows.at(index).snippet);
            item->setData(0, Qt::UserRole, index);
        }
        if (!rows.isEmpty()) {
            tree_->resizeColumnToContents(0);
            tree_->setCurrentItem(tree_->topLevelItem(0));
        }
    }

    QTreeWidget *treeWidget() const { return tree_; }
    QLabel *emptyState() const { return empty_; }
    QString headerText() const { return header_->text(); }
    QList<vedit::editor::ReferenceRow> rows() const { return rows_; }

private:
    QLabel *header_ = nullptr;
    QLabel *empty_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QList<vedit::editor::ReferenceRow> rows_;
};
#endif // VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR

class BackendWiring final : public QObject {
public:
    BackendWiring(QObject *parent,
                  vedit::app::ApplicationShell *shell,
                  diagnostics::DiagnosticsController *controller,
                  diagnostics::LspSessionBackend *lsp,
                  diagnostics::SlangBackend *lint,
                  diagnostics::ToolchainReport toolchain,
                  QString workspaceRoot,
                  QString settingsPath,
                  std::function<void()> restartAnalysis)
        : QObject(parent)
        , shell_(shell)
        , controller_(controller)
        , state_(lsp, lint)
        , toolchain_(std::move(toolchain))
        , workspaceRoot_(std::move(workspaceRoot))
        , settingsPath_(std::move(settingsPath))
        , restartAnalysis_(std::move(restartAnalysis))
    {
        // T29/AC-8.1: the resolved toolchain is already known here. Prime the
        // capability state with it BEFORE the settings watcher can apply
        // anything, so a first launch with no `settings.toml` answers the
        // formatting gate from the real probe (the old empty report claimed a
        // present formatter was missing until apply() ran).
        state_.seedToolchain(toolchain_);
        // AC-1.2: the visible status line. A permanent status-bar badge always
        // names the selected backend (and the lint engine), and is refreshed when
        // the session publishes its live capability table.
        badge_ = new QLabel();
        badge_->setObjectName(QStringLiteral("diagnosticsBackendStatus"));
        badge_->setToolTip(QCoreApplication::translate("VEdit", "Selected LSP backend and CLI lint engine"));
        if (shell_->statusBar() != nullptr) {
            shell_->statusBar()->addPermanentWidget(badge_);
        }

        liveTimer_ = new QTimer(this);
        liveTimer_->setInterval(400);
        connect(liveTimer_, &QTimer::timeout, this, [this]() { refreshLiveState(); });
        liveTimer_->start();

        navTimer_ = new QTimer(this);
        navTimer_->setInterval(60);
        connect(navTimer_, &QTimer::timeout, this, [this]() { pollNavigation(); });

        passTimer_ = new QTimer(this);
        passTimer_->setInterval(300);
        connect(passTimer_, &QTimer::timeout, this, [this]() {
            // batch-20261115 AC-2.4: the project pass is MANUAL ONLY. This timer
            // advances a pass the user started and presents its result; it never
            // starts one.
            controller_->refresh(); // advances the project lint pass (T8)
            reactToFinishedPass();
        });
        passTimer_->start();

        // item 12 / R3: the presentation policy. `project.lint` is the only
        // trigger (AC-2.4), so the modal dialog is the explicit run's surface;
        // the clickable status-bar notice remains for a pass an automatic path
        // in another composition may still request.
        lintNotice_ = new diagnostics::ProjectLintNotice(shell_, shell_->statusBar(), this);

        // batch-20261115 AC-2.4: the save-triggered incremental project pass is
        // GONE. A save no longer runs any project-wide lint: the whole-project
        // analysis is the explicit `Project > Lint Project` command only. The
        // single-file realtime verdict (the active-file watcher below) is what
        // follows an edit, so nothing disappears from the editor.
        autofix_.setLintBinary(toolchain_.veribleLint.path);

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        // AC-5.1/5.6/5.8 (card 2): the waiver jobs are polled from HERE, on the
        // caller's event loop, so no child/disk work ever runs on the GUI stack.
        waiverTimer_ = new QTimer(this);
        waiverTimer_->setInterval(120);
        connect(waiverTimer_, &QTimer::timeout, this, [this]() { pollWaiverJobs(); });
        // The panel can be inserted after this wiring (a restored dock builds its
        // content lazily), so the context menu is attached on first sight and
        // re-checked by the live poll.
        attachWaiverUi();
        watcher_ = new SettingsWatcher(this, settingsPath_,
                                       [this](const QHash<QString, QString> &values,
                                              const QStringList &warnings) {
                                           applySettings(values, warnings);
                                       });
#else
        (void)settingsPath;
#endif
        // AC-N3: the badge has its line the moment the wiring exists (a first
        // launch with no settings file included), so `BADGE-TEXT` is emit at
        // startup instead of waiting for the first settings change.
        publishBadge();
    }

    void setProjectFiles(const QStringList &files, const QStringList &includeDirs,
                         const QStringList &defines)
    {
        // RM-11: membership is asked of the registry, never re-decided here.
        // The project expansion already answers positively (its own scan uses
        // the Rust registry), so this is the C++ half of the SAME rule: a path
        // that is not a source language cannot become a project file even if a
        // future input path forgets to filter.
        QStringList members;
        members.reserve(files.size());
        for (const QString &file : files) {
            if (project::LanguageRegistry::instance().supports(file,
                                                               project::Capability::Source)) {
                members.append(file);
            }
        }
        projectFiles_ = members;
        projectIncludeDirs_ = includeDirs;
        projectDefines_ = defines;
    }

    /// Register the gated actions; `installDiagnostics` creates the commands so
    /// src/app never learns their names.
    void bindActions(QAction *definition, QAction *references, QAction *rename,
                     QAction *format, QAction *hover)
    {
        definitionAction_ = definition;
        referencesAction_ = references;
        renameAction_ = rename;
        formatAction_ = format;
        hoverAction_ = hover;
        refreshLiveState();
    }

    /// The capability table's gating decisions, published for the editor lane
    /// (`vedit.diagnostics.gates` on the shell) and used to grey the actions.
    void publishGates()
    {
        const auto capabilities = state_.capabilities();
        const QStringList actions = {
            QStringLiteral("definition"), QStringLiteral("references"),
            QStringLiteral("rename"), QStringLiteral("hover"),
            QStringLiteral("completion"), QStringLiteral("inlayHint"),
            QStringLiteral("codeAction"), QStringLiteral("formatting"),
            QStringLiteral("signatureHelp"),
        };
        QVariantMap gates;
        for (const QString &action : actions) {
            const CapabilityDecision decision = state_.gate(action);
            QVariantMap row;
            row.insert(QStringLiteral("enabled"), decision.enabled);
            row.insert(QStringLiteral("known"), decision.known);
            row.insert(QStringLiteral("source"), decision.source);
            row.insert(QStringLiteral("reason"), decision.reason);
            gates.insert(action, row);
        }
        shell_->setProperty("vedit.diagnostics.gates", gates);
        QStringList rows;
        for (const diagnostics::CapabilityRow &row : capabilities.rows) {
            rows.append(QStringLiteral("%1=%2").arg(row.action).arg(row.supported));
        }
        QVariantMap table;
        table.insert(QStringLiteral("backend"), capabilities.backend);
        table.insert(QStringLiteral("basis"), capabilities.basis);
        table.insert(QStringLiteral("source"), capabilities.source);
        table.insert(QStringLiteral("note"), capabilities.note);
        table.insert(QStringLiteral("rows"), rows.join(QLatin1Char(' ')));
        shell_->setProperty("vedit.diagnostics.capabilities", table);
    }

    /// AC-2.5 echo + AC-1.2/1.6: one machine line per resolved backend/engine.
    void dumpStatus(const char *tag)
    {
        const auto capabilities = state_.capabilities();
        std::printf("BACKEND-%s %s basis=%s argv='%s'\n", tag,
                    qPrintable(state_.statusLine()),
                    capabilities.basis.isEmpty() ? "unknown" : qPrintable(capabilities.basis),
                    qPrintable(state_.commandLine()));
        std::printf("BACKEND-AUX %s\n", qPrintable(state_.toolchain().auxiliaryStatusLine()));
        std::fflush(stdout);
    }

    /// The active file's language id, for the per-language badge route (AC-3.5).
    /// The registry is authoritative; the VHDL extensions are recognised
    /// explicitly until the VHDL lane registers its language there, so a `.vhd`
    /// file can never be answered by the Verilog backend ("never verible/slang
    /// for VHDL").
    QString activeLanguageId() const
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        KTextEditor::View *view = activeView();
        if (view == nullptr || view->document() == nullptr) {
            return QString();
        }
        const QString path = view->document()->url().toLocalFile();
        if (path.isEmpty()) {
            return QString();
        }
        const project::LanguageSpec *spec =
            project::LanguageRegistry::instance().languageFor(path);
        if (spec != nullptr) {
            return spec->id;
        }
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QStringLiteral("vhd") || suffix == QStringLiteral("vhdl")) {
            return QStringLiteral("vhdl");
        }
        return QString();
#else
        return QString();
#endif
    }

    /// The badge facts as one map (T14 renders; T9 owns the values).
    QVariantMap badgeMap(const BadgeState &badge) const
    {
        QVariantMap map;
        map.insert(QStringLiteral("requestedLsp"), badge.lsp.requested);
        map.insert(QStringLiteral("effectiveLsp"), badge.lsp.effective);
        map.insert(QStringLiteral("lspPath"), badge.lsp.path);
        map.insert(QStringLiteral("lspOk"), badge.lsp.applied);
        map.insert(QStringLiteral("lspReason"), badgeReasonCodeName(badge.lsp.reason));
        map.insert(QStringLiteral("lspReasonMessage"), badge.lsp.reasonMessage);
        map.insert(QStringLiteral("requestedLint"), badge.lint.requested);
        map.insert(QStringLiteral("effectiveLint"), badge.lint.effective);
        map.insert(QStringLiteral("lintPath"), badge.lint.path);
        map.insert(QStringLiteral("lintOk"), badge.lint.applied);
        map.insert(QStringLiteral("lintReason"), badgeReasonCodeName(badge.lint.reason));
        map.insert(QStringLiteral("lintReasonMessage"), badge.lint.reasonMessage);
        map.insert(QStringLiteral("basis"), badge.basis);
        map.insert(QStringLiteral("language"), badge.language);
        map.insert(QStringLiteral("languageBackend"), badge.languageBackend);
        map.insert(QStringLiteral("warnings"), badge.warnings);
        map.insert(QStringLiteral("summary"), badge.summary());
        return map;
    }

    /// AC-3.2/3.3/3.4 (card 2): the ONE text the badge's QLabel shows.
    ///
    /// `diagnostics::badgeText()` is the renderer of the SELECTION line
    /// (`requested→effective · lint=… · basis=… · reason=…`). The wiring appends
    /// the settings-warning marker when the store reported problems, because
    /// AC-N4 demands that "the settings have a problem" be visible ON THE BADGE
    /// (a count, not only a tooltip): a store that replaced `emacs` with
    /// `verible` keeps `reason=ok` for the selection, and without the marker the
    /// user would never learn the value was rewritten.
    QString renderBadgeText(const BadgeState &badge) const
    {
        QString text = diagnostics::badgeText(badge);
        if (!badge.warnings.isEmpty()) {
            text += QStringLiteral(" · ⚠ %1").arg(badge.warnings.size());
        }
        return text;
    }

    /// The badge's tooltip: the refusal sentences plus every store warning
    /// verbatim (the hover text a user sees without leaving the status bar).
    QString renderBadgeToolTip(const BadgeState &badge) const
    {
        QStringList lines;
        lines.append(QCoreApplication::translate("VEdit", "Selected LSP backend and CLI lint engine"));
        if (!badge.lsp.reasonMessage.isEmpty()) {
            lines.append(badge.lsp.reasonMessage);
        }
        if (!badge.lint.reasonMessage.isEmpty()) {
            lines.append(badge.lint.reasonMessage);
        }
        if (!badge.warnings.isEmpty()) {
            lines.append(QCoreApplication::translate("VEdit", "Settings problems:"));
            lines.append(badge.warnings.join(QLatin1Char('\n')));
        }
        return lines.join(QLatin1Char('\n'));
    }

    /// Publish the badge facts; the machine lines are printed only when the
    /// facts/rendered text CHANGE, so the 400 ms poll cannot flood the log. The
    /// value is state, not a timer: it is recomputed from the real selection on
    /// every poll.
    ///
    /// E1–E3 (AC-N3): `BADGE-TEXT` is the text the REAL QLabel holds
    /// (`badge_->text()`), read back AFTER `setText()` - never a second
    /// rendering of the same data. `VEDIT_BADGE_TRACE` adds a 1 Hz re-read of the
    /// same widget text so a probe can show the text is still there after the
    /// old 8 s message would have expired.
    void publishBadge()
    {
        const BadgeState badge = state_.badge(activeLanguageId());
        shell_->setProperty("vedit.diagnostics.badge", badgeMap(badge));
        const QString line = QStringLiteral(
                                 "BADGE requested-lsp=%1 effective-lsp=%2 lsp-ok=%3 "
                                 "lsp-reason=%4 requested-lint=%5 effective-lint=%6 lint-ok=%7 "
                                 "lint-reason=%8 basis=%9 language=%10 language-backend=%11 "
                                 "warnings=%12 lsp-message='%13' lint-message='%14'")
                                 .arg(badge.lsp.requested, badge.lsp.effective,
                                      badge.lsp.applied ? QStringLiteral("1") : QStringLiteral("0"),
                                      badgeReasonCodeName(badge.lsp.reason),
                                      badge.lint.requested, badge.lint.effective,
                                      badge.lint.applied ? QStringLiteral("1") : QStringLiteral("0"),
                                      badgeReasonCodeName(badge.lint.reason), badge.basis,
                                      badge.language,
                                      badge.languageBackend.isEmpty()
                                          ? QStringLiteral("<none>")
                                          : badge.languageBackend,
                                      QString::number(badge.warnings.size()),
                                      badge.lsp.reasonMessage, badge.lint.reasonMessage);
        if (line != lastBadgeLine_) {
            lastBadgeLine_ = line;
            std::printf("%s\n", qPrintable(line));
            std::fflush(stdout);
        }
        if (badge_ == nullptr) {
            return;
        }
        // The ONE renderer: the widget shows `badgeText()` (+ the warning marker).
        badge_->setText(renderBadgeText(badge));
        QString tooltip = renderBadgeToolTip(badge);
        // batch-20261118 AC-2/AC-3: a failed/degraded session and its restart
        // count are visible on the badge, so the status line can no longer read
        // "basis=live" while the server is dead. The session message already
        // carries the child's stderr tail.
        if (controller_ != nullptr) {
            const diagnostics::SessionSnapshot session = controller_->session();
            if (session.state == diagnostics::BackendState::Failed) {
                tooltip += QCoreApplication::translate(
                               "VEdit", "\nSession FAILED (restarts %1): %2")
                               .arg(session.restarts)
                               .arg(session.message);
            } else if (session.restarts > 0) {
                tooltip += QCoreApplication::translate("VEdit", "\nSession restarts: %1")
                               .arg(session.restarts);
            }
        }
        badge_->setToolTip(tooltip);
        // The machine line is the WIDGET's own text (AC-N3 hard requirement).
        const QString rendered = badge_->text();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool changed = rendered != lastBadgeText_;
        lastBadgeText_ = rendered;
        const bool traced = qEnvironmentVariableIsSet("VEDIT_BADGE_TRACE");
        if (changed || (traced && now - lastBadgeTextPrintMs_ >= 1000)) {
            lastBadgeTextPrintMs_ = now;
            std::printf("BADGE-TEXT '%s'\n", qPrintable(rendered));
            std::fflush(stdout);
        }
    }

    /// Shared tail of a settings apply: repoint the controller/autofix at the
    /// selection that is really in force, drop the previous engine's rows and
    /// (re)start the analysis. Runs for a FULL switch and for a PARTIAL one
    /// (AC-T9.2): the axis that applied must not stay unconfigured because the
    /// other axis failed.
    void adoptAppliedSelection()
    {
        controller_->setConfiguration(state_.lspServerPath(), workspaceRoot_,
                                      state_.rulesPath());
        controller_->setToolchainReport(state_.toolchain());
        // item 9 / MEDIUM b: the probe must use the CURRENT engine and rules
        // config. Both used to be captured once (or never): a settings change
        // left the hover measuring with the old binary/rules.
        autofix_.setLintBinary(state_.toolchain().veribleLint.path);
        autofix_.setRulesConfig(state_.rulesPath());
        // Keep the module tree's view in step with the SAME report: a settings
        // override of `tools.slang` changes what the next elaboration must use.
        shell_->setProperty("vedit.diagnostics.toolchainSlang", state_.toolchain().slang.path);
        // N3-p: stop the OLD engine's in-flight pass BEFORE the store is
        // cleared. clearProjectDiagnostics() drops the rows but leaves
        // passRunning_/passJob_ alive, so the old job would keep running, write
        // rows the new configuration does not cover, and refuse the next
        // explicit Lint Project as "already running". followProjectRoot() already
        // cancels first; this is the settings/backend switch's missing half.
        if (controller_->projectPassRunning()) {
            controller_->cancelProjectPass();
        }
        // AC-1.2/1.6: clear the old rows BEFORE the new backend answers, so a
        // stale row can never be shown under the new backend's name. The project
        // pass rows/dots are cleared here; `state_.apply()` above already
        // stopped the old LSP server through selectBackend(), so the realtime
        // session MUST be restarted explicitly below - `setFileSet({})` no
        // longer re-parses (batch-20261116 B1) and `analyzeFile()` would reuse
        // the stopped session.
        controller_->clearProjectDiagnostics();
        controller_->setFileSet(QStringList{});
        controller_->restartRealtimeSession();
        if (restartAnalysis_) {
            restartAnalysis_();
        }
        if (controller_->fileSet().isEmpty() && !projectFiles_.isEmpty()) {
            controller_->setFileSet(projectFiles_);
        }
        publishGates();
        refreshLiveState();
    }

    bool applySettings(const QHash<QString, QString> &values, const QStringList &warnings)
    {
        DiagnosticsBackendSettings settings;
        settings.lspBackend = values.value(QString::fromLatin1(settings::keys::kLspBackend),
                                           settings.lspBackend);
        settings.lintEngine = values.value(QString::fromLatin1(settings::keys::kLintEngine),
                                           settings.lintEngine);
        settings.veribleLs = values.value(QString::fromLatin1(settings::keys::kVeribleLs));
        settings.slang = values.value(QString::fromLatin1(settings::keys::kSlang));
        settings.slangServer = values.value(QString::fromLatin1(settings::keys::kSlangServer));
        settings.veribleFormat = values.value(QString::fromLatin1(settings::keys::kVeribleFormat));
        settings.veribleRules = values.value(QString::fromLatin1(settings::keys::kVeribleRules));
        settings.includeDirs = splitList(values.value(QString::fromLatin1(settings::keys::kIncludeDirs)));
        settings.defines = splitList(values.value(QString::fromLatin1(settings::keys::kDefines)));

        // AC-T9.1: the store's own explanation travels with the values. Before
        // this, a store the loader rejected (bare dotted key) or a value it
        // refused (`emacs`) meant defaults with ZERO feedback anywhere.
        state_.setLoadWarnings(warnings);
        settingsValues_ = values;
        // AC-1.4 (card 2): the stored `--autofix` mode is a UI fact, not only a
        // transport fact: the hover's 「修复」 button and the command path must
        // honour it. The classification is printed once per load so the six
        // values are observable from the product itself.
        autofixMode_ = values.value(QString::fromLatin1(settings::keys::kLintVeribleAutofix));
        {
            const diagnostics::AutofixModeCheck mode =
                diagnostics::checkAutofixMode(autofixMode_);
            std::printf("AUTOFIX-MODE stored='%s' mode='%s' valid=%d available=%d "
                        "interactive=%d effect=%d reason='%s'\n",
                        qPrintable(autofixMode_), qPrintable(mode.value), mode.valid ? 1 : 0,
                        mode.available ? 1 : 0, mode.interactive ? 1 : 0,
                        static_cast<int>(mode.effect), qPrintable(mode.reason));
        }
        std::printf("SETTINGS-WARNINGS count=%d\n", static_cast<int>(warnings.size()));
        for (const QString &warning : warnings) {
            std::printf("SETTINGS-WARNING '%s'\n", qPrintable(warning));
        }
        std::fflush(stdout);

        QElapsedTimer timer;
        timer.start();
        const QString previousLsp = currentSettings_.lspBackend;
        QString error;
        const bool applied = state_.apply(settings, toolchain_, &error);
        // The request is remembered even when it could not be applied: that is
        // exactly the pair the badge must show (`slang→verible · reason`).
        currentSettings_ = settings;
        if (!applied) {
            const BadgeState badge = state_.badge(activeLanguageId());
            QVariantMap failed = badgeMap(badge);
            failed.insert(QStringLiteral("ok"), false);
            failed.insert(QStringLiteral("reason"), error);
            shell_->setProperty("vedit.diagnostics.switch", failed);
            std::printf("SWITCH-FAILED from=%s requested-lsp=%s requested-lint=%s reason='%s'\n",
                        qPrintable(previousLsp),
                        qPrintable(settings.lspBackend), qPrintable(settings.lintEngine),
                        qPrintable(error));
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Backend switch failed: %1").arg(error),
                    8000);
            }
            // AC-T9.2: an axis that DID apply goes live even though the other
            // one could not; only a total failure leaves the analysis untouched.
            if (!badge.lsp.path.isEmpty() || badge.lint.applied) {
                adoptAppliedSelection();
            }
            publishBadge();
            return false;
        }
        adoptAppliedSelection();
        dumpStatus("SWITCHED");
        const BadgeState badge = state_.badge(activeLanguageId());
        QVariantMap switched;
        switched.insert(QStringLiteral("ok"), true);
        switched.insert(QStringLiteral("backend"), state_.lspBackendName());
        switched.insert(QStringLiteral("lint"), state_.lintEngineName());
        switched.insert(QStringLiteral("requestedLsp"), badge.lsp.requested);
        switched.insert(QStringLiteral("requestedLint"), badge.lint.requested);
        switched.insert(QStringLiteral("basis"), badge.basis);
        switched.insert(QStringLiteral("warnings"), warnings);
        shell_->setProperty("vedit.diagnostics.switch", switched);
        std::printf("SWITCH elapsed-ms=%lld backend=%s lint=%s basis=%s\n", timer.elapsed(),
                    qPrintable(state_.lspBackendName()), qPrintable(state_.lintEngineName()),
                    qPrintable(state_.capabilities().basis));
        std::fflush(stdout);
        publishBadge();
        return true;
    }

    void reloadSettings()
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        if (watcher_ != nullptr) {
            watcher_->reload();
        }
#endif
    }

    void dumpCapabilities()
    {
        const auto capabilities = state_.capabilities();
        std::printf("CAPABILITIES backend=%s basis=%s source='%s'\n",
                    qPrintable(capabilities.backend),
                    qPrintable(capabilities.basis), qPrintable(capabilities.source));
        std::printf("CAPABILITIES note='%s'\n", qPrintable(capabilities.note));
        for (const diagnostics::CapabilityRow &row : capabilities.rows) {
            std::printf("CAP-ROW %s supported=%d reason='%s'\n", qPrintable(row.action),
                        row.supported, qPrintable(row.reason));
        }
        for (const QString &action : QStringList{QStringLiteral("definition"),
                                                 QStringLiteral("references"),
                                                 QStringLiteral("rename"),
                                                 QStringLiteral("hover"),
                                                 QStringLiteral("completion"),
                                                 QStringLiteral("inlayHint"),
                                                 QStringLiteral("codeAction"),
                                                 QStringLiteral("formatting"),
                                                 QStringLiteral("signatureHelp")}) {
            const CapabilityDecision decision = state_.gate(action);
            std::printf("CAP-GATE %s enabled=%d known=%d source=%s reason='%s'\n",
                        qPrintable(action), decision.enabled ? 1 : 0,
                        decision.known ? 1 : 0, qPrintable(decision.source),
                        qPrintable(decision.reason));
        }
        std::fflush(stdout);
        publishGates();
        if (shell_->statusBar() != nullptr) {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "%1 (%2) — %3 action(s)")
                    .arg(capabilities.backend.isEmpty()
                             ? QCoreApplication::translate("VEdit", "no table")
                             : capabilities.backend,
                         capabilities.basis.isEmpty()
                             ? QCoreApplication::translate("VEdit", "unknown basis")
                             : capabilities.basis)
                    .arg(capabilities.rows.size()),
                6000);
        }
    }

    // --- the four actions that have a request path in the C ABI ------------
    void requestDefinition()
    {
        beginNavigation(QStringLiteral("definition"));
    }
    void requestReferences()
    {
        beginNavigation(QStringLiteral("references"));
    }
    /// T16 / AC-3.5: a REAL `textDocument/hover` request for the symbol under
    /// the cursor; the answer is rendered in a popup beside the caret. Every
    /// step writes one machine line (`HOVER-REQUEST` / `HOVER-RESPONSE` /
    /// `HOVER-RENDERED`) so the evidence probe can record request, response and
    /// rendered text separately - no placeholder text ever reaches the popup.
    void requestHover()
    {
        KTextEditor::View *view = activeView();
        vedit::app::EditorPane *pane = activePane();
        if (view == nullptr || pane == nullptr) {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Hover needs a KTextEditor view"), 5000);
            return;
        }
        hideHoverPopup();
        const KTextEditor::Cursor cursor = view->cursorPosition();
        const QString file = pane->currentFilePath();
        pendingAction_ = QStringLiteral("hover");
        pendingRequest_ =
            controller_->requestHover(file, cursor.line(), cursor.column());
        std::printf("HOVER-REQUEST backend=%s file='%s' line=%d column=%d request=%llu\n",
                    qPrintable(state_.lspBackendName()), qPrintable(file), cursor.line(),
                    cursor.column(), static_cast<unsigned long long>(pendingRequest_));
        std::fflush(stdout);
        if (pendingRequest_ == 0) {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Hover request refused by %1")
                    .arg(state_.lspBackendName()),
                6000);
            return;
        }
        navTimer_->start();
    }

    void hideHoverPopup()
    {
        if (!hoverPopup_.isNull()) {
            hoverPopup_->hide();
        }
        hoverShowsDiagnostic_ = false;
    }

    /// The one popup: a server hover and a diagnostic hover share it, so the
    /// user never sees two competing bubbles.
    ///
    /// LIFETIME: the pointer is a QPointer. The popup is parented to the active
    /// view, and `EditorPane::removeTab()` deletes the view together with its
    /// child popup, so a raw pointer here was a use-after-free on the next
    /// hover/navigation. `adoptHoverView()` re-parents on a tab switch and the
    /// view's `destroyed` signal clears the cache before anything can use it.
    /// RM-10: the KTextEditor::View a mouse event's widget belongs to. A REAL
    /// move inside the text lands on KatePart's inner `KateViewInternal` CHILD,
    /// never on the public View (measured: evidence/ux-batch-2/review-b4/
    /// 01-focus-delivery.txt; the same seam MultiCursorController::viewPointFor()
    /// uses). That is why the old filter on the View only ever saw the moves
    /// that PROPAGATED to it (e.g. over the gutter) and never the text.
    static KTextEditor::View *viewForWidget(QWidget *widget)
    {
        for (QWidget *current = widget; current != nullptr; current = current->parentWidget()) {
            if (auto *view = qobject_cast<KTextEditor::View *>(current)) {
                return view;
            }
        }
        return nullptr;
    }

    /// RM-10: the pane that owns `view`. A split layout must answer the hover for
    /// the pane the MOUSE is in, not always the active one.
    vedit::app::EditorPane *paneForView(KTextEditor::View *view) const
    {
        if (view == nullptr || shell_->editorArea() == nullptr) {
            return nullptr;
        }
        for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
            if (pane != nullptr && pane->containsEditorWidget(view)) {
                return pane;
            }
        }
        return nullptr;
    }

    /// RM-10: install the filter on `root` and its whole widget subtree
    /// (idempotent). The inner KatePart widget can be created lazily, which is
    /// why `ChildAdded` re-enters here.
    void watchViewTree(QWidget *root)
    {
        if (root == nullptr) {
            return;
        }
        for (const QPointer<QWidget> &watched : hoverWatched_) {
            if (watched.data() == root) {
                return;
            }
        }
        hoverWatched_.append(root);
        root->installEventFilter(this);
        if (auto *view = qobject_cast<KTextEditor::View *>(root)) {
            view->setMouseTracking(true);
        }
        const QList<QObject *> children = root->children();
        for (QObject *child : children) {
            if (child != nullptr && child->isWidgetType()) {
                watchViewTree(static_cast<QWidget *>(child));
            }
        }
    }

    diagnostics::DiagnosticHoverPopup *hoverPopup()
    {
        if (hoverPopup_.isNull()) {
            // RM-10: anchor to the HOVERED view (adoptHoverView() just set it),
            // never blindly to the active one: a split pane's bubble belongs to
            // the view under the mouse.
            auto *view = qobject_cast<KTextEditor::View *>(hoverView_.data());
            if (view == nullptr) {
                view = activeView();
            }
            QWidget *anchor = view != nullptr ? static_cast<QWidget *>(view) : shell_;
            auto *popup = new diagnostics::DiagnosticHoverPopup(anchor);
            QObject::connect(popup, &diagnostics::DiagnosticHoverPopup::fixRequested, this,
                             [this]() { applyDiagnosticFix(); });
            hoverPopup_ = popup;
        }
        return hoverPopup_.data();
    }

    /// Follow the ACTIVE view: re-parent the popup (a child of the old, hidden
    /// view would never appear again) and clear the cache when the view is
    /// destroyed. Called from the 400 ms live poll, which already follows the
    /// active view for the diagnostic hover.
    void adoptHoverView(KTextEditor::View *view)
    {
        if (view == hoverView_.data()) {
            return;
        }
        hideHoverPopup();
        // RM-10: the filter stays on every view (a split pane's hover must be
        // seen without the view being active), so nothing is removed here; a
        // destroyed view is disconnected by Qt itself.
        QObject::disconnect(hoverViewDestroyed_);
        QObject::disconnect(hoverDocumentEdited_);
        hoverView_ = view;
        if (!hoverPopup_.isNull()) {
            hoverPopup_->setParent(view != nullptr ? static_cast<QWidget *>(view) : shell_);
        }
        if (view != nullptr) {
            watchViewTree(view);
            // RM-10: the bubble describes a range of THIS document. An edit can
            // move or remove the range, so the bubble must not survive an edit.
            if (view->document() != nullptr) {
                hoverDocumentEdited_ = QObject::connect(
                    view->document(), &KTextEditor::Document::textChanged, this, [this]() {
                        if (hoverShowsDiagnostic_) {
                            hideHoverPopup();
                            publishHoverState();
                        }
                    });
            }
            hoverViewDestroyed_ = QObject::connect(view, &QObject::destroyed, this, [this]() {
                // The view (and therefore a popup parented to it) is going away.
                // QPointer would null it for a child popup; clearing explicitly
                // also covers the stable-parent case and kills the cached row.
                hoverPopup_ = nullptr;
                hoverShowsDiagnostic_ = false;
                hoverProbePending_ = false;
                hoverProbeSerial_++;
                hoverView_.clear();
                hoverViewDestroyed_ = QMetaObject::Connection();
                hoverDocumentEdited_ = QMetaObject::Connection();
                publishHoverState();
            });
        }
        publishHoverState();
    }

    /// Observable rename validation (`vedit.diagnostics.rename`): the product
    /// regression reads the refusal reason without opening a name dialog.
    void publishRenameState(bool refused, const QString &reason, const QString &symbol)
    {
        QVariantMap state;
        state.insert(QStringLiteral("refused"), refused);
        state.insert(QStringLiteral("reason"), reason);
        state.insert(QStringLiteral("symbol"), symbol);
        shell_->setProperty("vedit.diagnostics.rename", state);
    }

    /// N4: a REAL stale verdict, not a hard-coded false. The popup caches the
    /// row it is showing; when that row no longer exists in the current merged
    /// view (a reparse, an edit that dropped the upper layer, a backend switch)
    /// the bubble is describing a diagnostic the user can no longer see. The
    /// product clears the popup on a project switch and the view's destruction,
    /// so this flag exists to make a regression of that clearing observable.
    bool hoverRowIsStale() const
    {
        if (!hoverShowsDiagnostic_ || hoverFilePath_.isEmpty()) {
            return false;
        }
        for (const diagnostics::DiagnosticEntry &row :
             controller_->mergedDiagnosticsForFile(hoverFilePath_)) {
            const QString rule = row.code.isEmpty() ? row.source : row.code;
            if (row.line == hoverLine_ && rule == hoverRule_) {
                return false;
            }
        }
        return true;
    }

    /// Observable lifetime state for the product-level regression
    /// (`vedit.diagnostics.hover`): a missing map is a FAIL, not a pass.
    void publishHoverState()
    {
        bool parentIsActiveView = false;
        bool hidden = true;
        if (!hoverPopup_.isNull()) {
            parentIsActiveView = hoverPopup_->parentWidget() != nullptr
                && hoverPopup_->parentWidget() == static_cast<QWidget *>(activeView());
            hidden = !hoverPopup_->isVisible();
        }
        QVariantMap state;
        state.insert(QStringLiteral("known"), true);
        state.insert(QStringLiteral("alive"), !hoverPopup_.isNull());
        state.insert(QStringLiteral("parentIsActiveView"), parentIsActiveView);
        state.insert(QStringLiteral("hiddenOnViewChange"), hidden);
        state.insert(QStringLiteral("diagnostic"), hoverShowsDiagnostic_);
        state.insert(QStringLiteral("rule"), hoverRule_);
        state.insert(QStringLiteral("line"), hoverLine_);
        state.insert(QStringLiteral("path"), hoverFilePath_);
        state.insert(QStringLiteral("probePending"), hoverProbePending_);
        state.insert(QStringLiteral("fixable"), hoverDiagnosticFixable_);
        state.insert(QStringLiteral("stale"), hoverRowIsStale());
        shell_->setProperty("vedit.diagnostics.hover", state);
    }

    void placePopup(QWidget *popup, const QPoint &viewportPoint)
    {
        if (popup == nullptr) {
            return;
        }
        QWidget *anchor = popup->parentWidget();
        if (anchor == nullptr) {
            return;
        }
        // RM-10: the point is in VIEW coordinates of the view under the mouse.
        // The popup is re-parented to that view before placement, so the anchor
        // IS the view; fall back to the active view only for a stable-parent
        // popup (no diagnostic hover path does that).
        KTextEditor::View *view = qobject_cast<KTextEditor::View *>(anchor);
        if (view == nullptr) {
            view = activeView();
        }
        if (view == nullptr) {
            return;
        }
        popup->adjustSize();
        // `popup` is a CHILD widget (of the hovered view), so move() takes
        // PARENT coordinates. The old code added the view's GLOBAL origin to a
        // point that was already parent-relative, which put the bubble roughly
        // twice the offset to the right/below - off the window on a real
        // desktop. A widget-only screenshot could not show that.
        const QPoint local =
            view->mapToGlobal(viewportPoint) - anchor->mapToGlobal(QPoint(0, 0));
        int x = local.x() + 18;
        int y = local.y() + 22;
        if (x + popup->width() > anchor->width() - 8) {
            x = std::max(0, anchor->width() - popup->width() - 8);
        }
        if (y + popup->height() > anchor->height() - 8) {
            y = std::max(0, local.y() - popup->height() - 6);
        }
        popup->move(x, y);
        popup->raise();
        popup->show();
    }

    /// Render the server's hover in the popup beside the caret. The payload is
    /// the server's PLAIN text: not ours to interpret as HTML, and not a
    /// diagnostic, so no fix button and no "no fix" reason is invented for it.
    void renderHover(const QString &text)
    {
        KTextEditor::View *view = activeView();
        if (view == nullptr) {
            return;
        }
        // nit (review-b3-r3): the answer can land after a tab switch; adopt the
        // ACTIVE view first, or the bubble is placed in a popup that is still
        // parented to the old (hidden) view.
        if (view != hoverView_.data()) {
            adoptHoverView(view);
        }
        diagnostics::HoverContent content;
        content.found = false; // a plain symbol hover, not a diagnostic row
        content.title = QCoreApplication::translate("VEdit", "Hover · %1")
                            .arg(state_.lspBackendName());
        content.body = text;
        content.tooltip = text;
        diagnostics::DiagnosticHoverPopup *popup = hoverPopup();
        popup->setContent(content);
        hoverShowsDiagnostic_ = false;
        placePopup(popup, view->cursorToCoordinate(view->cursorPosition()));
        std::printf("HOVER-RENDERED backend=%s widget='veditHoverPopup' visible=%d bytes=%lld "
                    "text='%s'\n",
                    qPrintable(state_.lspBackendName()), popup->isVisible() ? 1 : 0,
                    static_cast<long long>(text.toUtf8().size()),
                    qPrintable(text.left(200)));
        std::fflush(stdout);
    }

    /// item 9: hover a DIAGNOSTIC range -> severity + rule id + message (+ a
    /// 「修复」 button only when the engine really can autofix that rule).
    void handleDiagnosticHover(KTextEditor::View *view, const QPoint &viewportPoint)
    {
        // A hover can arrive between a tab switch and the 400 ms live poll; make
        // sure the popup belongs to THIS view before placing it there.
        if (view != hoverView_.data()) {
            adoptHoverView(view);
        }
        // RM-10: the path belongs to the pane the VIEW is in (split panes), not
        // to the globally active pane.
        vedit::app::EditorPane *pane = paneForView(view);
        if (view == nullptr || pane == nullptr || view->document() == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        if (path.isEmpty()) {
            return;
        }
        // RM-10: `viewportPoint` is already mapped into VIEW coordinates by the
        // caller (the real mouse target is KatePart's inner widget).
        const KTextEditor::Cursor cursor = view->coordinatesToCursor(viewportPoint);
        const QVector<diagnostics::DiagnosticEntry> rows =
            controller_->mergedDiagnosticsForFile(path);
        const diagnostics::DiagnosticEntry *entry =
            diagnostics::diagnosticAt(rows, cursor.line(), cursor.column());
        if (entry == nullptr) {
            if (hoverShowsDiagnostic_) {
                hideHoverPopup();
                // RM-10: publish the transition AT ONCE. The 400 ms poll used to
                // be the only writer, so an observer (or a regression) could see
                // "diagnostic=true" for up to 400 ms after the bubble was gone.
                publishHoverState();
            }
            return;
        }
        const QString rule = entry->code.isEmpty() ? entry->source : entry->code;
        if (hoverShowsDiagnostic_ && hoverFilePath_ == path && hoverRule_ == rule
            && hoverLine_ == entry->line) {
            return; // the same row: do not re-probe on every mouse pixel
        }
        const QString text = view->document()->text();
        hoverEntry_ = *entry;
        hoverFilePath_ = path;
        hoverRule_ = rule;
        hoverLine_ = entry->line;
        hoverShowsDiagnostic_ = true;
        hoverViewportPoint_ = viewportPoint;
        hoverProbePending_ = true;
        hoverDiagnosticFixable_ = false;
        const quint64 serial = ++hoverProbeSerial_;
        // AC-1.4 (card 2): the stored --autofix mode is part of the offer. An
        // interactive value or an unknown spelling never shows a 「修复」 button;
        // the mode's own reason is what the tooltip carries.
        const diagnostics::AutofixModeCheck mode = diagnostics::checkAutofixMode(autofixMode_);
        const bool modeOffersFix = mode.valid && mode.available;
        const QString modeReason = mode.reason;
        // item 9 / MEDIUM c: do NOT run the engine on the mouse-move path. The
        // content is shown at once and the measured fixability arrives through
        // an asynchronous probe; the popup is updated when it lands.
        diagnostics::HoverContent pending = diagnostics::describeDiagnostic(
            *entry, false,
            modeOffersFix
                ? QCoreApplication::translate("VEdit",
                                              "checking whether the engine can fix this…")
                : modeReason);
        pending.fixPending = modeOffersFix;
        diagnostics::DiagnosticHoverPopup *popup = hoverPopup();
        popup->setContent(pending);
        placePopup(popup, viewportPoint);
        publishHoverState();
        std::printf("DIAG-HOVER file='%s' line=%d column=%d severity=%d rule='%s' pending=1 "
                    "probe-count=%lld\n",
                    qPrintable(path), entry->line + 1, entry->column + 1, entry->severity,
                    qPrintable(rule), static_cast<long long>(autofix_.asyncProbeCount()));
        std::fflush(stdout);
        if (!modeOffersFix) {
            // The engine is never asked for a fix this product cannot run.
            std::printf("DIAG-HOVER-PROBE rule='%s' fixable=0 mode='%s' reason='%s'\n",
                        qPrintable(rule), qPrintable(autofixMode_), qPrintable(modeReason));
            std::fflush(stdout);
            return;
        }
        const QString probePath = path;
        const int probeLine = entry->line;
        autofix_.probeAsync(rule, path, text,
                            [this, serial, probePath, probeLine](const diagnostics::AutofixOutcome &probe) {
            if (serial != hoverProbeSerial_ || probePath != hoverFilePath_
                || probeLine != hoverLine_) {
                return; // a newer hover owns the popup now
            }
            hoverProbePending_ = false;
            hoverDiagnosticFixable_ = probe.supported;
            if (hoverShowsDiagnostic_ && !hoverPopup_.isNull()) {
                const diagnostics::HoverContent content =
                    diagnostics::describeDiagnostic(hoverEntry_, probe.supported, probe.error);
                hoverPopup_->setContent(content);
                placePopup(hoverPopup_, hoverViewportPoint_);
            }
            std::printf("DIAG-HOVER-PROBE rule='%s' fixable=%d async-count=%lld\n",
                        qPrintable(hoverRule_), probe.supported ? 1 : 0,
                        static_cast<long long>(autofix_.asyncProbeCount()));
            std::fflush(stdout);
            publishHoverState();
        });
    }

    /// item 9: apply the engine's autofix as ONE undoable whole-document edit.
    void applyDiagnosticFix()
    {
        // RM-10: the :Fix」 button belongs to the bubble, and the bubble is
        // parented to the view it was built for - use THAT view's pane, not the
        // globally active one (split panes).
        auto *view = qobject_cast<KTextEditor::View *>(
            hoverPopup_.isNull() ? nullptr : hoverPopup_->parentWidget());
        if (view == nullptr) {
            view = activeView();
        }
        vedit::app::EditorPane *pane = paneForView(view);
        if (view == nullptr || view->document() == nullptr || !hoverShowsDiagnostic_) {
            return;
        }
        // nit (review-b3-r3): in a split layout the focus can move to another
        // pane between the hover and the click. The cached rule/file describe the
        // file the BUBBLE was built for; requiring that pane to still show that
        // file keeps a fix computed for A out of B's buffer.
        if (pane == nullptr || pane->currentFilePath() != hoverFilePath_) {
            return;
        }
        // AC-1.4 (card 2): the stored `lint.verible.autofix` mode decides what
        // the button really does. An interactive value cannot complete without a
        // terminal, `inplace` rewrites files (confirm first) and
        // `generate-waiver` writes + registers a waiver instead of editing the
        // buffer. Before this, the button ignored the mode entirely.
        const diagnostics::AutofixModeCheck mode = diagnostics::checkAutofixMode(autofixMode_);
        if (!mode.valid || !mode.available) {
            const QString reason = mode.reason.isEmpty()
                ? QCoreApplication::translate("VEdit", "the configured --autofix mode cannot run here")
                : mode.reason;
            std::printf("AUTOFIX-REFUSED rule='%s' mode='%s' reason='%s'\n",
                        qPrintable(hoverRule_), qPrintable(autofixMode_), qPrintable(reason));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "No automatic fix: %1").arg(reason), 9000);
            return;
        }
        if (mode.effect == diagnostics::AutofixEffect::WritesInPlace) {
            const auto answer = QMessageBox::question(
                shell_, QCoreApplication::translate("VEdit", "Apply the fix?"),
                QCoreApplication::translate(
                    "VEdit",
                    "The stored --autofix mode is `inplace`, so verible-verilog-lint rewrites the "
                    "file. Apply the fix for `%1` to this document? It becomes one undoable edit; "
                    "the file changes when you save.")
                    .arg(hoverRule_),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                std::printf("AUTOFIX-CANCELLED rule='%s' mode='inplace'\n", qPrintable(hoverRule_));
                std::fflush(stdout);
                return;
            }
        }
        if (mode.effect == diagnostics::AutofixEffect::GeneratesWaiver) {
            std::printf("AUTOFIX-GENERATE-WAIVER rule='%s' file='%s'\n", qPrintable(hoverRule_),
                        qPrintable(hoverFilePath_));
            std::fflush(stdout);
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
            waiveDiagnostic(diagnostics::LintEngineKind::VeribleLint, hoverEntry_,
                            hoverFilePath_, QStringLiteral("autofix-generate-waiver"));
#else
            shell_->statusBar()->showMessage(
                QCoreApplication::translate(
                    "VEdit", "This build has no waiver service, so it cannot generate a waiver."),
                9000);
#endif
            hideHoverPopup();
            return;
        }
        const QString original = view->document()->text();
        const diagnostics::AutofixOutcome outcome =
            autofix_.fix(hoverRule_, hoverFilePath_, original);
        if (!outcome.ok()) {
            std::printf("AUTOFIX-SKIPPED rule='%s' reason='%s'\n", qPrintable(hoverRule_),
                        qPrintable(outcome.error));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "No automatic fix: %1").arg(outcome.error),
                8000);
            return;
        }
        const bool applied =
            diagnostics::applyAutofixToDocument(view->document(), outcome.fixedText);
        hideHoverPopup();
        if (!applied) {
            // item 9: a refused replacement (read-only document, concurrent
            // edit) is a FAILURE. Claiming "Fixed" here was a lie; the log tag
            // must not read AUTOFIX-APPLIED either (it used to print applied=0).
            std::printf("AUTOFIX-FAILED rule='%s' applied=0 in-bytes=%lld out-bytes=%lld file='%s' "
                        "reason='document refused the replacement'\n",
                        qPrintable(hoverRule_),
                        static_cast<long long>(original.toUtf8().size()),
                        static_cast<long long>(outcome.fixedText.toUtf8().size()),
                        qPrintable(hoverFilePath_));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit",
                                            "The fix could not be applied: the document refused "
                                            "the edit (is it read-only?)"),
                9000);
            return;
        }
        std::printf("AUTOFIX-APPLIED rule='%s' applied=1 in-bytes=%lld out-bytes=%lld file='%s'\n",
                    qPrintable(hoverRule_),
                    static_cast<long long>(original.toUtf8().size()),
                    static_cast<long long>(outcome.fixedText.toUtf8().size()),
                    qPrintable(hoverFilePath_));
        std::fflush(stdout);
        shell_->statusBar()->showMessage(
            QCoreApplication::translate("VEdit", "Fixed %1 with verible-verilog-lint (one undo "
                                                 "reverts it)")
                .arg(hoverRule_),
            6000);
    }

    // --- AC-5.1/5.6/5.8 (card 2): the waiver ACTION, wired to the real panel ---
    //
    // The data layer (waiver_actions.h) decides WHAT would be written and refuses
    // the cases the engines cannot express. This section only wires it to the
    // user: a context menu on the real Diagnostics panel rows (and the
    // `diagnostics.waive` command for the selected row). Every operation is one
    // asynchronous Rust job, so no child/disk work runs on the GUI stack; the
    // result is printed as a machine line AND shown in the status bar, so a
    // failure is as visible as a success.
    //
    // The panel's row roles are the panel's own contract (diagnostics_panel.cpp:
    // file/line/column under Qt::UserRole, severity under the public
    // IssueSeverityRole). The DiagnosticEntry itself is looked up in the
    // controller by identity - never reconstructed from the rendered row text,
    // so a waiver can never be planned from a string a user edited.
    bool entryForItem(QTreeWidgetItem *item, diagnostics::DiagnosticEntry *entry,
                      QString *file) const
    {
        if (item == nullptr || entry == nullptr || file == nullptr) {
            return false; // empty space, or a group header (severity -1)
        }
        const QVariant severity = item->data(0, diagnostics::DiagnosticsPanel::IssueSeverityRole);
        if (severity.isValid() && severity.toInt() < 0) {
            return false;
        }
        *file = item->data(0, Qt::UserRole).toString();
        const int line = item->data(0, Qt::UserRole + 1).toInt();
        const int column = item->data(0, Qt::UserRole + 2).toInt();
        if (file->isEmpty()) {
            return false;
        }
        const QVector<diagnostics::DiagnosticEntry> rows =
            controller_->mergedDiagnosticsForFile(*file);
        if (const diagnostics::DiagnosticEntry *found =
                diagnostics::diagnosticAt(rows, line, column)) {
            *entry = *found;
            return true;
        }
        // A row whose column lies outside the entry's range still names its line;
        // falling back to that line keeps the action reachable.
        for (const diagnostics::DiagnosticEntry &row : rows) {
            if (row.line == line) {
                *entry = row;
                return true;
            }
        }
        return false;
    }

    /// The engine the row came from decides the default waiver format. A slang
    /// row stays slang (its `optionName` decides whether it can be waived), a
    /// verible/LSP row stays verible; the context menu lets the user override.
    static diagnostics::LintEngineKind engineForRow(const diagnostics::DiagnosticEntry &entry)
    {
        return entry.source.contains(QStringLiteral("slang"), Qt::CaseInsensitive)
            ? diagnostics::LintEngineKind::Slang
            : diagnostics::LintEngineKind::VeribleLint;
    }

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    /// One live waiver job (an append or a registration) and what its result
    /// means for the log/status line.
    struct WaiverJob {
        enum class Kind { Append, Register };
        Kind kind = Kind::Append;
        diagnostics::LintEngineKind engine = diagnostics::LintEngineKind::VeribleLint;
        QString path;
        QString trigger;
    };

    static QString firstPathLine(const QString &value)
    {
        for (const QString &line : value.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                return trimmed;
            }
        }
        return QString();
    }

    /// The waiver file this engine writes to: the store's configured file when
    /// it names one (an existing setup is respected), else a project-root file
    /// created on first use and registered right after. Empty = no project root
    /// and no configured path, i.e. nothing can be scoped.
    QString waiverPathFor(diagnostics::LintEngineKind engine) const
    {
        QString configured;
        QString defaultName;
        if (engine == diagnostics::LintEngineKind::Slang) {
            // The one slang waiver file (`--waiver-file`); the key is owned by
            // the settings lane (src/workspace/settings/lint_options.h:185).
            configured = firstPathLine(
                settingsValues_.value(QStringLiteral("lint.slang.waiver_file")));
            defaultName = QStringLiteral("slang-waivers.toml");
        } else {
            configured = firstPathLine(settingsValues_.value(
                QString::fromLatin1(settings::keys::kLintVeribleWaiverFiles)));
            defaultName = QStringLiteral("verible-waivers.txt");
        }
        if (!configured.isEmpty()) {
            return configured;
        }
        if (workspaceRoot_.isEmpty()) {
            return QString();
        }
        return QDir(workspaceRoot_).filePath(defaultName);
    }

    static QString engineToken(diagnostics::LintEngineKind engine)
    {
        return engine == diagnostics::LintEngineKind::Slang ? QStringLiteral("slang")
                                                            : QStringLiteral("verible");
    }

    /// Plan + start ONE waiver append. A plan the engines cannot express is
    /// REFUSED here (before a job exists) with the data layer's own sentence -
    /// never widened into a whole-file waiver (AC-5.8).
    void waiveDiagnostic(diagnostics::LintEngineKind engine,
                         const diagnostics::DiagnosticEntry &row,
                         const QString &fileFallback,
                         const QString &trigger)
    {
        const diagnostics::WaiverEntry plan =
            diagnostics::planWaiverEntry(engine, row, row.optionName, fileFallback);
        if (!plan.valid) {
            std::printf("WAIVER-REFUSED engine=%s trigger=%s file='%s' line=%d code='%s' "
                        "option='%s' reason='%s'\n",
                        qPrintable(engineToken(engine)), qPrintable(trigger),
                        qPrintable(plan.file), row.line + 1, qPrintable(row.code),
                        qPrintable(row.optionName), qPrintable(plan.reason));
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Waiver refused: %1").arg(plan.reason),
                    9000);
            }
            return;
        }
        const QString path = waiverPathFor(engine);
        if (path.isEmpty()) {
            const QString reason = QCoreApplication::translate(
                "VEdit",
                "no waiver file is configured and there is no project root to create one in");
            std::printf("WAIVER-REFUSED engine=%s trigger=%s file='%s' reason='%s'\n",
                        qPrintable(engineToken(engine)), qPrintable(trigger),
                        qPrintable(plan.file), qPrintable(reason));
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Waiver refused: %1").arg(reason), 9000);
            }
            return;
        }
        // The exact line/block that will be written, so the log proves the
        // preview and the engine call are the same value.
        const QString preview = engine == diagnostics::LintEngineKind::Slang
            ? diagnostics::slangWaiverBlock(plan.file, plan.optionName).content
            : diagnostics::veribleWaiverLine(plan);
        std::printf("WAIVER-PLAN engine=%s trigger=%s file='%s' line=%d path='%s' "
                    "preview='%s'\n",
                    qPrintable(engineToken(engine)), qPrintable(trigger),
                    qPrintable(plan.file), plan.line, qPrintable(path),
                    qPrintable(QString(preview).replace(QLatin1Char('\n'),
                                                        QStringLiteral("\\n"))));
        std::fflush(stdout);
        QString error;
        const quint64 job = waiver_.append(path, plan, /*createParentDirs=*/true, &error);
        if (job == 0) {
            std::printf("WAIVER-FAILED engine=%s trigger=%s path='%s' reason='%s'\n",
                        qPrintable(engineToken(engine)), qPrintable(trigger), qPrintable(path),
                        qPrintable(error));
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Waiver failed: %1").arg(error), 9000);
            }
            return;
        }
        WaiverJob record;
        record.kind = WaiverJob::Kind::Append;
        record.engine = engine;
        record.path = path;
        record.trigger = trigger;
        waiverJobs_.insert(job, record);
        if (waiverTimer_ != nullptr) {
            waiverTimer_->start();
        }
    }

    /// Poll every live waiver job on the caller's event loop. A READY append is
    /// followed by a REGISTER job, so the next lint pass really uses the file.
    void pollWaiverJobs()
    {
        if (waiverJobs_.isEmpty()) {
            if (waiverTimer_ != nullptr) {
                waiverTimer_->stop();
            }
            return;
        }
        QList<quint64> polled;
        QList<QPair<quint64, WaiverJob>> started;
        for (auto it = waiverJobs_.cbegin(); it != waiverJobs_.cend(); ++it) {
            const quint64 id = it.key();
            const WaiverJob job = it.value();
            const diagnostics::WaiverActions::Result result = waiver_.poll(id);
            if (result.state == diagnostics::WaiverActions::State::Pending) {
                continue;
            }
            polled.append(id);
            const bool append = job.kind == WaiverJob::Kind::Append;
            if (result.state == diagnostics::WaiverActions::State::Ready) {
                const QString writtenPath = result.path.isEmpty() ? job.path : result.path;
                if (append) {
                    std::printf("WAIVER-READY engine=%s trigger=%s written=%d deduped=%d "
                                "path='%s' entries=%d skipped=%d\n",
                                qPrintable(engineToken(job.engine)), qPrintable(job.trigger),
                                result.written ? 1 : 0, result.deduped ? 1 : 0,
                                qPrintable(writtenPath), result.entries, result.skipped);
                    if (!result.content.isEmpty()) {
                        std::printf("WAIVER-CONTENT '%s'\n",
                                    qPrintable(QString(result.content)
                                                   .replace(QLatin1Char('\n'),
                                                            QStringLiteral("\\n"))));
                    }
                    std::fflush(stdout);
                    // AC-5.1/5.3: the produced file is registered in the store, so
                    // the next lint pass really consumes it. The registration is a
                    // second job with its own visible outcome.
                    QString error;
                    const quint64 registerJob =
                        waiver_.registerFile(settingsPath_, job.engine, writtenPath, &error);
                    if (registerJob == 0) {
                        std::printf("WAIVER-REGISTER-FAILED engine=%s path='%s' reason='%s'\n",
                                    qPrintable(engineToken(job.engine)),
                                    qPrintable(writtenPath), qPrintable(error));
                        std::fflush(stdout);
                        if (shell_->statusBar() != nullptr) {
                            shell_->statusBar()->showMessage(
                                QCoreApplication::translate("VEdit",
                                                            "The waiver was written but could not "
                                                            "be registered: %1")
                                    .arg(error),
                                9000);
                        }
                    } else {
                        WaiverJob registration;
                        registration.kind = WaiverJob::Kind::Register;
                        registration.engine = job.engine;
                        registration.path = writtenPath;
                        registration.trigger = job.trigger;
                        started.append(qMakePair(registerJob, registration));
                    }
                    if (shell_->statusBar() != nullptr) {
                        shell_->statusBar()->showMessage(
                            QCoreApplication::translate("VEdit", "Waiver written to %1")
                                .arg(writtenPath),
                            8000);
                    }
                } else {
                    std::printf("WAIVER-REGISTERED engine=%s path='%s' written=%d deduped=%d\n",
                                qPrintable(engineToken(job.engine)), qPrintable(writtenPath),
                                result.written ? 1 : 0, result.deduped ? 1 : 0);
                    std::fflush(stdout);
                    // The store changed on disk: reload it so the page/badge show
                    // the new configuration and the next lint pass uses it.
                    reloadSettings();
                }
            } else {
                const QString reason = result.error.isEmpty()
                    ? QCoreApplication::translate("VEdit", "the waiver job failed")
                    : result.error;
                std::printf("WAIVER-%s engine=%s path='%s' reason='%s'\n",
                            append ? "FAILED" : "REGISTER-FAILED",
                            qPrintable(engineToken(job.engine)), qPrintable(job.path),
                            qPrintable(reason));
                std::fflush(stdout);
                if (shell_->statusBar() != nullptr) {
                    shell_->statusBar()->showMessage(
                        QCoreApplication::translate("VEdit", "Waiver failed: %1").arg(reason),
                        9000);
                }
            }
        }
        for (quint64 id : polled) {
            waiverJobs_.remove(id);
        }
        for (const auto &entry : started) {
            waiverJobs_.insert(entry.first, entry.second);
        }
    }

    /// Right-click on a real diagnostics row: one action per engine, each
    /// enabled only when the data layer can express it. The reason of a disabled
    /// action is its tooltip, so a refusal is explainable before it happens.
    void showWaiverMenu(QTreeWidget *tree, const QPoint &pos)
    {
        if (tree == nullptr) {
            return;
        }
        QTreeWidgetItem *item = tree->itemAt(pos);
        if (item == nullptr) {
            std::printf("WAIVER-MENU row=none\n");
            std::fflush(stdout);
            return;
        }
        diagnostics::DiagnosticEntry entry;
        QString file;
        if (!entryForItem(item, &entry, &file)) {
            std::printf("WAIVER-MENU row=header\n");
            std::fflush(stdout);
            return;
        }
        std::printf("WAIVER-MENU row='%s:%d' source='%s' code='%s' option='%s'\n",
                    qPrintable(file), entry.line + 1, qPrintable(entry.source),
                    qPrintable(entry.code), qPrintable(entry.optionName));
        std::fflush(stdout);
        const diagnostics::WaiverEntry veriblePlan =
            diagnostics::planWaiverEntry(diagnostics::LintEngineKind::VeribleLint, entry);
        const diagnostics::WaiverEntry slangPlan = diagnostics::planWaiverEntry(
            diagnostics::LintEngineKind::Slang, entry, entry.optionName, file);
        QMenu menu(tree);
        menu.setObjectName(QStringLiteral("waiver-menu"));
        QAction *verible = menu.addAction(
            QCoreApplication::translate("VEdit", "Waive with verible-verilog-lint"));
        verible->setEnabled(veriblePlan.valid);
        verible->setToolTip(veriblePlan.valid ? diagnostics::veribleWaiverLine(veriblePlan)
                                              : veriblePlan.reason);
        QAction *slang = menu.addAction(
            QCoreApplication::translate("VEdit", "Waive with slang ([[waivers]])"));
        slang->setEnabled(slangPlan.valid);
        slang->setToolTip(slangPlan.valid ? slangPlan.optionName : slangPlan.reason);
        menu.addSeparator();
        // Always visible: why an engine is not offered, so a refusal can never
        // look like a missing feature. A missing optionName stays a REFUSAL -
        // this product never widens a diagnostic into a whole-file waiver.
        const QString explanation = !slangPlan.valid
            ? slangPlan.reason
            : (!veriblePlan.valid ? veriblePlan.reason : QString());
        if (!explanation.isEmpty()) {
            QAction *why = menu.addAction(explanation);
            why->setEnabled(false);
        }
        QAction *chosen = menu.exec(tree->viewport()->mapToGlobal(pos));
        if (chosen == verible) {
            waiveDiagnostic(diagnostics::LintEngineKind::VeribleLint, entry, file,
                            QStringLiteral("panel-menu"));
        } else if (chosen == slang) {
            waiveDiagnostic(diagnostics::LintEngineKind::Slang, entry, file,
                            QStringLiteral("panel-menu"));
        }
    }
#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

    /// AC-N3 E6 evidence seam (test-only, env-gated like VEDIT_UI_PROBE):
    /// drive the SAME handler the panel context menu and the `diagnostics.waive`
    /// command call, for a row the panel really renders. Format
    /// `<auto|verible|slang>:<path-suffix>:<0-based line>`; without the variable
    /// nothing runs. It fakes nothing: the row is read from the controller, the
    /// plan from the data layer and the write from the Rust service.
    void runWaiverProbeIfRequested()
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        if (waiverProbeDone_) {
            return;
        }
        const QByteArray raw = qgetenv("VEDIT_DIAGNOSTICS_WAIVE_PROBE");
        if (raw.isEmpty()) {
            return;
        }
        const QStringList parts = QString::fromLocal8Bit(raw).split(QLatin1Char(':'));
        if (parts.size() != 3) {
            return;
        }
        bool ok = false;
        const int line = parts.at(2).toInt(&ok);
        if (!ok) {
            return;
        }
        for (const diagnostics::DisplayDiagnostic &row : controller_->displayDiagnostics()) {
            if (!row.entry.file.endsWith(parts.at(1)) || row.entry.line != line) {
                continue;
            }
            waiverProbeDone_ = true;
            diagnostics::LintEngineKind engine = engineForRow(row.entry);
            if (parts.at(0) == QStringLiteral("verible")) {
                engine = diagnostics::LintEngineKind::VeribleLint;
            } else if (parts.at(0) == QStringLiteral("slang")) {
                engine = diagnostics::LintEngineKind::Slang;
            }
            std::printf("WAIVE-PROBE source=panel-row file='%s' line=%d engine=%s code='%s' "
                        "option='%s' origin=%d\n",
                        qPrintable(row.entry.file), row.entry.line + 1,
                        qPrintable(engineToken(engine)), qPrintable(row.entry.code),
                        qPrintable(row.entry.optionName), static_cast<int>(row.origin));
            std::fflush(stdout);
            waiveDiagnostic(engine, row.entry, row.entry.file, QStringLiteral("env-probe"));
            return;
        }
#endif
    }

    /// Make the waiver action reachable from the REAL Diagnostics panel. The
    /// panel can be inserted after this wiring (a restored dock builds its
    /// content lazily), so this is idempotent and the live poll re-checks it.
    void attachWaiverUi()
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        if (issuesTree_ != nullptr || shell_->panels() == nullptr) {
            return;
        }
        QDockWidget *dock = shell_->panels()->dock(QStringLiteral("outline"));
        if (dock == nullptr) {
            return;
        }
        auto *panel = qobject_cast<diagnostics::DiagnosticsPanel *>(dock->widget());
        if (panel == nullptr) {
            return;
        }
        auto *tree = qobject_cast<QTreeWidget *>(panel->issuesWidget());
        if (tree == nullptr) {
            return;
        }
        issuesTree_ = tree;
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree, &QWidget::customContextMenuRequested, this,
                [this, tree](const QPoint &pos) { showWaiverMenu(tree, pos); });
        std::printf("WAIVER-UI panel='%s' dock-title='%s' row-actions=2 "
                    "tree-global=%d,%d %dx%d\n",
                    qPrintable(dock->objectName()), qPrintable(dock->windowTitle()),
                    tree->mapToGlobal(QPoint(0, 0)).x(), tree->mapToGlobal(QPoint(0, 0)).y(),
                    tree->width(), tree->height());
        std::fflush(stdout);
#endif
    }

    /// The diagnostic under the caret in the ACTIVE editor (the command's
    /// second path, so the action also works without touching the panel). The
    /// row is looked up in the controller's merged view, exactly like a panel
    /// row; a caret anywhere on the diagnostic's line selects that line's first
    /// row (a CLI row may report a column range narrower than the caret).
    bool entryAtCaret(diagnostics::DiagnosticEntry *entry, QString *file) const
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        KTextEditor::View *view = activeView();
        vedit::app::EditorPane *pane = activePane();
        if (view == nullptr || view->document() == nullptr || pane == nullptr) {
            return false;
        }
        const QString path = pane->currentFilePath();
        if (path.isEmpty()) {
            return false;
        }
        const KTextEditor::Cursor cursor = view->cursorPosition();
        const QVector<diagnostics::DiagnosticEntry> rows =
            controller_->mergedDiagnosticsForFile(path);
        if (const diagnostics::DiagnosticEntry *found =
                diagnostics::diagnosticAt(rows, cursor.line(), cursor.column())) {
            *entry = *found;
            *file = path;
            return true;
        }
        for (const diagnostics::DiagnosticEntry &row : rows) {
            if (row.line == cursor.line()) {
                *entry = row;
                *file = path;
                return true;
            }
        }
        return false;
#else
        (void)entry;
        (void)file;
        return false;
#endif
    }

    /// The `diagnostics.waive` command: waive the row SELECTED in the panel, or
    /// (when the panel has no selection) the diagnostic under the caret in the
    /// editor. Kept separate from the context menu so a keyboard/command-registry
    /// path exists, and a probe can drive the same handler without a mouse.
    void waiveSelectedDiagnostic()
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        attachWaiverUi();
        diagnostics::DiagnosticEntry entry;
        QString file;
        QString trigger;
        if (issuesTree_ != nullptr && entryForItem(issuesTree_->currentItem(), &entry, &file)) {
            trigger = QStringLiteral("command-panel-row");
        } else if (entryAtCaret(&entry, &file)) {
            trigger = QStringLiteral("command-caret");
        } else {
            std::printf("WAIVER-REFUSED trigger=command reason='no diagnostic is selected in the "
                        "panel and there is none on the caret line'\n");
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate(
                        "VEdit",
                        "Select a diagnostic in the Diagnostics panel (or put the caret on one) "
                        "first."),
                    8000);
            }
            return;
        }
        waiveDiagnostic(engineForRow(entry), entry, file, trigger);
#endif
    }

    void requestRename()
    {
        KTextEditor::View *view = activeView();
        vedit::app::EditorPane *pane = activePane();
        if (view == nullptr || pane == nullptr) {
            return;
        }
        // B5 (item 4): the capability gate is enforced HERE, on the request
        // path, not only by greying the menu item. A backend that does not
        // provide rename refuses with its own reason and no dialog is shown.
        const CapabilityDecision gate = state_.gate(QStringLiteral("rename"));
        if (!gate.enabled) {
            std::printf("RENAME-REFUSED backend=%s reason='%s'\n",
                        qPrintable(state_.lspBackendName()), qPrintable(gate.reason));
            std::fflush(stdout);
            publishRenameState(true, gate.reason, QString());
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Rename is not available: %1").arg(gate.reason),
                9000);
            return;
        }
        // Validate the CURSOR first: making the user type a name and only then
        // failing is exactly the behaviour the ruling forbids. `prepareRename`
        // is not exposed by the ABI, so the local check rejects everything it
        // can classify: no token at all, number literals, comment/string text,
        // system tasks ($display) and language keywords. The server stays the
        // authority for anything else.
        const KTextEditor::Cursor cursor = view->cursorPosition();
        const QString documentText =
            view->document() != nullptr ? view->document()->text() : QString();
        const diagnostics::RenameTarget target =
            diagnostics::renameTargetAt(documentText, cursor.line(), cursor.column());
        const QString symbol = target.symbol;
        if (!target.valid) {
            const QString reason = target.reason.isEmpty()
                ? QCoreApplication::translate(
                      "VEdit", "Rename needs the cursor on a symbol (line %1, column %2 has none)")
                      .arg(cursor.line() + 1)
                      .arg(cursor.column() + 1)
                : target.reason;
            std::printf("RENAME-REFUSED backend=%s symbol='%s' reason='%s' line=%d column=%d\n",
                        qPrintable(state_.lspBackendName()), qPrintable(symbol),
                        qPrintable(reason), cursor.line() + 1, cursor.column() + 1);
            std::fflush(stdout);
            publishRenameState(true, reason, symbol);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Rename is not possible here: %1").arg(reason),
                9000);
            return;
        }
        bool ok = false;
        const QString newName = QInputDialog::getText(
            shell_, QCoreApplication::translate("VEdit", "Rename %1").arg(symbol),
            QCoreApplication::translate("VEdit", "New name:"), QLineEdit::Normal, symbol, &ok);
        if (!ok || newName.trimmed().isEmpty() || newName == symbol) {
            return;
        }
        const QString file = pane->currentFilePath();
        pendingAction_ = QStringLiteral("rename");
        pendingRequest_ =
            controller_->requestRename(file, cursor.line(), cursor.column(), newName.trimmed());
        if (pendingRequest_ == 0) {
            const QString reason = controller_->lastError().isEmpty()
                ? QCoreApplication::translate("VEdit", "%1 refused the rename request")
                      .arg(state_.lspBackendName())
                : controller_->lastError();
            std::printf("RENAME-REFUSED backend=%s reason='%s'\n",
                        qPrintable(state_.lspBackendName()), qPrintable(reason));
            std::fflush(stdout);
            publishRenameState(true, reason, symbol);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Rename was refused: %1").arg(reason), 9000);
            return;
        }
        publishRenameState(false, QString(), symbol);
        std::printf("RENAME-REQUEST backend=%s symbol='%s' new='%s' line=%d column=%d\n",
                    qPrintable(state_.lspBackendName()), qPrintable(symbol),
                    qPrintable(newName.trimmed()), cursor.line() + 1, cursor.column() + 1);
        std::fflush(stdout);
        navTimer_->start();
    }

    void formatDocument()
    {
        KTextEditor::View *view = activeView();
        vedit::app::EditorPane *pane = activePane();
        if (view == nullptr || pane == nullptr || view->document() == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        const QString original = view->document()->text();
        const CapabilityDecision gate = state_.gate(QStringLiteral("formatting"));
        QString formatted;
        QString error;
        if (!diagnostics::formatVerilog(state_.toolchain().veribleFormat.path, original,
                                        &formatted, &error)) {
            std::printf("FORMAT-FAILED backend=%s reason='%s'\n",
                        qPrintable(state_.lspBackendName()), qPrintable(error));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Formatting failed: %1").arg(error), 8000);
            return;
        }
        const bool changed = formatted != original;
        if (changed) {
            // One edit (undoable); KTextEditor groups a full setText() replace.
            view->document()->setText(formatted);
        }
        std::printf("FORMAT backend=%s formatter=%s changed=%d enabled-by='%s' "
                    "in-bytes=%lld out-bytes=%lld file='%s'\n",
                    qPrintable(state_.lspBackendName()),
                    qPrintable(state_.toolchain().veribleFormat.path), changed ? 1 : 0,
                    qPrintable(gate.source), static_cast<long long>(original.toUtf8().size()),
                    static_cast<long long>(formatted.toUtf8().size()), qPrintable(path));
        std::fflush(stdout);
        shell_->statusBar()->showMessage(
            changed ? QCoreApplication::translate("VEdit", "Formatted with verible-verilog-format")
                    : QCoreApplication::translate("VEdit",
                                                  "Already formatted (verible-verilog-format)"),
            5000);
    }

    /// batch-20261116 L2.2: hand the project file-set poller to this wiring so
    /// an explicit Run with an empty set can restart a failed expansion. The
    /// callback is replaced when Open Folder swaps the poller.
    void setRetryProjectExpansion(std::function<void()> retry)
    {
        retryProjectExpansion_ = std::move(retry);
    }

    /// AC-1.6: the request of the project pass. ONE builder, so every trigger
    /// (today: the explicit `project.lint` only, AC-2.4) composes the same file
    /// set, -I/-D, engine and rules.
    /// RM-17: the module tree publishes its ONE derived top scope here (see
    /// src/workspace/top_scope.h). With a top the project pass lints only the
    /// analysed set; every excluded file gets the visible NotAnalyzed mark with
    /// a reason NAMING the top, and one status line says how many were excluded.
    /// Auto (empty top) restores the whole file set and clears those marks.
    void applyTopScope(const QString &top, const QStringList &analysed,
                       const QStringList &excluded)
    {
        // The controller owns the file verdicts: it stores the scope, filters
        // the NEXT project pass to `analysed` and paints every excluded file
        // with the visible NotAnalyzed mark (reason names the top).
        controller_->setTopScope(top, analysed, excluded);
        shell_->setProperty("vedit.diagnostics.topScope", top);
        shell_->setProperty("vedit.diagnostics.topScopeExcluded", excluded.size());
        if (top.isEmpty()) {
            return;
        }
        shell_->addShellDiagnostic(
            QCoreApplication::translate("VEdit",
                                        "%1 file(s) outside the top %2 are not analysed")
                .arg(excluded.size())
                .arg(top));
    }

    diagnostics::LintPassRequest buildLintRequest() const
    {
        diagnostics::LintPassRequest request;
        // A lint pass runs only on files whose language has a LINT analyzer.
        //
        // G1-c: and never on a file whose EVERY module definition lost the
        // catalog's duplicate pick (`vedit.moduleTree.shadowedFiles`, published by
        // the module tree lane's catalog). Handing it to slang would let slang
        // apply its own duplicate rule and pick a SECOND winner - the reported
        // defect, where the compiler resolved a module to a file the tree never
        // chose. The subtraction is the whole point of the shadowed list; with no
        // catalog the set is empty and the request is unchanged.
        const diagnostics::realtime_scope::ShadowedFileSet shadowed =
            diagnostics::realtime_scope::shadowedFileSetFrom(
                shell_->property("vedit.moduleTree.shadowedFiles").toStringList());
        request.files.clear();
        request.files.reserve(projectFiles_.size());
        for (const QString &file : projectFiles_) {
            if (diagnostics::realtime_scope::isShadowedFile(shadowed, file)) {
                continue;
            }
            if (project::LanguageRegistry::instance().supports(file,
                                                               project::Capability::Lint)) {
                request.files.append(file);
            }
        }
        request.includeDirs = projectIncludeDirs_.isEmpty() ? currentSettings_.includeDirs
                                                            : projectIncludeDirs_;
        request.defines = projectDefines_.isEmpty() ? currentSettings_.defines
                                                    : projectDefines_;
        request.engine = static_cast<int>(state_.lintKind());
        request.slangPath = state_.toolchain().slang.path;
        request.veriblePath = state_.toolchain().veribleLint.path;
        request.rulesPath = state_.rulesPath();
        return request;
    }

    /// item 12 / batch-20261115 AC-2.4: the ONLY run. `Project > Lint Project`
    /// (menu/toolbar) is the one trigger of a whole-project pass, and it is the
    /// one path allowed to present the result in a modal dialog.
    void lintProject()
    {
        // G1-a review finding 1: a FAILED expansion is retried before anything
        // else. The dock's failed line promises exactly that ("press Run Project
        // Lint to retry the expansion"), and linting the last set would report
        // verdicts for files the user's new filelist may no longer contain.
        // This is also reachable with a NON-empty stale set (a re-expansion that
        // failed), so the check cannot live inside the empty-set branch below.
        const bool projectKnown = !workspaceRoot_.isEmpty() || !shell_->projectRoot().isEmpty();
        const QVariantMap pollerState =
            shell_->property("vedit.diagnostics.projectFilesState").toMap();
        const QString phase = pollerState.value(QStringLiteral("phase")).toString();
        if (projectKnown && phase == QStringLiteral("failed") && retryProjectExpansion_) {
            retryProjectExpansion_();
            QVariantMap run;
            run.insert(QStringLiteral("started"), false);
            run.insert(QStringLiteral("reason"), QStringLiteral("project-fileset-failed"));
            run.insert(QStringLiteral("phase"), phase);
            run.insert(QStringLiteral("retry"), QStringLiteral("started"));
            run.insert(QStringLiteral("files"), projectFiles_.size());
            shell_->setProperty("vedit.diagnostics.projectLintRun", run);
            std::printf("LINT-PROJECT-FILESET-FAILED loading=%d root='%s' phase='%s' "
                        "retry='started'\n",
                        projectKnown ? 1 : 0, qPrintable(workspaceRoot_), qPrintable(phase));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate(
                    "VEdit", "The project file set failed to expand — retrying the expansion"),
                8000);
            return;
        }
        if (!projectKnown || projectFiles_.isEmpty()) {
            // L2.2: with no file set there is nothing to lint - and the DOCK
            // must keep saying why (it reads the same poller map), not just an
            // 8 s status-bar flash. The Run stays deterministic: the published
            // `projectLintRun` says `started=false`, names the reason and says
            // whether the expansion was restarted.
            QString retry = QStringLiteral("not-needed");
            QVariantMap run;
            run.insert(QStringLiteral("started"), false);
            run.insert(QStringLiteral("reason"), QStringLiteral("no-project-fileset"));
            run.insert(QStringLiteral("phase"), phase);
            run.insert(QStringLiteral("retry"), retry);
            run.insert(QStringLiteral("files"), projectFiles_.size());
            shell_->setProperty("vedit.diagnostics.projectLintRun", run);
            const QString message = projectKnown
                ? QCoreApplication::translate(
                      "VEdit", "The project file set is still loading — try again in a moment")
                : QCoreApplication::translate("VEdit", "No project file set — open a folder first");
            std::printf("LINT-PROJECT-NO-FILES loading=%d root='%s' phase='%s' retry='%s'\n",
                        projectKnown ? 1 : 0, qPrintable(workspaceRoot_), qPrintable(phase),
                        qPrintable(retry));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(message, 8000);
            return;
        }
        // A second explicit click always wins over the pass already running
        // (`cancelProjectPass()` is a TRUE stop: release + running cleared), so
        // the new request starts at once instead of being refused as "already
        // running".
        if (controller_->projectPassRunning()) {
            controller_->cancelProjectPass();
        }
        // F6 ordering fix: the open-file snapshot must be recorded BEFORE the
        // pass starts, or this run lints files the LSP session already covers
        // (the first run used to lint EVERY file because the snapshot was empty).
        controller_->setOpenFiles(modifiedOpenFiles());
        controller_->setNextPassAutomatic(false);
        const diagnostics::LintPassRequest request = buildLintRequest();
        QString error;
        if (!controller_->startProjectPass(request, &error)) {
            std::printf("LINT-PROJECT-FAILED engine=%s reason='%s'\n",
                        qPrintable(state_.lintEngineName()), qPrintable(error));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Project lint did not start: %1").arg(error),
                8000);
            return;
        }
        std::printf("LINT-PROJECT engine=%s files=%d lsp-backend=%s include-dirs=%lld "
                    "defines=%lld skipped-open=%lld non-source=%lld automatic=0\n",
                    qPrintable(state_.lintEngineName()), controller_->projectPassFileCount(),
                    qPrintable(state_.lspBackendName()),
                    static_cast<long long>(request.includeDirs.size()),
                    static_cast<long long>(request.defines.size()),
                    static_cast<long long>(controller_->projectPassSkipped().size()),
                    static_cast<long long>(controller_->projectPassNonSource().size()));
        // RM-11: a request path with no lint analyzer is EXCLUDED and its reason
        // recorded - never parsed as HDL. This is the observable the regression
        // reads (a `.f` manifest must never appear here as a linted file).
        for (const QString &path : controller_->projectPassNonSource()) {
            std::printf("LINT-SKIP path='%s' reason='no lint analyzer for this file type'\n",
                        qPrintable(path));
        }
        std::fflush(stdout);
        shell_->statusBar()->showMessage(
            QCoreApplication::translate("VEdit", "Linting project with %1…")
                .arg(state_.lintEngineName()),
            5000);
    }

    /// The error-severity rows of the LIVE upper layer (R2). Rows of a file that
    /// was edited after the pass are already gone, so a stale verdict can never
    /// reach the dialog.
    QVector<diagnostics::DiagnosticEntry> projectErrors() const
    {
        // `upperLayerDiagnostics()` is the live upper layer ALREADY converted to
        // the display scale (the CLI ABI encodes 0 = error, which the editor's
        // model would otherwise paint as information).
        QVector<diagnostics::DiagnosticEntry> errors;
        for (const diagnostics::DiagnosticEntry &entry : controller_->upperLayerDiagnostics()) {
            if (entry.severity != 1) {
                continue;
            }
            errors.append(entry);
        }
        return errors;
    }

    /// The presentation state, observable for the product-level regression: the
    /// object COUNT cannot detect a re-show of the same hidden dialog, but
    /// `dialogsShown` (incremented by every showDialog()) and `dialogVisible` can.
    void publishPresentation(bool automatic, int rows, const char *mode)
    {
        QVariantMap presentation;
        presentation.insert(QStringLiteral("mode"), QString::fromLatin1(mode));
        presentation.insert(QStringLiteral("automatic"), automatic);
        presentation.insert(QStringLiteral("rows"), rows);
        presentation.insert(QStringLiteral("dialogsShown"), lintNotice_->dialogsShown());
        presentation.insert(QStringLiteral("noticesShown"), lintNotice_->noticesShown());
        presentation.insert(QStringLiteral("noticeVisible"), lintNotice_->noticeVisible());
        presentation.insert(QStringLiteral("dialogVisible"),
                            lintNotice_->dialog() != nullptr
                                && lintNotice_->dialog()->isVisible());
        presentation.insert(QStringLiteral("serial"),
                            QVariant::fromValue<qulonglong>(controller_->projectPassSerial()));
        shell_->setProperty("vedit.diagnostics.presentation", presentation);
    }

    /// Called from the 300 ms poll: react ONCE to a pass that reached a terminal
    /// phase. Automatic passes never open a modal dialog (R3).
    void reactToFinishedPass()
    {
        const quint64 serial = controller_->projectPassSerial();
        if (serial == lastPassSerial_) {
            return;
        }
        lastPassSerial_ = serial;
        const diagnostics::LintPassSnapshot pass = controller_->projectPass();
        if (pass.phase != diagnostics::LintPhase::Finished) {
            if (pass.phase == diagnostics::LintPhase::Failed && shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Project lint failed: %1").arg(pass.message),
                    8000);
            }
            return;
        }
        const QVector<diagnostics::DiagnosticEntry> errors = projectErrors();
        const bool automatic = controller_->lastPassWasAutomatic();
        if (errors.isEmpty()) {
            // A clean (or warning-only) pass has nothing to guide: an empty
            // modal dialog is worse than none, and R3 keeps the auto run silent.
            lintNotice_->clear();
            publishPresentation(automatic, 0, automatic ? "automatic" : "explicit");
            std::printf("LINT-PASS-FINISHED automatic=%d errors=0 files=%d dialogs=%d notice=0 "
                        "stage='%s' message='%s' upper-rows=%d\n",
                        automatic ? 1 : 0, controller_->projectPassFileCount(),
                        lintNotice_->dialogsShown(), qPrintable(pass.stage),
                        qPrintable(pass.message),
                        static_cast<int>(controller_->upperLayerDiagnostics().size()));
            std::fflush(stdout);
            return;
        }
        // batch-20261115 AC-4.4: NO modal dialog, not even for the explicit run.
        // The rows are presented where they live - the Project Lint dock - and
        // the status bar carries the one-line, clickable summary. A modal would
        // block the editor for a pass whose result the user can read at leisure.
        lintNotice_->present(errors, diagnostics::ProjectLintNotice::Mode::Automatic);
        publishPresentation(automatic, static_cast<int>(errors.size()),
                            automatic ? "automatic" : "explicit");
        std::printf("LINT-PASS-FINISHED automatic=%d errors=%d files=%d dialogs=%d notice=%d\n",
                    automatic ? 1 : 0, static_cast<int>(errors.size()),
                    controller_->projectPassFileCount(), lintNotice_->dialogsShown(),
                    lintNotice_->noticeVisible() ? 1 : 0);
        std::fflush(stdout);
    }

private:
    static QStringList splitList(const QString &value)
    {
        QStringList out;
        for (const QString &line : value.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                out.append(trimmed);
            }
        }
        return out;
    }

    vedit::app::EditorPane *activePane() const
    {
        return shell_->editorArea() != nullptr ? shell_->editorArea()->activePane() : nullptr;
    }

    KTextEditor::View *activeView() const
    {
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        vedit::app::EditorPane *pane = activePane();
        return pane != nullptr ? qobject_cast<KTextEditor::View *>(pane->editorWidget()) : nullptr;
#else
        return nullptr;
#endif
    }

    /// Ruling C8 (rev 2, content-based): the pass skips ONLY open documents
    /// whose BUFFER is modified - their bytes on disk are stale, so a disk read
    /// would lint the wrong text. A just-saved file and any other open file that
    /// is unmodified on disk are INCLUDED (analysing them is correct). Every
    /// pane and every tab counts, not just the active one: a dirty background
    /// tab used to be linted from disk and covered with a stale verdict.
    QStringList modifiedOpenFiles() const
    {
        QStringList files;
        if (shell_->editorArea() != nullptr) {
            for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
                if (pane == nullptr) {
                    continue;
                }
                for (int index = 0; index < pane->tabCount(); ++index) {
                    const QString path = pane->tabFilePath(index);
                    if (!path.isEmpty() && pane->isTabModified(index)
                        && !files.contains(path)) {
                        files.append(path);
                    }
                }
            }
        }
        return files;
    }

    /// Every open document (all panes, all tabs) - the observability side of R2.
    QStringList allOpenFiles() const
    {
        QStringList files;
        if (shell_->editorArea() != nullptr) {
            for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
                if (pane == nullptr) {
                    continue;
                }
                for (const QString &path : pane->openFilePaths()) {
                    if (!path.isEmpty() && !files.contains(path)) {
                        files.append(path);
                    }
                }
            }
        }
        return files;
    }

public:
    /// The LSP binary the controller must be configured with. `state_` only
    /// learns a path from a settings apply; before the first one - and a root
    /// change can happen before it - the resolved toolchain path is the truth.
    /// (Falling back to an empty state path made the initial file-set adoption
    /// configure the session with NO server, so the rename E2E's session never
    /// became ready.)
    QString configuredLspPath() const
    {
        const QString selected = state_.lspServerPath();
        return selected.isEmpty() ? toolchain_.verible.path : selected;
    }

    /// Open Folder: the lane used to pin `projectRoot`/`projectFiles_` once at
    /// install time, so both explicit and automatic lint kept running over the
    /// OLD project after a folder switch. This re-pins the root, cancels any
    /// in-flight pass, drops the old project's rows and reconfigures the session.
    void followProjectRoot(const QString &root)
    {
        workspaceRoot_ = root;
        // N4/nit: the bubble describes a row of the OLD project. It used to stay
        // visible across the switch (the active view does not change, so
        // adoptHoverView() early-returned). Clear the cache and invalidate an
        // in-flight probe so a late answer cannot re-show it under the new root.
        hoverProbeSerial_++;
        hoverProbePending_ = false;
        hideHoverPopup();
        hoverFilePath_.clear();
        hoverRule_.clear();
        hoverLine_ = -1;
        hoverEntry_ = diagnostics::DiagnosticEntry{};
        publishHoverState();
        projectFiles_.clear();
        projectIncludeDirs_.clear();
        projectDefines_.clear();
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS)
        // U6: the old project's discovered rules file must not be used for the
        // new root; the new file-set poll publishes the new one when it lands.
        settings::setCurrentDiscoveredRulesPath(QString());
#endif
        // RM-2: drop the OLD project's `-I`/`-D` from the backends NOW. The
        // next file set is still being expanded, and a session started in that
        // window (or by a path that never adopts) must fall back to the
        // tools-page values instead of running with the previous project's
        // environment. This re-selects the SAME backend/server, so no panel or
        // controller is recreated.
        const bool envCleared = state_.setProjectEnvironment(QStringList{}, QStringList{});
        if (controller_->projectPassRunning()) {
            controller_->cancelProjectPass();
        }
        lintNotice_->clear();
        controller_->clearProjectDiagnostics();
        controller_->setOpenFiles(QStringList());
        controller_->invalidateRealtimeSession();
        controller_->setConfiguration(configuredLspPath(), workspaceRoot_, state_.rulesPath());
        controller_->setFileSet(QStringList{});
        publishProjectState();
        publishProjectEnvironment(envCleared);
        // RM-9: the file tree's "From filelist" group and the status map
        // describe the OLD project. Publish an empty set with a NEW revision so
        // no file of the previous root can be drawn under the new one.
        shell_->setProperty("vedit.diagnostics.projectFiles", QStringList{});
        shell_->setProperty("vedit.diagnostics.projectFilesRevision",
                            QVariant::fromValue<qulonglong>(++projectSetRevision_));
        std::printf("PROJECT-ROOT-CHANGED root='%s' files=0 (waiting for the new file set)\n",
                    qPrintable(root));
        std::printf("PROJECT-ROOT-ENV root='%s' source=cleared include-dirs=%d defines=%d env=''\n",
                    qPrintable(root), 0, 0);
        std::fflush(stdout);
    }

    /// The new root's file set arrived: re-pin the request, the backends' `-I`/`-D`
    /// and the realtime LSP session. The environment is re-pinned BEFORE the
    /// restart, because `setProjectEnvironment()` stops the running server and the
    /// restart below is what brings it back with the new `-I`/`-D`.
    void adoptProjectFiles(const QStringList &files, const QStringList &includeDirs,
                           const QStringList &defines)
    {
        setProjectFiles(files, includeDirs, defines);
        const bool envPinned = state_.setProjectEnvironment(includeDirs, defines);
        // Recompute both scopes from the new revision before starting a server.
        // The watcher starts it over open documents and their direct children;
        // the expanded project list supplies its separate symbol index.
        controller_->invalidateRealtimeSession();
        controller_->setConfiguration(configuredLspPath(), workspaceRoot_, state_.rulesPath());
        controller_->setFileSet(files);
        publishProjectState();
        publishProjectEnvironment(envPinned);
        QStringList argv;
        for (const QString &directory : includeDirs) {
            argv.append(QStringLiteral("-I%1").arg(directory));
        }
        for (const QString &define : defines) {
            argv.append(QStringLiteral("-D%1").arg(define));
        }
        std::printf("PROJECT-ROOT-FILES root='%s' files=%d\n", qPrintable(workspaceRoot_),
                    static_cast<int>(files.size()));
        std::printf("PROJECT-ROOT-ENV root='%s' source=project pinned=%d include-dirs=%d "
                    "defines=%d env='%s'\n",
                    qPrintable(workspaceRoot_), envPinned ? 1 : 0,
                    static_cast<int>(state_.projectIncludeDirs().size()),
                    static_cast<int>(state_.projectDefines().size()),
                    qPrintable(argv.join(QLatin1Char(' '))));
        std::fflush(stdout);
        // RM-9: publish the EXPANDED project set (directory scan + every
        // imported `.f` list). It is the set the project PASS lints and the
        // per-file status marks describe; the realtime LSP session is a separate,
        // bounded set (current file + level-1 closure, L1.1). The file tree
        // renders the members that live outside its directory scan in a "From
        // filelist" group. One publisher, one revision, so no consumer can read a
        // half-updated set.
        shell_->setProperty("vedit.diagnostics.projectFiles", files);
        shell_->setProperty("vedit.diagnostics.projectFilesRevision",
                            QVariant::fromValue<qulonglong>(++projectSetRevision_));
        // batch-20261115 AC-2.4: adopting a file set starts NO whole-project pass.
        // The whole-project pass is the explicit `Project > Lint Project`
        // command (`lintProject`), so opening a folder, switching a root,
        // importing a filelist, choosing a top or saving a file costs no lint
        // run. What the adoption DOES do is restart an ALREADY-RUNNING realtime
        // session with the new environment (B1 above) - never start one.
    }

    /// The environment the backends will hand the next session (observable, so
    /// the switch regression can assert A->B without reading implementation).
    void publishProjectEnvironment(bool pinned)
    {
        QVariantMap environment;
        environment.insert(QStringLiteral("root"), workspaceRoot_);
        environment.insert(QStringLiteral("includeDirs"), state_.projectIncludeDirs());
        environment.insert(QStringLiteral("defines"), state_.projectDefines());
        environment.insert(QStringLiteral("settingsPath"), settingsPath_);
        // `pinned` is the RESULT of pushing these values through the backend's
        // selectBackend/selectEngine seam - the call that writes the -I/-D the
        // next session is built from. False = this build has no LSP backend.
        environment.insert(QStringLiteral("pinned"), pinned);
        shell_->setProperty("vedit.diagnostics.projectEnv", environment);
    }

    void publishProjectState()
    {
        QVariantMap project;
        project.insert(QStringLiteral("root"), workspaceRoot_);
        project.insert(QStringLiteral("files"), projectFiles_.size());
        project.insert(QStringLiteral("pending"),
                       projectFiles_.isEmpty() && !workspaceRoot_.isEmpty());
        project.insert(QStringLiteral("openFiles"), allOpenFiles());
        project.insert(QStringLiteral("modifiedOpenFiles"), modifiedOpenFiles());
        // RM-2: the file set and the `-I`/`-D` the lint/LSP side actually holds
        // for it - the panel and the analysis expand the same inputs now.
        project.insert(QStringLiteral("includeDirs"), projectIncludeDirs_);
        project.insert(QStringLiteral("defines"), projectDefines_);
        // RM-11: what the last lint request excluded because this build has no
        // analyzer for that file type (a `.f` manifest, a note, a build script).
        project.insert(QStringLiteral("nonSourceSkipped"), controller_->projectPassNonSource());
        shell_->setProperty("vedit.diagnostics.project", project);
    }

private:
    void beginNavigation(const QString &action)
    {
        KTextEditor::View *view = activeView();
        vedit::app::EditorPane *pane = activePane();
        if (view == nullptr || pane == nullptr) {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "%1 needs a KTextEditor view").arg(action),
                5000);
            return;
        }
        hideHoverPopup();
        const KTextEditor::Cursor cursor = view->cursorPosition();
        const QString file = pane->currentFilePath();
        pendingAction_ = action;
        pendingRequest_ = action == QStringLiteral("definition")
            ? controller_->requestDefinition(file, cursor.line(), cursor.column())
            : controller_->requestReferences(file, cursor.line(), cursor.column());
        if (pendingRequest_ == 0) {
            // The ABI refused (usually: no session yet). Silently doing nothing is
            // the one answer a user cannot act on, so name the real reason.
            std::printf("NAV-REFUSED action=%s backend=%s file='%s' line=%d column=%d\n",
                        qPrintable(action), qPrintable(state_.lspBackendName()), qPrintable(file),
                        cursor.line(), cursor.column());
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate(
                    "VEdit", "%1 was refused by %2 — the backend session is not ready yet")
                    .arg(action, state_.lspBackendName()),
                8000);
            return;
        }
        navTimer_->start();
    }

    void pollNavigation()
    {
        if (pendingRequest_ == 0) {
            navTimer_->stop();
            return;
        }
        const diagnostics::LspPolled polled = controller_->pollNavigation();
        if (polled.pending) {
            return;
        }
        pendingRequest_ = 0;
        navTimer_->stop();
        if (polled.failed) {
            std::printf("NAV-FAILED action=%s backend=%s reason='%s'\n",
                        qPrintable(pendingAction_), qPrintable(state_.lspBackendName()),
                        qPrintable(polled.error));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(polled.error, 8000);
            return;
        }
        if (pendingAction_ == QStringLiteral("hover")) {
            std::printf("HOVER-RESPONSE backend=%s kind=hover bytes=%lld empty=%d\n",
                        qPrintable(state_.lspBackendName()),
                        static_cast<long long>(polled.hoverText.toUtf8().size()),
                        polled.hoverText.isEmpty() ? 1 : 0);
            std::fflush(stdout);
            if (polled.hoverText.isEmpty()) {
                hideHoverPopup();
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "No hover from %1 here")
                        .arg(state_.lspBackendName()),
                    5000);
                return;
            }
            renderHover(polled.hoverText);
            return;
        }
        if (pendingAction_ == QStringLiteral("rename")) {
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
            // AC-2.1 (batch 2): the answer is APPLIED, not counted. The editor
            // lane's engine edits the open documents (ONE undo step per file) and
            // writes targets that are not open; a refusal names the file and the
            // reason and changes nothing.
            QList<vedit::editor::WorkspaceTextEdit> edits;
            edits.reserve(polled.edits.size());
            for (const diagnostics::LspTextEdit &raw : polled.edits) {
                vedit::editor::WorkspaceTextEdit edit;
                edit.filePath = QUrl(raw.uri).toLocalFile();
                edit.startLine = raw.startLine;
                edit.startColumn = raw.startCharacter;
                edit.endLine = raw.endLine;
                edit.endColumn = raw.endCharacter;
                edit.newText = raw.newText;
                edits.append(edit);
            }
            const vedit::editor::WorkspaceEditReport report =
                vedit::editor::applyWorkspaceEdit(edits);
            if (!report.ok) {
                std::printf("RENAME-FAILED backend=%s edits=%lld reason='%s'\n",
                            qPrintable(state_.lspBackendName()),
                            static_cast<long long>(polled.edits.size()),
                            qPrintable(report.error));
                std::fflush(stdout);
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Rename failed: %1").arg(report.error),
                    9000);
                return;
            }
            for (const QString &line : report.perFile) {
                std::printf("RENAME-FILE %s\n", qPrintable(line));
            }
            std::printf("RENAME-APPLIED backend=%s edits=%d files=%d in-editor=%d on-disk=%d\n",
                        qPrintable(state_.lspBackendName()), report.editCount, report.fileCount,
                        report.openDocumentCount, report.writtenFileCount);
            std::fflush(stdout);
            shell_->statusBar()->showMessage(report.summary(), 9000);
#else
            std::printf("RENAME-FAILED backend=%s reason='this build has no KTextEditor editor'\n",
                        qPrintable(state_.lspBackendName()));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Rename needs the KTextEditor editor"), 9000);
#endif
            return;
        }
        if (pendingAction_ == QStringLiteral("definition")) {
            if (polled.locations.isEmpty()) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "No definition found"), 5000);
                return;
            }
            const diagnostics::LspLocation &location = polled.locations.first();
            const QString target = QUrl(location.uri).toLocalFile();
            std::printf("DEFINITION backend=%s file='%s' line=%d column=%d\n",
                        qPrintable(state_.lspBackendName()), qPrintable(target),
                        location.startLine + 1, location.startCharacter + 1);
            std::fflush(stdout);
            vedit::workspace::jump::jumpTo(target, location.startLine + 1,
                                           location.startCharacter + 1);
            return;
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        // AC-2.2 (batch 2): every location becomes a jumpable row, and an empty
        // answer opens the list's real empty state instead of a bare count.
        showReferencesList(polled.locations);
#else
        std::printf("REFERENCES backend=%s count=%lld list=unavailable\n",
                    qPrintable(state_.lspBackendName()),
                    static_cast<long long>(polled.locations.size()));
        std::fflush(stdout);
        shell_->statusBar()->showMessage(
            QCoreApplication::translate("VEdit", "%1 reference(s) from %2")
                .arg(polled.locations.size())
                .arg(state_.lspBackendName()),
            6000);
#endif
    }

#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
    /// AC-2.2: the references list of the symbol under the cursor. Rows come from
    /// the editor lane's builder (deduplicated, ordered, snippet from the buffer
    /// when the file is open); the dialog itself only renders them and jumps on
    /// double-click through the ONE application-wide jump seam.
    void showReferencesList(const QVector<diagnostics::LspLocation> &locations)
    {
        QList<vedit::editor::WorkspaceLocation> list;
        list.reserve(locations.size());
        for (const diagnostics::LspLocation &raw : locations) {
            vedit::editor::WorkspaceLocation location;
            location.filePath = QUrl(raw.uri).toLocalFile();
            location.line = raw.startLine;
            location.column = raw.startCharacter;
            list.append(location);
        }
        const QString symbol = symbolUnderCursor();
        const QList<vedit::editor::ReferenceRow> rows = vedit::editor::buildReferenceRows(list);
        if (referencesDialog_ == nullptr) {
            auto *dialog = new ReferencesDialog(shell_);
            QObject::connect(dialog->treeWidget(), &QTreeWidget::itemDoubleClicked, this,
                             [this](QTreeWidgetItem *item, int) {
                                 if (item == nullptr || referencesDialog_ == nullptr) {
                                     return;
                                 }
                                 const int index = item->data(0, Qt::UserRole).toInt();
                                 const QList<vedit::editor::ReferenceRow> rows =
                                     referencesDialog_->rows();
                                 if (index < 0 || index >= rows.size()) {
                                     return;
                                 }
                                 const vedit::editor::ReferenceRow row = rows.at(index);
                                 std::printf("REFERENCES-JUMP row=%d file='%s' line=%d column=%d\n",
                                             index, qPrintable(row.filePath), row.line + 1,
                                             row.column + 1);
                                 std::fflush(stdout);
                                 vedit::workspace::jump::jumpTo(row.filePath, row.line + 1,
                                                                row.column + 1);
                             });
            referencesDialog_ = dialog;
        }
        referencesDialog_->setAnswer(symbol, rows);
        referencesDialog_->show();
        referencesDialog_->raise();
        referencesDialog_->activateWindow();
        std::printf("REFERENCES-LIST backend=%s count=%d empty=%d first='%s' header='%s'\n",
                    qPrintable(state_.lspBackendName()), static_cast<int>(rows.size()),
                    rows.isEmpty() ? 1 : 0,
                    rows.isEmpty() ? "" : qPrintable(rows.first().location()),
                    qPrintable(referencesDialog_->headerText()));
        std::fflush(stdout);
        if (rows.isEmpty()) {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "No references found for %1").arg(
                    symbol.isEmpty() ? QCoreApplication::translate("VEdit", "this symbol") : symbol),
                6000);
        } else {
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "%1 reference(s) from %2")
                    .arg(rows.size())
                    .arg(state_.lspBackendName()),
                6000);
        }
    }

    /// The identifier under the cursor (empty when the cursor is not on one).
    QString symbolUnderCursor() const
    {
        KTextEditor::View *view = activeView();
        if (view == nullptr || view->document() == nullptr) {
            return QString();
        }
        const KTextEditor::Cursor cursor = view->cursorPosition();
        const QString line = view->document()->line(cursor.line());
        const auto isIdentifier = [](QChar character) {
            return character.isLetterOrNumber() || character == QLatin1Char('_')
                   || character == QLatin1Char('$');
        };
        int begin = std::min(cursor.column(), static_cast<int>(line.size()));
        int end = begin;
        while (begin > 0 && isIdentifier(line.at(begin - 1))) {
            --begin;
        }
        while (end < line.size() && isIdentifier(line.at(end))) {
            ++end;
        }
        return line.mid(begin, end - begin);
    }
#endif // VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR

    void refreshLiveState()
    {
        // AC-3.2/3.3/3.4 (card 2): the badge's text is owned by publishBadge() -
        // the one place that renders `diagnostics::badgeText()` onto the REAL
        // QLabel and from there publishes `BADGE-TEXT`. The old inline renderer
        // (effective backend only, no request/reason) is gone: it was exactly the
        // line that made a refused switch invisible.
        attachWaiverUi();
        auto applyGate = [this](QAction *action, const QString &name) {
            if (action == nullptr) {
                return;
            }
            const CapabilityDecision decision = state_.gate(name);
            action->setEnabled(decision.enabled);
            action->setToolTip(decision.reason);
            action->setStatusTip(decision.reason);
        };
        // AC-2.1/AC-2.2 observability: the live session snapshot, so a caller can
        // wait for "the backend is ready" instead of guessing from the capability
        // table (which is published before the server has answered).
        const diagnostics::SessionSnapshot session = controller_->session();
        QVariantMap sessionMap;
        sessionMap.insert(QStringLiteral("state"), static_cast<int>(session.state));
        sessionMap.insert(QStringLiteral("ready"), session.state == diagnostics::BackendState::Ready);
        sessionMap.insert(QStringLiteral("filesExpected"), session.filesExpected);
        sessionMap.insert(QStringLiteral("filesOpened"), session.filesOpened);
        sessionMap.insert(QStringLiteral("diagnostics"), session.diagnostics);
        sessionMap.insert(QStringLiteral("generation"),
                          QVariant::fromValue<qulonglong>(session.generation));
        sessionMap.insert(QStringLiteral("backend"), state_.lspBackendName());
        sessionMap.insert(QStringLiteral("message"), session.message);
        // batch-20261118 AC-3: the bounded restart count is state the panel,
        // a probe or the verifier can read without guessing.
        sessionMap.insert(QStringLiteral("restarts"), session.restarts);
        sessionMap.insert(QStringLiteral("failed"),
                          session.state == diagnostics::BackendState::Failed);
        shell_->setProperty("vedit.diagnostics.session", sessionMap);

        applyGate(definitionAction_, QStringLiteral("definition"));
        applyGate(referencesAction_, QStringLiteral("references"));
        applyGate(renameAction_, QStringLiteral("rename"));
        applyGate(formatAction_, QStringLiteral("formatting"));
        applyGate(hoverAction_, QStringLiteral("hover"));
        publishGates();

        // item 10 + T9: the file tree's dot source, published through the shell
        // property seam. ONE controller call decides WHAT changed: a structural
        // change (finished pass, lane update, scope) republishes the whole map;
        // a single document edit publishes only the files whose verdict moved,
        // so typing never rebuilds the project view. The revision pair is the
        // consumer's token: `viewRevision` = full map, `fileStatusDeltaRevision`
        // = the delta to apply on top.
        const diagnostics::LivePublish publish = controller_->takeLivePublish();
        if (publish.full) {
            lastPublishedViewRevision_ = publish.viewRevision;
            QVariantMap statuses;
            for (auto it = publish.statuses.constBegin(); it != publish.statuses.constEnd(); ++it) {
                statuses.insert(it.key(), it.value());
            }
            shell_->setProperty("vedit.diagnostics.fileStatus", statuses);
            shell_->setProperty("vedit.diagnostics.viewRevision",
                                QVariant::fromValue<qulonglong>(publish.viewRevision));
            // AC-N3 E6 observability: the rows the Diagnostics panel really
            // renders (the same merged view the panel reads). The waiver action
            // is planned from a row of THIS list, so its presence is the
            // precondition of the waiver evidence. A full publish is a
            // structural change (a pass finished), so the row list is expected
            // here - the per-file path below never builds it.
            const QVector<diagnostics::DisplayDiagnostic> displayed =
                controller_->displayDiagnostics();
            int withOptionName = 0;
            for (const diagnostics::DisplayDiagnostic &row : displayed) {
                if (!row.entry.optionName.isEmpty()) {
                    ++withOptionName;
                }
            }
            const diagnostics::FileSetReport report = controller_->fileSetReport();
            std::printf("DIAG-ROWS count=%d option-names=%d upper=%d lower=%d slang-req=%d "
                        "slang-fail=%d slang-msg='%s' revision=%llu\n",
                        static_cast<int>(displayed.size()), withOptionName, publish.upperCount,
                        publish.mergedCount, report.slangRequested, report.slangFailures,
                        qPrintable(report.slangMessage),
                        static_cast<unsigned long long>(publish.viewRevision));
            for (const diagnostics::DisplayDiagnostic &row : displayed) {
                std::printf("DIAG-ROW file='%s' line=%d column=%d severity=%d code='%s' "
                            "option='%s' source='%s' origin=%d\n",
                            qPrintable(row.entry.file), row.entry.line + 1, row.entry.column + 1,
                            row.entry.severity, qPrintable(row.entry.code),
                            qPrintable(row.entry.optionName), qPrintable(row.entry.source),
                            static_cast<int>(row.origin));
            }
            std::fflush(stdout);
        } else if (!publish.changedFiles.isEmpty()) {
            QVariantMap delta;
            for (auto it = publish.statuses.constBegin(); it != publish.statuses.constEnd(); ++it) {
                delta.insert(it.key(), it.value());
            }
            shell_->setProperty("vedit.diagnostics.fileStatusDelta", delta);
            shell_->setProperty("vedit.diagnostics.fileStatusDeltaRevision",
                                QVariant::fromValue<qulonglong>(++fileStatusDeltaRevision_));
            // The per-file path is observable too (the DIAG-ROWS line above is a
            // structural-change witness; this one names the delta instead of
            // silently doing nothing).
            std::printf("DIAG-ROWS-DELTA files=%d merged=%d revision=%llu\n",
                        static_cast<int>(publish.changedFiles.size()), publish.mergedCount,
                        static_cast<unsigned long long>(publish.viewRevision));
            std::fflush(stdout);
        }
        if (publish.full || !publish.changedFiles.isEmpty()) {
            // The revision every status consumer polls (file tree, Project
            // panel). It moves for a delta too - that is what tells those
            // caches to APPLY the delta instead of rebuilding (T9).
            shell_->setProperty("vedit.diagnostics.statusRevision",
                                QVariant::fromValue<qulonglong>(++statusRevisionCounter_));
            // R2 observability: the files whose upper layer is live. Their set
            // moves with the view (an edit drops a file from the upper layer),
            // so it is published with a change, not on every poll.
            shell_->setProperty("vedit.diagnostics.upperFiles", controller_->upperLayerFiles());
        }

        // RM-10: watch EVERY pane's view (not only the active one) so a split
        // pane answers the hover; the inner KatePart widget is created lazily,
        // so ChildAdded re-enters the same install.
        if (shell_->editorArea() != nullptr) {
            for (vedit::app::EditorPane *pane : shell_->editorArea()->panes()) {
                if (pane == nullptr) {
                    continue;
                }
                if (auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget())) {
                    watchViewTree(view);
                }
            }
        }
        // item 9: follow the ACTIVE view for the diagnostic hover. A tab switch
        // RE-PARENTS the popup; a destroyed view clears the cache. A bubble
        // anchored to a view of ANOTHER pane is a split-pane hover: the poll
        // must not steal it (that would hide it within 400 ms).
        KTextEditor::View *active = activeView();
        auto *popupView = qobject_cast<KTextEditor::View *>(
            hoverPopup_.isNull() ? nullptr : hoverPopup_->parentWidget());
        const bool splitPaneHover = hoverShowsDiagnostic_ && popupView != nullptr
            && popupView != active && paneForView(popupView) != activePane();
        if (!splitPaneHover) {
            adoptHoverView(active);
        }
        // RM-10: keep the bubble honest when the text moves under a stationary
        // mouse (keyboard scrolling). The cached view point must still resolve to
        // the cached row; otherwise the bubble is hidden. A STALE row keeps the
        // N4 observable path (rows cleared elsewhere) instead of hiding silently.
        if (hoverShowsDiagnostic_ && !hoverPopup_.isNull() && hoverPopup_->isVisible()) {
            if (popupView == nullptr || popupView->document() == nullptr) {
                hideHoverPopup();
            } else if (!hoverRowIsStale()) {
                const KTextEditor::Cursor at = popupView->coordinatesToCursor(hoverViewportPoint_);
                const diagnostics::DiagnosticEntry *entry =
                    diagnostics::diagnosticAt(
                        controller_->mergedDiagnosticsForFile(hoverFilePath_), at.line(),
                        at.column());
                const QString rule = entry == nullptr
                    ? QString()
                    : (entry->code.isEmpty() ? entry->source : entry->code);
                if (entry == nullptr || rule != hoverRule_ || entry->line != hoverLine_) {
                    hideHoverPopup();
                } else {
                    placePopup(hoverPopup_, hoverViewportPoint_); // scroll-safe
                }
            }
        }
        publishHoverState();
        publishProjectState();
        // T9/AC-3.5: the badge facts follow the ACTIVE FILE, so switching from a
        // .sv to a .vhd file changes the language route without a settings
        // change (the UI half that renders it is T14's).
        publishBadge();
        runWaiverProbeIfRequested();
    }

protected:
    /// item 9: a mouse move over the editor asks the merged view what is under
    /// the pointer. Nothing is requested from the LSP session here: a diagnostic
    /// hover is answered from the rows the editor already shows.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event == nullptr) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::ChildAdded) {
            // RM-10: KatePart's inner KateViewInternal (the REAL mouse target)
            // can appear after the view; install on the new widget subtree.
            auto *child = static_cast<QChildEvent *>(event)->child();
            if (child != nullptr && child->isWidgetType()) {
                watchViewTree(static_cast<QWidget *>(child));
            }
            return QObject::eventFilter(watched, event);
        }
        auto *widget = qobject_cast<QWidget *>(watched);
        // Never hijack the popup's own mouse (the :Fix」 button must stay
        // clickable): a move over the bubble is not a new hover.
        if (widget != nullptr && !hoverPopup_.isNull()
            && (widget == hoverPopup_.data() || hoverPopup_->isAncestorOf(widget))) {
            return QObject::eventFilter(watched, event);
        }
        KTextEditor::View *view = widget != nullptr ? viewForWidget(widget) : nullptr;
        if (view == nullptr) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseMove) {
            auto *move = static_cast<QMouseEvent *>(event);
            // RM-10: QMouseEvent::pos() is in the RECEIVING widget's coordinates
            // (KateViewInternal), NOT the view's: map it before resolving the
            // cursor. This is the point the old filter never saw at all.
            const QPoint viewPoint = widget == static_cast<QWidget *>(view)
                                         ? move->pos()
                                         : widget->mapTo(view, move->pos());
            handleDiagnosticHover(view, viewPoint);
        } else if (event->type() == QEvent::Wheel) {
            // A scroll moves the text under the bubble: never leave it dangling.
            if (hoverShowsDiagnostic_) {
                hideHoverPopup();
                publishHoverState();
            }
        } else if (event->type() == QEvent::Leave && watched == hoverView_.data()) {
            if (hoverShowsDiagnostic_) {
                hideHoverPopup();
            }
        }
        // ALWAYS false: the filter OBSERVES, it never consumes. Normal selection,
        // KTextEditor's own tooltips and Ctrl+click keep working from the same
        // point inside the squiggle.
        return QObject::eventFilter(watched, event);
    }

private:

    vedit::app::ApplicationShell *shell_ = nullptr;
    diagnostics::DiagnosticsController *controller_ = nullptr;
    BackendSwitchState state_;
    diagnostics::ToolchainReport toolchain_;
    QString workspaceRoot_;
    QString settingsPath_; ///< the tools-page store this lane follows
    std::function<void()> restartAnalysis_;
    /// L2.2: the live project file-set poller's retry seam (empty when this
    /// build has no FFI-backed project model).
    std::function<void()> retryProjectExpansion_;
    DiagnosticsBackendSettings currentSettings_;
    /// The last store values/warnings exactly as the Rust loader delivered them
    /// (the badge warnings, the configured waiver files and the autofix mode all
    /// read THIS, so no consumer re-reads the file on its own).
    QHash<QString, QString> settingsValues_;
    /// `lint.verible.autofix` as stored ("no"/"patch"/.../"" = no flag).
    QString autofixMode_;
    /// The last BADGE line printed, so the 400 ms poll logs only real changes.
    QString lastBadgeLine_;
    /// The last RENDERED badge text (`QLabel::text()`), and when it was last
    /// printed by the VEDIT_BADGE_TRACE heartbeat (E2).
    QString lastBadgeText_;
    qint64 lastBadgeTextPrintMs_ = 0;

    QLabel *badge_ = nullptr;
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
    ReferencesDialog *referencesDialog_ = nullptr;
#endif
    QTimer *liveTimer_ = nullptr;
    QTimer *navTimer_ = nullptr;
    QTimer *passTimer_ = nullptr;
    QAction *definitionAction_ = nullptr;
    QAction *referencesAction_ = nullptr;
    QAction *renameAction_ = nullptr;
    QAction *formatAction_ = nullptr;
    QAction *hoverAction_ = nullptr;
    // A QPointer: closing a tab destroys the view (and a child popup), and the
    // 400 ms poll must not touch a dead pointer when it follows the next one.
    QPointer<diagnostics::DiagnosticHoverPopup> hoverPopup_;
    QPointer<QObject> hoverView_;
    QMetaObject::Connection hoverViewDestroyed_;
    QMetaObject::Connection hoverDocumentEdited_;
    /// RM-10: every widget the hover filter is installed on (the pane views and
    /// their descendants, incl. KatePart's inner KateViewInternal). QPointer so a
    /// destroyed tab never leaves a dangling entry to re-install on.
    QList<QPointer<QWidget>> hoverWatched_;
    bool hoverShowsDiagnostic_ = false;
    bool hoverProbePending_ = false;
    bool hoverDiagnosticFixable_ = false;
    quint64 hoverProbeSerial_ = 0;
    QPoint hoverViewportPoint_;
    QString hoverFilePath_;
    QString hoverRule_;
    int hoverLine_ = -1;
    diagnostics::DiagnosticEntry hoverEntry_;
    QString pendingAction_;
    quint64 pendingRequest_ = 0;

    // R3 + item 12 + item 9 state.
    diagnostics::ProjectLintNotice *lintNotice_ = nullptr;
    diagnostics::AutofixEngine autofix_;
    quint64 lastPassSerial_ = 0;
    quint64 lastPublishedViewRevision_ = 0;
    /// Monotone tokens the status consumers compare against their caches (T9).
    quint64 fileStatusDeltaRevision_ = 0;
    quint64 statusRevisionCounter_ = 0;

    QStringList projectFiles_;
    QStringList projectIncludeDirs_;
    QStringList projectDefines_;
    /// RM-9: bumps every time `vedit.diagnostics.projectFiles` changes, so the
    /// file tree re-reads the set only when it actually moved.
    quint64 projectSetRevision_ = 0;
    /// RM-9: a full project-open pass was requested while another pass was
    /// running; the pass timer starts it when that one is terminal. Never
    /// replaces an explicit Lint Project with an automatic pass.

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    SettingsWatcher *watcher_ = nullptr;
    diagnostics::WaiverActions waiver_;
    QHash<quint64, WaiverJob> waiverJobs_;
    QTimer *waiverTimer_ = nullptr;
    /// The env-gated E6 probe fires once (see runWaiverProbeIfRequested()).
    bool waiverProbeDone_ = false;
#endif
    /// The real panel tree the waiver context menu is attached to (null until
    /// the dock exists; attachWaiverUi() is idempotent). A QPointer: a layout
    /// reset can destroy the dock's content, and the next poll re-attaches
    /// instead of dereferencing a dead widget.
    QPointer<QTreeWidget> issuesTree_;
};

} // namespace
#endif // VEDIT_DIAGNOSTICS_HAVE_APP_SHELL

namespace {

// U3r2-F2: the format command's default shortcut has ONE source — the editor
// action table in src/editor/kernel/lsp_actions.cpp, which is also the value
// the editor context menu falls back to when the wired shell action cannot be
// resolved. A configuration without the editor kernel has neither a format path
// nor a context menu, so the command carries no default key there.
QKeySequence formatDefaultShortcut()
{
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
    return vedit::editor::editorActionShortcut(vedit::editor::EditorAction::Format);
#else
    return QKeySequence();
#endif
}

} // namespace

bool installProjectLintContents(QWidget *host, vedit::app::ApplicationShell &shell,
                                diagnostics::DiagnosticsController *controller)
{
    if (host == nullptr) {
        // AC-4.5: a shell without the content host is NOT a crash. One explicit
        // diagnostic says the whole-project result has no panel; the rest of the
        // lane (the status-bar notice, `Project > Lint Project`) keeps working.
        shell.addShellDiagnostic(QCoreApplication::translate(
            "VEdit",
            "The Project Lint dock is not available in this build (no content host): the "
            "whole-project result has no panel, so `Project > Lint Project` only reports "
            "in the status bar"));
        return false;
    }
    // One panel per host: a second install call must not stack a second content
    // widget (the dock's own empty-state page is replaced by the event filter).
    if (host->findChild<diagnostics::ProjectLintPanel *>() != nullptr) {
        return true;
    }
    auto *contents = new diagnostics::ProjectLintPanel(controller, &shell, host);
    auto *hostLayout = qobject_cast<QVBoxLayout *>(host->layout());
    if (hostLayout == nullptr) {
        hostLayout = new QVBoxLayout(host);
        hostLayout->setContentsMargins(0, 0, 0, 0);
    }
    hostLayout->addWidget(contents);
    return true;
}

void installDiagnostics(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }

    const diagnostics::ToolchainSearchContext toolchainContext =
        diagnostics::applicationToolchainContext();
    diagnostics::ToolchainReport toolchain = diagnostics::resolveToolchain(toolchainContext);
    if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
        std::printf("TOOLCHAIN %s %s local=[%s]\n", qPrintable(toolchain.statusLine()),
                    qPrintable(toolchain.auxiliaryStatusLine()),
                    qPrintable(toolchainContext.localDirs.join(QLatin1Char(','))));
    }

    auto lsp = diagnostics::createFfiLspSessionBackend();
    auto slang = diagnostics::createFfiSlangBackend(toolchain.slang.path);
    auto *lspRaw = lsp.get();
    auto *slangRaw = slang.get();
    auto *holder = new ControllerHolder(
        std::make_unique<diagnostics::DiagnosticsController>(std::move(lsp), std::move(slang)),
        &shell);
    auto *controller = holder->controller();
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    // T8: the project-wide CLI pass (engine selection lives in the request).
    controller->setProjectLintBackend(diagnostics::createFfiProjectLintBackend());
#endif
    controller->setToolchainReport(toolchain);
    shell.setProperty("vedit.diagnosticsController", QVariant::fromValue<QObject *>(holder));

    const QString projectRoot = options.projectRoot;
    controller->setConfiguration(toolchain.verible.path, projectRoot);
    // The module tree lane is installed BEFORE this one (src/workspace/
    // integration.cpp), so it cannot read the report directly. Publish the
    // slang path this lane resolved — the same `toolchain.slang.path` the
    // project lint pass uses below — through the shell-property seam the
    // capability/session probes already use. The module tree reads it when it
    // starts an elaboration; nothing is re-resolved.
    shell.setProperty("vedit.diagnostics.toolchainSlang", toolchain.slang.path);
    shell.addShellDiagnostic(toolchain.userMessage());
    if (!toolchain.missing().isEmpty() && qEnvironmentVariableIsSet("VEDIT_TOOLCHAIN_TRACE")) {
        qDebug("diagnostics: %s", qPrintable(toolchain.probeDetail()));
    }

    const auto factory = [controller]() {
        auto *panel = new diagnostics::DiagnosticsPanel(controller);
        controller->refresh();
        return panel;
    };
    shell.panels()->setFactory(QStringLiteral("outline"), factory);

    if (QDockWidget *dock = shell.panels()->dock(QStringLiteral("outline"))) {
        QWidget *placeholder = dock->widget();
        if (placeholder != nullptr
            && placeholder->objectName().startsWith(QStringLiteral("panel-placeholder-"))) {
            dock->setWidget(factory());
            delete placeholder;
        }
    }

    // --- T11 wiring: settings-driven backend/lint-engine switch -------------
    auto *watcher = new ActiveFileWatcher(&shell, &shell, controller);
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
    new AnnotationBridge(&shell, &shell, controller);
#endif

    // batch-20261117 / AC-1: the Preferences dialog writes the store at
    // `i18n::settingsFilePath(stateDir)` — Qt's AppConfigLocation when no
    // `--state-dir` was given. This lane has to read THAT file. The old
    // `applicationDirPath()` fallback pointed inside the AppImage's read-only
    // squashfs, where no settings.toml can exist, so SettingsWatcher never
    // fired and the tools-page backend / include dirs / defines / rules were
    // never applied on a normal launch. One path feeds the watcher, the
    // project expansion inputs and the rename lane: all four reads fixed here.
    const QString settingsPath = vedit::app::i18n::settingsFilePath(options.stateDir);

    auto *wiring = new BackendWiring(
        &shell, &shell, controller, lspRaw, slangRaw, toolchain, projectRoot, settingsPath,
        [watcher, controller]() { watcher->reanalyzeNow(); });
    // RM-17: install the top-scope sink once the wiring exists. The module tree
    // publishes its derived scope from the SAME catalog run that produced its
    // tree (header-only seam: no link edge between the two lanes).
    vedit::workspace::topScopeSink() =
        [wiring](const QString &top, const QStringList &analysed,
                 const QStringList &excluded) {
            wiring->applyTopScope(top, analysed, excluded);
        };

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    // RM-2: ONE expansion factory for both the install-time poll and every
    // root switch. It reads the panel's inputs from the shell-property seam at
    // call time, so the poller for root B expands with B's imported `.f` lists,
    // B's variables and the settings store - exactly what the project panel
    // uses. The settings-path capture is BY VALUE: the factory outlives
    // `installDiagnostics()` through the root-change connection.
    const auto makeProjectPoller = [&shell, controller, settingsPath,
                                    wiring](const QString &root) {
        const ProjectExpansionInputs inputs =
            projectExpansionInputs(shell, root, settingsPath);
        return new ProjectFileSetPoller(
            &shell, &shell, controller, root, inputs,
            [wiring](const QStringList &files, const QStringList &includeDirs,
                     const QStringList &defines) {
                // The same sink the root-switch poller uses: one setFileSet per
                // file set, not the sink's call plus the poller's second one.
                wiring->adoptProjectFiles(files, includeDirs, defines);
            });
    };
    auto *poller = makeProjectPoller(projectRoot);
    // L2.2: Run with an empty set can restart a FAILED expansion (never a
    // running one). The root-switch lambda below replaces both the poller and
    // this callback, so it can never point at a deleted poller.
    wiring->setRetryProjectExpansion([poller]() { poller->retryExpansion(); });
    // HIGH (review finding 3): the lane must FOLLOW Open Folder. The file set,
    // the session root, the backends' -I/-D and every upper-layer row are
    // re-pinned; the old poller (with a possibly in-flight job) is destroyed and
    // replaced for the new root.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [wiring, poller, makeProjectPoller](const QString &root) mutable {
                         wiring->followProjectRoot(root);
                         delete poller;
                         poller = makeProjectPoller(root);
                         wiring->setRetryProjectExpansion(
                             [poller]() { poller->retryExpansion(); });
                     });
    (void)poller;
#else
    (void)projectRoot;
#endif

    // batch-20261115 AC-4.2..4.5: the whole-project result lives in its OWN
    // bottom dock, whose content host the editor lane creates
    // (`ApplicationShell::projectLintHost()`, panel id `project-lint`). This lane
    // installs content into that host and never creates a dock, so the layout
    // persists through the shell's own saveState/restoreState. The host-missing
    // branch (one diagnostic, no crash) lives in `installProjectLintContents`,
    // which the acceptance test also drives directly with a null host.
    shell.setProperty("vedit.diagnostics.projectLintPanel",
                      installProjectLintContents(shell.projectLintHost(), shell, controller));

    if (shell.commands() != nullptr) {
        const auto addCommand = [&shell](const QString &id, const QString &text,
                                         const QString &menu, const QKeySequence &shortcut,
                                         int order) {
            vedit::app::CommandSpec spec;
            spec.id = id;
            spec.text = text;
            spec.menu = menu;
            spec.defaultShortcut = shortcut;
            spec.order = order;
            QAction *action = shell.commands()->add(spec);
            return action != nullptr ? action : shell.commands()->action(id);
        };

        // UX batch 2 (item 7): every command text below is user-visible (Project
        // menu + the editor context menu shares the same spellings), so it goes
        // through the translator. Command ids, shortcuts and handlers are
        // unchanged.
        QAction *definition = addCommand(QStringLiteral("diagnostics.definition"),
                                         QCoreApplication::translate("VEdit", "Go to &Definition"),
                                         QStringLiteral("project"), QKeySequence(Qt::Key_F12), 60);
        QAction *references = addCommand(QStringLiteral("diagnostics.references"),
                                         QCoreApplication::translate("VEdit", "Find &References"),
                                         QStringLiteral("project"),
                                         QKeySequence(QStringLiteral("Shift+F12")), 61);
        QAction *rename = addCommand(QStringLiteral("diagnostics.rename"),
                                     QCoreApplication::translate("VEdit", "&Rename Symbol..."),
                                     QStringLiteral("project"),
                                     QKeySequence(Qt::Key_F2), 62);
        QAction *format = addCommand(QStringLiteral("diagnostics.format"),
                                     QCoreApplication::translate("VEdit", "&Format Document"),
                                     QStringLiteral("project"),
                                     // U3r2-F2: ONE source of truth — the editor
                                     // action's default (lsp_actions.cpp), which the
                                     // context menu also shows as its fallback. It is
                                     // free in the registry (the old colliding value,
                                     // Ctrl+Alt+F, belongs to the Files-panel toggle).
                                     formatDefaultShortcut(), 63);
        // T16 / AC-3.5: the hover request path the editor's context menu uses.
        // It is gated by the LIVE capability row, so it is enabled exactly while
        // the connected backend advertises hoverProvider.
        QAction *hover = addCommand(QStringLiteral("diagnostics.hover"),
                                    QCoreApplication::translate("VEdit", "Show &Hover"),
                                    QStringLiteral("project"), QKeySequence(), 66);
        QAction *lint = addCommand(QStringLiteral("project.lint"),
                                   QCoreApplication::translate("VEdit", "&Lint Project"),
                                   QStringLiteral("project"), QKeySequence(), 51);
        QAction *capabilities = addCommand(QStringLiteral("diagnostics.capabilities"),
                                           QCoreApplication::translate("VEdit", "Show Backend &Capabilities"),
                                           QStringLiteral("project"), QKeySequence(), 64);
        QAction *reload = addCommand(QStringLiteral("diagnostics.reloadSettings"),
                                     QCoreApplication::translate("VEdit", "Reload Backend Settings"),
                                     QStringLiteral("project"), QKeySequence(), 65);
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        // AC-5.1/5.6/5.8 (card 2): the keyboard/command-registry path to the same
        // waiver action the panel's context menu offers (it waives the SELECTED
        // row). Offered only when this build links the Rust service, because
        // without it no waiver can be written.
        QAction *waive = addCommand(QStringLiteral("diagnostics.waive"),
                                    QCoreApplication::translate("VEdit", "&Waive Diagnostic…"),
                                    QStringLiteral("project"),
                                    QKeySequence(QStringLiteral("Ctrl+Alt+W")), 67);
#endif

        if (definition != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.definition"),
                                         [wiring]() { wiring->requestDefinition(); });
        }
        if (references != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.references"),
                                         [wiring]() { wiring->requestReferences(); });
        }
        if (rename != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.rename"),
                                         [wiring]() { wiring->requestRename(); });
        }
        if (format != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.format"),
                                         [wiring]() { wiring->formatDocument(); });
        }
        if (hover != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.hover"),
                                         [wiring]() { wiring->requestHover(); });
        }
        if (lint != nullptr) {
            shell.commands()->setHandler(QStringLiteral("project.lint"),
                                         [wiring]() { wiring->lintProject(); });
        }
        if (capabilities != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.capabilities"),
                                         [wiring]() { wiring->dumpCapabilities(); });
        }
        if (reload != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.reloadSettings"),
                                         [wiring]() { wiring->reloadSettings(); });
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        if (waive != nullptr) {
            shell.commands()->setHandler(QStringLiteral("diagnostics.waive"),
                                         [wiring]() { wiring->waiveSelectedDiagnostic(); });
        }
#endif
        wiring->bindActions(definition, references, rename, format, hover);

        // AC-1.3 / AC-N3 E4 (card 2): ONE user-visible command per dock. This
        // lane used to register a second `view.panel.diagnostics` entry that
        // opened the SAME "outline" dock under a different name ("Diagnostics"),
        // so the View menu named one dock twice. The dock and its command live in
        // src/app (dock title "Outline", `view.panel.outline`, Ctrl+Alt+O); this
        // lane no longer registers a competing view command.
        // (The old Ctrl+Alt+Shift+D alias is gone with it; the doc table in
        // docs/USER_GUIDE.md that lists it is a docs-sync item, not product code.)

        // AC-1.3 / AC-N3 E4 (card 2): the ONE command that toggles the Outline
        // dock, read back from the LIVE registry: its label carries the dock's
        // own title (`PanelRegistry::title()`), its shortcut is unchanged, and no
        // second command for the same dock exists. The evidence line cannot
        // drift from what the user sees, because it is the registry's value.
        if (QAction *outline = shell.commands()->action(QStringLiteral("view.panel.outline"))) {
            const QString title = shell.panels() != nullptr
                ? shell.panels()->title(QStringLiteral("outline"))
                : QString();
            QString label = outline->text();
            label.remove(QLatin1Char('&'));
            std::printf("VIEW-PANEL id='view.panel.outline' label='%s' dock-title='%s' "
                        "shortcut='%s' title-equals-label=%d duplicate-command=%d\n",
                        qPrintable(label), qPrintable(title),
                        qPrintable(outline->shortcut().toString(QKeySequence::PortableText)),
                        label == title ? 1 : 0,
                        shell.commands()->action(QStringLiteral("view.panel.diagnostics"))
                                != nullptr
                            ? 1
                            : 0);
            std::fflush(stdout);
        }

        vedit::app::CommandSpec reparseSpec;
        reparseSpec.id = QStringLiteral("project.reparse");
        reparseSpec.text = QCoreApplication::translate("VEdit", "Re&parse Project");
        reparseSpec.menu = QStringLiteral("project");
        reparseSpec.order = 50;
        shell.commands()->add(reparseSpec);
        shell.commands()->setHandler(reparseSpec.id, [controller]() { controller->reparse(); });
    }
}

/// RM-9: the testable seam to the controller that installDiagnostics() created
/// (see the header). The holder is a child of the shell; the controller itself
/// is owned by it, exactly as the panel sees it.
diagnostics::DiagnosticsController *diagnosticsControllerForShell(
    const vedit::app::ApplicationShell &shell)
{
    // ControllerHolder deliberately carries no Q_OBJECT (it is a lifetime
    // holder), so findChild<T>() cannot be instantiated for it. The shell's
    // direct children contain it; identify it by type instead.
    for (QObject *child : shell.children()) {
        if (auto *holder = dynamic_cast<ControllerHolder *>(child)) {
            return holder->controller();
        }
    }
    return nullptr;
}

/// L2.1: the soft timeout in force. The default is the frozen 300 s; the
/// environment override exists so a test can observe the slow/backoff
/// transition without waiting five minutes (`VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS`,
/// 0 is a valid immediate soft timeout).
int projectExpandSoftTimeoutMs()
{
    bool ok = false;
    const int overrideMs =
        qEnvironmentVariableIntValue("VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS", &ok);
    if (ok && overrideMs >= 0) {
        return overrideMs;
    }
    return kProjectExpandSoftTimeoutMs;
}

} // namespace vedit::workspace

#else

namespace vedit::workspace {

diagnostics::DiagnosticsController *diagnosticsControllerForShell(
    const vedit::app::ApplicationShell &)
{
    return nullptr;
}

bool installProjectLintContents(QWidget *, vedit::app::ApplicationShell &,
                                diagnostics::DiagnosticsController *)
{
    // No app shell in this build: there is no dock and no host to install into.
    return false;
}

} // namespace vedit::workspace

#endif // VEDIT_DIAGNOSTICS_HAVE_APP_SHELL
