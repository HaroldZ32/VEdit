#include "diagnostics_integration.h"

#include "../top_scope.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)

#include "autofix.h"
#include "diagnostic_hover.h"
#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "lsp_backend.h"
#include "project_lint_notice.h"
#include "project_lint_trigger.h"
#include "symbol_scan.h"
#include "slang_backend.h"
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
    void reanalyzeNow()
    {
        lastText_.clear(); // force the next poll to treat the buffer as changed
        analyze();
    }

private:
    static QString bufferText(QWidget *widget)
    {
        if (widget == nullptr) {
            return QString();
        }
#if defined(VEDIT_DIAGNOSTICS_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(widget)) {
            return view->document() != nullptr ? view->document()->text() : QString();
        }
#endif
        if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
            return text->toPlainText();
        }
        return QString();
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
            lastText_.clear();
            return;
        }
        const QString text = bufferText(widget);
        if (text.isEmpty() && lastPath_ == path) {
            return;
        }
        if (path != lastPath_) {
            lastPath_ = path;
            lastText_ = text;
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
        if (text != lastText_) {
            lastText_ = text;
            // Fallback for a widget whose document has no signal (or a change
            // made before the subscription): still invalidate the active file.
            controller_->markFileEdited(path);
            debounce_->start();
        }
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
        const QString text = bufferText(pane->editorWidget());
        if (path.isEmpty() || text.isEmpty()) {
            return;
        }
        // RM-11: a document is analysed only when this build has an ANALYZER
        // for its language. A `.f` manifest, a `.md` note, a `.tcl` script has
        // no language entry, so it is never handed to a Verilog server - the
        // user's "syntax error on my filelist" was this call analysing the
        // manifest as HDL. The outcome is a STATE (published below), not a
        // skip log line and never a diagnostic row.
        const project::LanguageRegistry &languages = project::LanguageRegistry::instance();
        if (!languages.supports(path, project::Capability::Diagnostics)) {
            QVariantMap availability;
            availability.insert(QStringLiteral("path"), path);
            availability.insert(QStringLiteral("available"), false);
            availability.insert(QStringLiteral("reason"),
                                languages.unavailableReason(path, project::Capability::Diagnostics));
            shell_->setProperty("vedit.diagnostics.analysisAvailability", availability);
            return;
        }
        QFile file(path);
        QString disk;
        if (file.open(QIODevice::ReadOnly)) {
            disk = QString::fromUtf8(file.readAll());
        }
        QVariantMap availability;
        availability.insert(QStringLiteral("path"), path);
        availability.insert(QStringLiteral("available"), true);
        availability.insert(QStringLiteral("reason"), QString());
        shell_->setProperty("vedit.diagnostics.analysisAvailability", availability);
        controller_->analyzeFile(path, text, disk != text);
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    diagnostics::DiagnosticsController *controller_ = nullptr;
    QTimer *tick_ = nullptr;
    QTimer *debounce_ = nullptr;
    QString lastPath_;
    QString lastText_;
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
    inputs.filelists = published.value(QStringLiteral("filelists")).toStringList();
    inputs.variables = published.value(QStringLiteral("variables")).toStringList();
    const QString settingsPath = published.value(QStringLiteral("settingsPath")).toString();
    if (!settingsPath.isEmpty()) {
        inputs.settingsPath = settingsPath;
    }
    return inputs;
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
        beginExpansion(inputs);
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

private:
    /// RM-9: one expansion with exactly `inputs`. Re-entrant by design: the
    /// RUNNING job's completion calls it again when the panel's inputs moved.
    void beginExpansion(const ProjectExpansionInputs &inputs)
    {
        inputs_ = inputs;
        settingsPath_ = inputs.settingsPath;
        if (service_ == nullptr || job_ != 0) {
            return; // the running job's completion re-checks the inputs
        }
        const QByteArray root = projectRoot_.toUtf8();
        const QByteArray settingsPath = inputs.settingsPath.toUtf8();
        const BorrowedStringList filelists(inputs.filelists);
        const BorrowedStringList variables(inputs.variables);
        // R2/R3: the file set must be the C7 project model - the `.f` expansion
        // UNION a recursive scan of the folder - not the legacy filelist-only
        // `vd_project_parse`. Measured: with the legacy call a folder of .sv
        // files and no `.f` produced ZERO files, so "Lint Project" (and every
        // save-triggered pass) had nothing to lint and reported "No project file
        // set" - the feature was dead for exactly the projects a new user opens.
        //
        // RM-2: `vd_project_expand_v2` with the PANEL's inputs (imported `.f`
        // lists, variables table, settings store). The file set, the `-I`/`-D`
        // and the discovered rules path are then exactly the panel's - one
        // project model, not two.
        job_ = vd_project_expand_v2(service_, root.constData(), filelists.pointer(),
                                    settingsPath.isEmpty() ? nullptr : settingsPath.constData(),
                                    variables.pointer());
        std::printf("PROJECT-EXPAND-INPUTS root='%s' filelists=%d variables=%d settings='%s'\n",
                    qPrintable(projectRoot_), static_cast<int>(inputs.filelists.size()),
                    static_cast<int>(inputs.variables.size()),
                    settingsPath.isEmpty() ? "<none>" : qPrintable(inputs.settingsPath));
        std::fflush(stdout);
        if (job_ == 0) {
            qWarning("diagnostics: project expansion was rejected: %s",
                     qPrintable(serviceError()));
            return;
        }
        ticks_ = 0;
        timer_->start(50);
    }

    /// The panel's inputs changed (a `.f` was imported/removed, a variable was
    /// set): follow it. No-op while a job runs - its completion re-checks.
    void watchInputs()
    {
        if (shell_ == nullptr) {
            return;
        }
        const ProjectExpansionInputs next =
            projectExpansionInputs(*shell_, projectRoot_, settingsPath_);
        if (next.filelists == inputs_.filelists && next.variables == inputs_.variables) {
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
        if (++ticks_ > 200) {
            timer_->stop();
            qWarning("diagnostics: the project job did not finish within 10 s");
            job_ = 0;
            return;
        }
        VdProjectInfo info{};
        const int status = vd_project_poll(service_, job_, &info);
        if (status == VD_STATUS_PENDING) {
            return;
        }
        timer_->stop();
        job_ = 0;
        if (status != VD_STATUS_READY) {
            qWarning("diagnostics: the project job failed: %s", qPrintable(serviceError()));
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
            if (next.filelists != inputs_.filelists || next.variables != inputs_.variables) {
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
    int ticks_ = 0;
    ProjectExpansionInputs inputs_;
    QString settingsPath_;
    QStringList files_;
    QStringList includeDirs_;
    QStringList defines_;
};

/// T11: read <stateDir>/settings.toml through the SAME C ABI the Preferences
/// dialog writes with, and report the key/value map when the file changes. The
/// mtime poll makes the online switch independent of another lane's class; the
/// read itself is the ABI's asynchronous job (no disk IO on the GUI stack).
class SettingsWatcher final : public QObject {
public:
    using ChangedCallback = std::function<void(const QHash<QString, QString> &)>;

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
            return;
        }
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
        vd_settings_free(&settings);
        if (changed_) {
            changed_(values);
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
            // RM-9: a full pass deferred because another pass was running (a
            // re-adoption mid-pass) starts as soon as that one is terminal. It is
            // started BEFORE the presentation poll, so it owns its own terminal
            // serial and is presented as automatic (notice, no dialog).
            if (fullPassPending_ && !controller_->projectPassRunning()) {
                fullPassPending_ = false;
                startFullProjectAnalysis();
            }
            controller_->refresh(); // advances the project lint pass (T8)
            reactToFinishedPass();
        });
        passTimer_->start();

        // item 12 / R3: the presentation policy. The modal dialog is reserved
        // for an explicit `project.lint`; the automatic (save-triggered) pass
        // only ever gets the clickable status-bar notice.
        lintNotice_ = new diagnostics::ProjectLintNotice(shell_, shell_->statusBar(), this);

        // R3: the ONE save seam of the editor area feeds the automatic pass.
        saveTrigger_ = new diagnostics::ProjectLintTrigger(controller_, this);
        saveTrigger_->setRequestBuilder([this]() { return buildLintRequest(); });
        // RM-9 save policy: a save re-lints exactly the file that reached disk;
        // the other files keep their verdicts (replaceUpperLayer keeps rows of
        // files the pass did not lint), so a save costs one lint run, not N.
        saveTrigger_->setIncrementalRequestBuilder(
            [this](const QString &savedFile) { return buildLintRequestFor(savedFile); });
        saveTrigger_->setOpenFilesProvider([this]() { return modifiedOpenFiles(); });
        saveTrigger_->setResultSink([this](bool started, const QString &error) {
            if (started || error.isEmpty() || shell_->statusBar() == nullptr) {
                return;
            }
            // nit (review-b3-r3): during startup the 50 ms project-model poll
            // has not delivered yet, so "no project file set" would blame the
            // user for a project that is merely still loading. The explicit
            // `project.lint` path already distinguishes the two; the automatic
            // one stays silent instead of crying wolf on the first save.
            if (projectFiles_.isEmpty()
                && (!workspaceRoot_.isEmpty() || !shell_->projectRoot().isEmpty())) {
                return;
            }
            shell_->statusBar()->showMessage(
                QCoreApplication::translate("VEdit", "Automatic project lint did not start: %1")
                    .arg(error),
                6000);
        });
        if (shell_->editorArea() != nullptr) {
            shell_->editorArea()->setOnDocumentSaved(
                [this](const QString &path, bool) { saveTrigger_->noteSaved(path); });
        }
        autofix_.setLintBinary(toolchain_.veribleLint.path);

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
        watcher_ = new SettingsWatcher(this, settingsPath_,
                                       [this](const QHash<QString, QString> &values) {
                                           applySettings(values);
                                       });
#else
        (void)settingsPath;
#endif
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

    bool applySettings(const QHash<QString, QString> &values)
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

        QElapsedTimer timer;
        timer.start();
        QString error;
        if (!state_.apply(settings, toolchain_, &error)) {
            QVariantMap failed;
            failed.insert(QStringLiteral("ok"), false);
            failed.insert(QStringLiteral("reason"), error);
            shell_->setProperty("vedit.diagnostics.switch", failed);
            std::printf("SWITCH-FAILED from=%s requested-lsp=%s requested-lint=%s reason='%s'\n",
                        qPrintable(currentSettings_.lspBackend),
                        qPrintable(settings.lspBackend), qPrintable(settings.lintEngine),
                        qPrintable(error));
            std::fflush(stdout);
            if (shell_->statusBar() != nullptr) {
                shell_->statusBar()->showMessage(
                    QCoreApplication::translate("VEdit", "Backend switch failed: %1").arg(error),
                    8000);
            }
            return false;
        }
        currentSettings_ = settings;
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
        // stale row can never be shown under the new backend's name. This is
        // BOTH layers: `setFileSet({})` below clears the lower (LSP/slang) rows
        // through reparse(), and the project pass rows/dots are cleared here -
        // without this, the previous engine's upper layer stayed visible under
        // the new engine's status line (N3).
        controller_->clearProjectDiagnostics();
        controller_->setFileSet(QStringList{});
        if (restartAnalysis_) {
            restartAnalysis_();
        }
        if (controller_->fileSet().isEmpty() && !projectFiles_.isEmpty()) {
            controller_->setFileSet(projectFiles_);
        }
        publishGates();
        refreshLiveState();
        dumpStatus("SWITCHED");
        QVariantMap switched;
        switched.insert(QStringLiteral("ok"), true);
        switched.insert(QStringLiteral("backend"), state_.lspBackendName());
        switched.insert(QStringLiteral("lint"), state_.lintEngineName());
        shell_->setProperty("vedit.diagnostics.switch", switched);
        std::printf("SWITCH elapsed-ms=%lld backend=%s lint=%s basis=%s\n", timer.elapsed(),
                    qPrintable(state_.lspBackendName()), qPrintable(state_.lintEngineName()),
                    qPrintable(state_.capabilities().basis));
        std::fflush(stdout);
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
        // item 9 / MEDIUM c: do NOT run the engine on the mouse-move path. The
        // content is shown at once and the measured fixability arrives through
        // an asynchronous probe; the popup is updated when it lands.
        diagnostics::HoverContent pending =
            diagnostics::describeDiagnostic(*entry, false,
                QCoreApplication::translate("VEdit", "checking whether the engine can fix this…"));
        pending.fixPending = true;
        diagnostics::DiagnosticHoverPopup *popup = hoverPopup();
        popup->setContent(pending);
        placePopup(popup, viewportPoint);
        publishHoverState();
        std::printf("DIAG-HOVER file='%s' line=%d column=%d severity=%d rule='%s' pending=1 "
                    "probe-count=%lld\n",
                    qPrintable(path), entry->line + 1, entry->column + 1, entry->severity,
                    qPrintable(rule), static_cast<long long>(autofix_.asyncProbeCount()));
        std::fflush(stdout);
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

    /// AC-1.6: the request of the next project pass. ONE builder for the
    /// explicit (`project.lint`) and the automatic (save-triggered) path, so the
    /// two can never disagree about the file set, -I/-D, engine or rules.
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
        request.files.clear();
        request.files.reserve(projectFiles_.size());
        for (const QString &file : projectFiles_) {
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

    /// RM-9: the save-triggered INCREMENTAL pass - exactly the file that just
    /// reached disk. Everything else (engine, -I/-D, rules) comes from the same
    /// builder, so the full and the incremental path cannot disagree about the
    /// environment.
    diagnostics::LintPassRequest buildLintRequestFor(const QString &savedFile) const
    {
        diagnostics::LintPassRequest request = buildLintRequest();
        request.files.clear();
        if (!savedFile.isEmpty()
            && project::LanguageRegistry::instance().supports(savedFile,
                                                               project::Capability::Lint)) {
            request.files.append(savedFile);
        }
        return request;
    }

    /// RM-9 coverage policy: a project OPEN is a FULL analysis. One automatic
    /// pass over the whole expanded set (directory scan + imported `.f` lists)
    /// gives every file a verdict - including files the user never opens - and
    /// skips only dirty buffers (their bytes on disk are stale, so the pass
    /// would lint the wrong text). Async, single-flight and cancellable like
    /// every other pass; reactToFinishedPass() presents the automatic result as
    /// a notice, never a modal dialog.
    void startFullProjectAnalysis()
    {
        if (projectFiles_.isEmpty()) {
            return;
        }
        // RM-9: NEVER cancel a pass that is already running from here. A second
        // adoption can land while the previous pass (or the user's explicit
        // Lint Project!) is still running; replacing it here would silently
        // downgrade an explicit run to an automatic one (no dialog, wrong
        // presentation). Remember that the current set still needs its full
        // pass; the pass timer starts it when the running pass is terminal.
        if (controller_->projectPassRunning()) {
            fullPassPending_ = true;
            std::printf("LINT-PROJECT-OPEN deferred (a pass is running)\n");
            std::fflush(stdout);
            return;
        }
        fullPassPending_ = false;
        controller_->setOpenFiles(modifiedOpenFiles());
        controller_->setNextPassAutomatic(true);
        QString error;
        if (!controller_->startProjectPass(buildLintRequest(), &error)) {
            // Startup may race the toolchain/settings resolve. The request is
            // recorded (A-15) and a save or an explicit run re-runs it; a
            // status-bar complaint here would blame the user for a pass that
            // simply had not started yet.
            std::printf("LINT-PROJECT-OPEN not-started reason='%s'\n", qPrintable(error));
            std::fflush(stdout);
            return;
        }
        std::printf("LINT-PROJECT-OPEN engine=%s files=%d skipped-open=%lld automatic=1\n",
                    qPrintable(state_.lintEngineName()), controller_->projectPassFileCount(),
                    static_cast<long long>(controller_->projectPassSkipped().size()));
        std::fflush(stdout);
    }

    /// item 12: the EXPLICIT run (menu/toolbar). Only this path may present the
    /// result in a modal dialog; the automatic one gets the status-bar notice.
    void lintProject()
    {
        if (projectFiles_.isEmpty()) {
            // Distinguish "still loading" from "no project at all": the 50 ms
            // project-model poll may simply not have delivered yet, and sending
            // the user to File > Open Folder would be the wrong remedy.
            const bool projectKnown = !workspaceRoot_.isEmpty() || !shell_->projectRoot().isEmpty();
            const QString message = projectKnown
                ? QCoreApplication::translate(
                      "VEdit", "The project file set is still loading — try again in a moment")
                : QCoreApplication::translate("VEdit", "No project file set — open a folder first");
            std::printf("LINT-PROJECT-NO-FILES loading=%d root='%s'\n", projectKnown ? 1 : 0,
                        qPrintable(workspaceRoot_));
            std::fflush(stdout);
            shell_->statusBar()->showMessage(message, 8000);
            return;
        }
        // RM-9: an explicit click always wins over a running automatic pass
        // (the project-open pass or a save pass). cancelProjectPass() is a TRUE
        // stop (release + running cleared), so the explicit pass starts at once
        // instead of being refused as "already running".
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
            std::printf("LINT-PASS-FINISHED automatic=%d errors=0 files=%d dialogs=%d notice=0\n",
                        automatic ? 1 : 0, controller_->projectPassFileCount(),
                        lintNotice_->dialogsShown());
            std::fflush(stdout);
            return;
        }
        lintNotice_->present(errors, automatic ? diagnostics::ProjectLintNotice::Mode::Automatic
                                               : diagnostics::ProjectLintNotice::Mode::Explicit);
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
        // RM-9: a pending open-pass belongs to the OLD project.
        fullPassPending_ = false;
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
        controller_->setConfiguration(configuredLspPath(), workspaceRoot_, state_.rulesPath());
        controller_->setFileSet(QStringList{});
        controller_->refresh();
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
    /// and the LSP session. The environment is re-pinned BEFORE `setFileSet()`,
    /// because that call starts the session the switch exists to correct.
    void adoptProjectFiles(const QStringList &files, const QStringList &includeDirs,
                           const QStringList &defines)
    {
        setProjectFiles(files, includeDirs, defines);
        const bool envPinned = state_.setProjectEnvironment(includeDirs, defines);
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
        // imported `.f` list). It is the SAME set the pass lints and the LSP
        // session opens; the file tree renders the members that live outside its
        // directory scan in a "From filelist" group. One publisher, one
        // revision, so no consumer can read a half-updated set.
        shell_->setProperty("vedit.diagnostics.projectFiles", files);
        shell_->setProperty("vedit.diagnostics.projectFilesRevision",
                            QVariant::fromValue<qulonglong>(++projectSetRevision_));
        // RM-9 coverage policy: a project OPEN is a full analysis, so every file
        // of the set (not only the opened one) gets a verdict.
        startFullProjectAnalysis();
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
        const auto capabilities = state_.capabilities();
        const QString basis = capabilities.basis.isEmpty() ? QStringLiteral("unknown")
                                                           : capabilities.basis;
        if (badge_ != nullptr) {
            badge_->setText(QCoreApplication::translate("VEdit", "LSP backend=%1 · lint=%2 · %3")
                                .arg(state_.lspBackendName(), state_.lintEngineName(), basis));
        }
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
        shell_->setProperty("vedit.diagnostics.session", sessionMap);

        applyGate(definitionAction_, QStringLiteral("definition"));
        applyGate(referencesAction_, QStringLiteral("references"));
        applyGate(renameAction_, QStringLiteral("rename"));
        applyGate(formatAction_, QStringLiteral("formatting"));
        applyGate(hoverAction_, QStringLiteral("hover"));
        publishGates();

        // item 10: the file tree's dot source, published through the shell
        // property seam. The map is rebuilt ONLY when the merged view changed,
        // so a 400 ms poll never rescans the project.
        const quint64 revision = controller_->viewRevision();
        if (revision != lastPublishedViewRevision_
            || !shell_->property("vedit.diagnostics.fileStatus").isValid()) {
            lastPublishedViewRevision_ = revision;
            QVariantMap statuses;
            const QHash<QString, int> map = controller_->fileStatuses();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                statuses.insert(it.key(), it.value());
            }
            shell_->setProperty("vedit.diagnostics.fileStatus", statuses);
            shell_->setProperty("vedit.diagnostics.viewRevision",
                                QVariant::fromValue<qulonglong>(revision));
        }
        // R2 observability: the files whose upper layer is live. Published on
        // every poll (not only on a revision change) so a reader can never see a
        // list that predates the invalidation it just observed.
        shell_->setProperty("vedit.diagnostics.upperFiles", controller_->upperLayerFiles());

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
    DiagnosticsBackendSettings currentSettings_;

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
    diagnostics::ProjectLintTrigger *saveTrigger_ = nullptr;
    diagnostics::ProjectLintNotice *lintNotice_ = nullptr;
    diagnostics::AutofixEngine autofix_;
    quint64 lastPassSerial_ = 0;
    quint64 lastPublishedViewRevision_ = 0;

    QStringList projectFiles_;
    QStringList projectIncludeDirs_;
    QStringList projectDefines_;
    /// RM-9: bumps every time `vedit.diagnostics.projectFiles` changes, so the
    /// file tree re-reads the set only when it actually moved.
    quint64 projectSetRevision_ = 0;
    /// RM-9: a full project-open pass was requested while another pass was
    /// running; the pass timer starts it when that one is terminal. Never
    /// replaces an explicit Lint Project with an automatic pass.
    bool fullPassPending_ = false;

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    SettingsWatcher *watcher_ = nullptr;
#endif
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

    QString settingsPath = options.stateDir;
    if (settingsPath.isEmpty()) {
        settingsPath = QCoreApplication::applicationDirPath();
    }
    settingsPath += QStringLiteral("/settings.toml");

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
    // HIGH (review finding 3): the lane must FOLLOW Open Folder. The file set,
    // the session root, the backends' -I/-D and every upper-layer row are
    // re-pinned; the old poller (with a possibly in-flight job) is destroyed and
    // replaced for the new root.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [wiring, poller, makeProjectPoller](const QString &root) mutable {
                         wiring->followProjectRoot(root);
                         delete poller;
                         poller = makeProjectPoller(root);
                     });
    (void)poller;
#else
    (void)projectRoot;
#endif

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
        wiring->bindActions(definition, references, rename, format, hover);

        // T131: the user must FIND the diagnostics view.
        vedit::app::CommandSpec panelSpec;
        panelSpec.id = QStringLiteral("view.panel.diagnostics");
        panelSpec.text = QCoreApplication::translate("VEdit", "&Diagnostics");
        panelSpec.menu = QStringLiteral("view");
        // RM-5c (review M1): Ctrl+Alt+D is KDE's "show desktop" global shortcut
        // and is consumed by the desktop before the application sees it, so the
        // shipped default must be a sequence neither the desktop nor KTextEditor
        // claims. Static on purpose: the effective binding is one value
        // everywhere (manual, Shortcuts page, binding dump), not a rule.
        panelSpec.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+Shift+D"));
        panelSpec.order = 160;
        shell.commands()->add(panelSpec);
        shell.commands()->setHandler(panelSpec.id, [&shell]() {
            shell.panels()->showPanel(QStringLiteral("outline"), &shell);
        });

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

} // namespace vedit::workspace

#else

namespace vedit::workspace {

diagnostics::DiagnosticsController *diagnosticsControllerForShell(
    const vedit::app::ApplicationShell &)
{
    return nullptr;
}

} // namespace vedit::workspace

#endif // VEDIT_DIAGNOSTICS_HAVE_APP_SHELL
