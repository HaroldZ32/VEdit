#include "rename_integration.h"

#include "rename_preview.h"

#include "../project/language_registry.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

namespace vedit::workspace {

namespace {

// The HDL extensions the language registry owns. The registry is the ONE source
// of the extension table (it already mirrors the Rust registry), so the gate
// never carries a second list.
QStringList hdlExtensions()
{
    const QVector<project::LanguageSpec> &languages =
        project::LanguageRegistry::instance().languages();
    for (const project::LanguageSpec &spec : languages) {
        if (spec.id == QStringLiteral("systemverilog")) {
            QStringList extensions;
            extensions.reserve(spec.extensions.size());
            for (const QString &extension : spec.extensions) {
                extensions.append(QLatin1Char('.') + extension);
            }
            return extensions;
        }
    }
    return {};
}

bool isHdlFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const project::LanguageSpec *spec =
        project::LanguageRegistry::instance().languageFor(path);
    return spec != nullptr && spec->id == QStringLiteral("systemverilog");
}

} // namespace

RenameEntryGate renameEntryGate(const RenameEntryInput &input)
{
    RenameEntryGate gate;
    if (input.activeFile.isEmpty()) {
        gate.reason = QCoreApplication::translate("VEdit", "Rename needs an open document.");
        return gate;
    }
    if (!isHdlFile(input.activeFile)) {
        // One line, and it names BOTH what is accepted and what was seen: the
        // user learns why the command is greyed out without opening a log.
        gate.reason = QCoreApplication::translate(
                          "VEdit",
                          "Rename needs a Verilog/SystemVerilog file (%1); %2 is not one.")
                          .arg(hdlExtensions().join(QLatin1Char('/')),
                               QFileInfo(input.activeFile).fileName());
        return gate;
    }
    if (!input.slangFound) {
        gate.reason = QCoreApplication::translate(
            "VEdit",
            "Rename is unavailable: the slang binary was not found (bundled, PATH or VEDIT_SLANG).");
        return gate;
    }
    if (input.projectRoot.isEmpty()) {
        gate.reason = QCoreApplication::translate(
            "VEdit",
            "Rename needs an open project folder (the index is engineering-wide).");
        return gate;
    }
    if (input.projectFiles.isEmpty()) {
        // Transient: the project is open and its file set is still being
        // expanded. The command becomes usable when the set lands (the lane
        // re-evaluates on the published revision, no restart).
        gate.reason = QCoreApplication::translate(
            "VEdit",
            "Rename is waiting for the project file set (the scan has not finished yet).");
        return gate;
    }
    gate.available = true;
    return gate;
}

} // namespace vedit::workspace

#if defined(VEDIT_RENAME_HAVE_APP_SHELL)

#if defined(VEDIT_RENAME_HAVE_TOOLCHAIN)
#include "../diagnostics/toolchain.h"
#endif

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"

#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QPair>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStatusBar>
#include <QTextCursor>
#include <QTimer>

#if defined(VEDIT_RENAME_HAVE_KTEXTEDITOR)
#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#if defined(VEDIT_RENAME_HAVE_FFI)
#include <vd_ffi.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace vedit::workspace {

namespace {

constexpr const char *kCommandId = "rename.symbol";
constexpr int kCommandOrder = 68;      // right after the LSP actions (60-67)
constexpr int kStatePollMs = 400;      // the active tab has no shell-level signal
constexpr int kJobPollMs = 100;        // one rename job at a time, polled from the GUI
constexpr qint64 kJobBudgetMs = 120000; // the engine's own wall-clock budget
constexpr qint64 kJobHardStopMs = kJobBudgetMs + 20000;

// AC-T13.1 asks for the editor's F2 "equivalent" entry. F2 is ALREADY the
// primary binding of the LSP rename (`diagnostics.rename`, registered by the
// diagnostics lane, source of truth in src/editor/kernel/lsp_actions.cpp): Qt
// triggers NEITHER action of a duplicated sequence, and the product selfcheck's
// `commands.no-duplicate-shortcuts` fails on it. So this command carries the
// documented alias Ctrl+Shift+F2 while the conflict is a reported finding; a
// captain decision to move F2 here is a ONE-line change, and the registry test
// (printRegistryChecks) asserts the effective binding either way.
QKeySequence renameShortcut()
{
    return QKeySequence(QStringLiteral("Ctrl+Shift+F2"));
}

// Machine-readable lines are what the evidence log quotes; they go to stdout
// next to every other lane's lines and are flushed so a --selfcheck or an
// Xvfb run can be cut off without losing them.
void emitLine(const QString &line)
{
    std::printf("%s\n", qPrintable(line));
    std::fflush(stdout);
}

// One borrowed `VdStringList`: the byte arrays and the pointer array stay alive
// as long as this object does, which is exactly the ABI's "borrowed for the
// duration of the call" contract.
#if defined(VEDIT_RENAME_HAVE_FFI)
class BorrowedList final
{
public:
    explicit BorrowedList(const QStringList &values)
    {
        storage_.reserve(values.size());
        for (const QString &value : values) {
            storage_.append(value.toUtf8());
        }
        pointers_.reserve(storage_.size());
        for (QByteArray &value : storage_) {
            pointers_.append(value.data());
        }
        list_.count = static_cast<std::size_t>(pointers_.size());
        list_.items = pointers_.isEmpty() ? nullptr : pointers_.data();
    }

    const VdStringList &list() const { return list_; }

private:
    QList<QByteArray> storage_;
    QList<char *> pointers_;
    VdStringList list_{};
};
#endif // VEDIT_RENAME_HAVE_FFI

/// The product-wide toolchain answer for `slang` (bundled > local > PATH > env,
/// T12). The diagnostics lane owns the resolution order; this lane reuses it
/// instead of growing a second one (same pattern as the Scala run lane).
bool resolveSlang()
{
#if defined(VEDIT_RENAME_HAVE_TOOLCHAIN)
    const diagnostics::ToolchainReport report =
        diagnostics::resolveToolchain(diagnostics::applicationToolchainContext());
    return report.slang.found();
#else
    return false;
#endif
}

/// The lane's live state: the entry command, the gate it renders, the ONE rename
/// job that may be running (plan or apply) and the preview dialog. One instance
/// per shell (created and owned by installRename), destroyed with the shell.
class RenameLane final : public QObject
{
public:
    explicit RenameLane(vedit::app::ApplicationShell &shell)
        : QObject(&shell)
        , shell_(shell)
    {
        stateTimer_.setInterval(kStatePollMs);
        QObject::connect(&stateTimer_, &QTimer::timeout, this, [this]() { pollState(); });
        jobTimer_.setInterval(kJobPollMs);
        QObject::connect(&jobTimer_, &QTimer::timeout, this, [this]() { pollJob(); });
    }

    ~RenameLane() override
    {
        stateTimer_.stop();
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (jobKind_ != JobKind::None && service_ != nullptr) {
            vd_rename_cancel(service_, job_);
        }
#endif
        releaseJob();
        freePlan();
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (service_ != nullptr) {
            vd_service_destroy(service_);
            service_ = nullptr;
        }
#endif
    }

    void install()
    {
        if (shell_.commands() == nullptr) {
            emitLine(QStringLiteral(
                "RENAME-ENTRY-FAIL reason='the shell has no command registry'"));
            return;
        }
        vedit::app::CommandSpec spec;
        spec.id = QString::fromLatin1(kCommandId);
        spec.text = QCoreApplication::translate("VEdit", "Rename Symbol in &Project…");
        spec.menu = QStringLiteral("project");
        spec.defaultShortcut = renameShortcut();
        spec.order = kCommandOrder;
        action_ = shell_.commands()->add(spec);
        if (action_ == nullptr) {
            action_ = shell_.commands()->action(spec.id);
        }
        if (action_ == nullptr) {
            emitLine(QStringLiteral("RENAME-ENTRY-FAIL reason='the registry refused %1'")
                         .arg(spec.id));
            return;
        }
        shell_.commands()->setHandler(spec.id, [this]() { trigger(); });

        activeFile_ = activeFilePath();
        projectRoot_ = shell_.projectRoot();
        projectFilesRevision_ =
            shell_.property("vedit.diagnostics.projectFilesRevision").toULongLong();
        projectFiles_ = shell_.property("vedit.diagnostics.projectFiles").toStringList();
        slangFound_ = resolveSlang();
        syncGate();
        printRegistryChecks();
        stateTimer_.start();
    }

private:
    void pollState()
    {
        const QString file = activeFilePath();
        const QString root = shell_.projectRoot();
        const qulonglong revision =
            shell_.property("vedit.diagnostics.projectFilesRevision").toULongLong();
        bool changed = false;
        if (file != activeFile_ || root != projectRoot_) {
            activeFile_ = file;
            projectRoot_ = root;
            // The toolchain can appear while the file stays the same; an
            // explicit action re-resolves it too (trigger()).
            slangFound_ = resolveSlang();
            changed = true;
        }
        if (revision != projectFilesRevision_) {
            projectFilesRevision_ = revision;
            projectFiles_ = shell_.property("vedit.diagnostics.projectFiles").toStringList();
            changed = true;
        }
        if (changed) {
            syncGate();
        }
    }

    /// The project's EFFECTIVE `-I`/`-D` (T72): published by the diagnostics
    /// lane as `vedit.diagnostics.projectEnv` — the SAME expansion the lint /
    /// LSP children receive, never a second discovery.
    ///
    /// A property for a DIFFERENT root contributes nothing: the file-set rule
    /// ("another project's state is never carried") is applied to the
    /// environment too, so a project still loading can never lend its includes
    /// to the one the user is renaming in.
    void projectEnvironment(QStringList *includeDirs, QStringList *defines) const
    {
        includeDirs->clear();
        defines->clear();
        const QVariantMap published = shell_.property("vedit.diagnostics.projectEnv").toMap();
        if (published.isEmpty()
            || published.value(QStringLiteral("root")).toString() != projectRoot_) {
            return;
        }
        *includeDirs = published.value(QStringLiteral("includeDirs")).toStringList();
        *defines = published.value(QStringLiteral("defines")).toStringList();
    }

    /// Re-evaluate the AC-T13.1 rule and render it: the QAction's enabled state,
    /// its tooltip and the ONE observable line the evidence quotes.
    void syncGate()
    {
        RenameEntryInput input;
        input.activeFile = activeFile_;
        input.projectRoot = projectRoot_;
        input.projectFiles = projectFiles_;
        input.slangFound = slangFound_;
        gate_ = renameEntryGate(input);
#if !defined(VEDIT_RENAME_HAVE_FFI)
        gate_.available = false;
        gate_.reason = QCoreApplication::translate(
            "VEdit", "Rename is unavailable: this build carries no service layer (vd-ffi).");
#endif
#if !defined(VEDIT_RENAME_HAVE_TOOLCHAIN)
        gate_.available = false;
        gate_.reason = QCoreApplication::translate(
            "VEdit", "Rename is unavailable: this build carries no toolchain resolver.");
#endif
        if (action_ != nullptr) {
            action_->setEnabled(gate_.available);
            const QString tip =
                gate_.available
                    ? QCoreApplication::translate(
                          "VEdit",
                          "Rename the symbol at the cursor across the project (slang AST index)")
                    : gate_.reason;
            action_->setToolTip(tip);
            action_->setStatusTip(tip);
        }
        const QString line =
            QStringLiteral("enabled=%1 reason='%2' documents=%3 project-files=%4")
                .arg(gate_.available ? 1 : 0)
                .arg(gate_.reason.isEmpty() ? QStringLiteral("-") : gate_.reason)
                .arg(activeFile_.isEmpty() ? 0 : 1)
                .arg(projectFiles_.size());
        if (line != lastGateLine_) {
            lastGateLine_ = line;
            emitLine(QStringLiteral("RENAME-GATE ") + line);
        }
    }

    /// The command-registry test AC-T13.1 asks for, executed on the LIVE
    /// registry of the product (not on a copy): the action exists, sits in the
    /// announced menu, carries the announced binding exactly once, and its
    /// enabled state is the gate's answer.
    void printRegistryChecks()
    {
        int passed = 0;
        int failed = 0;
        const auto check = [&passed, &failed](const char *name, bool ok,
                                              const QString &detail) {
            ok ? ++passed : ++failed;
            emitLine(QStringLiteral("RENAME-ENTRY-CHECK name=%1 ok=%2%3")
                         .arg(QString::fromLatin1(name))
                         .arg(ok ? 1 : 0)
                         .arg(detail.isEmpty() ? QString()
                                               : QStringLiteral(" detail='%1'").arg(detail)));
        };
        vedit::app::CommandRegistry *commands = shell_.commands();
        const QString id = QString::fromLatin1(kCommandId);
        const QKeySequence expected = renameShortcut();
        check("registered", commands->action(id) != nullptr, id);
        check("action-identity", commands->action(id) == action_, QString());
        check("menu-entry",
              commands->actionsInMenu(QStringLiteral("project")).contains(action_),
              QStringLiteral("project"));
        check("text", action_ != nullptr && !action_->text().isEmpty(),
              action_ != nullptr ? action_->text() : QStringLiteral("<missing>"));
        check("shortcut", commands->effectiveShortcut(id) == expected,
              QStringLiteral("effective='%1' expected='%2'")
                  .arg(commands->effectiveShortcut(id).toString(), expected.toString()));
        QStringList owners;
        for (const QString &other : commands->ids()) {
            QAction *candidate = commands->action(other);
            if (candidate != nullptr && !expected.isEmpty()
                && candidate->shortcut() == expected) {
                owners.append(other);
            }
        }
        check("shortcut-unique", owners == QStringList{id},
              owners.join(QLatin1Char(',')));
        check("enabled-matches-gate",
              action_ != nullptr && action_->isEnabled() == gate_.available,
              QStringLiteral("enabled=%1 gate=%2")
                  .arg(action_ != nullptr && action_->isEnabled() ? 1 : 0)
                  .arg(gate_.available ? 1 : 0));
        emitLine(QStringLiteral("RENAME-ENTRY command='%1' menu='project' text='%2' "
                                "shortcut='%3' enabled=%4 reason='%5'")
                     .arg(id, action_->text(), expected.toString())
                     .arg(gate_.available ? 1 : 0)
                     .arg(gate_.reason.isEmpty() ? QStringLiteral("-") : gate_.reason));
        emitLine(QStringLiteral("RENAME-ENTRY-CHECKS passed=%1 failed=%2")
                     .arg(passed)
                     .arg(failed));
    }

    void trigger()
    {
        if (jobKind_ != JobKind::None) {
            message(QCoreApplication::translate(
                "VEdit", "Rename: a rename job is already in progress."));
            emitLine(QStringLiteral(
                "RENAME-UI state=refused reason='a rename job is already in progress'"));
            return;
        }
        // An explicit user action is the moment to look for a tool that was
        // installed (or a folder that was opened) since the last poll.
        pollState();
        slangFound_ = resolveSlang();
        syncGate();
        if (!gate_.available) {
            message(gate_.reason);
            emitLine(QStringLiteral("RENAME-UI state=refused reason='%1'").arg(gate_.reason));
            return;
        }
        const QString symbol = symbolUnderCursor();
        if (symbol.isEmpty()) {
            const QString reason = QCoreApplication::translate(
                "VEdit", "Rename needs the cursor on a SystemVerilog identifier.");
            message(reason);
            emitLine(QStringLiteral("RENAME-UI state=refused reason='%1'").arg(reason));
            return;
        }
        openPreview(symbol);
    }

    /// AC-T13.2: the command opens ONE non-modal dialog that renders the
    /// per-item preview. Every job is asynchronous, so the GUI thread never
    /// blocks on the index/plan/apply engine.
    void openPreview(const QString &symbol)
    {
        if (preview_.isNull()) {
            preview_ = new RenamePreviewDialog(&shell_);
            QObject::connect(preview_, &QDialog::finished, this, [this](int) {
                // The dialog refuses to close while a job runs, so reaching here
                // means no job owns the plan any more.
                freePlan();
                emitLine(QStringLiteral("RENAME-UI state=closed"));
            });
            preview_->setRequests(
                [this](const QString &from, const QString &to) { startPlan(from, to); },
                [this]() { startApply(); }, [this]() { cancelJob(); });
        }
        preview_->setSymbol(symbol);
        preview_->show();
        preview_->raise();
        preview_->activateWindow();
        emitLine(QStringLiteral("RENAME-UI state=open symbol='%1' file='%2'")
                     .arg(symbol, activeFile_));
    }

    void startPlan(const QString &from, const QString &to)
    {
        if (jobKind_ != JobKind::None) {
            return;
        }
        freePlan();
        if (from.isEmpty() || to.isEmpty()) {
            refusePlan(QStringLiteral("invalid"),
                       QCoreApplication::translate(
                           "VEdit", "the rename needs both the current and the new identifier"));
            return;
        }
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (service_ == nullptr) {
            service_ = vd_service_create();
        }
        if (service_ == nullptr) {
            refusePlan(QStringLiteral("service"),
                       QCoreApplication::translate(
                           "VEdit", "the service layer could not be created"));
            return;
        }
        if (projectRoot_.isEmpty() || projectFiles_.isEmpty()) {
            refusePlan(QStringLiteral("no_project"),
                       QCoreApplication::translate(
                           "VEdit", "the project has no file set to index"));
            return;
        }
        const BorrowedList files(projectFiles_);
        // T72: the elaboration needs the project's own `-I`/`-D` (the SAME
        // effective lists the lint children receive). Without them a project
        // whose file includes a header from another directory cannot be
        // indexed, so the whole rename fails before it starts.
        QStringList includeDirs;
        QStringList defines;
        projectEnvironment(&includeDirs, &defines);
        const BorrowedList includeList(includeDirs);
        const BorrowedList defineList(defines);
        emitLine(QStringLiteral("RENAME-PROJECT include-dirs=%1 defines=%2 root='%3'")
                     .arg(includeDirs.size())
                     .arg(defines.size())
                     .arg(projectRoot_));
        const QByteArray root = projectRoot_.toUtf8();
        const QByteArray fromBytes = from.toUtf8();
        const QByteArray toBytes = to.toUtf8();
        VdRenameOptions options{};
        options.whole_identifier = 1; // the ABI never falls back to text replacement
        options.dry_run = 0;          // a preview plans; it never writes
        options.max_entries_per_file = 0; // the engine's default preview bound
        options.timeout_ms = static_cast<uint64_t>(kJobBudgetMs);
        options.include_dirs = includeList.list();
        options.defines = defineList.list();
        const quint64 job = vd_rename_plan_start(service_, root.constData(), &files.list(),
                                                 fromBytes.constData(), toBytes.constData(),
                                                 &options);
        if (job == 0) {
            refusePlan(QStringLiteral("engine"),
                       serviceError(QCoreApplication::translate(
                           "VEdit", "the engine rejected the plan request")));
            return;
        }
        job_ = job;
        jobKind_ = JobKind::Plan;
        cancelRequested_ = false;
        jobClock_.start();
        jobTimer_.start();
        emitLine(
            QStringLiteral("RENAME-PREVIEW state=pending from='%1' to='%2' files=%3 root='%4'")
                .arg(from, to)
                .arg(projectFiles_.size())
                .arg(projectRoot_));
#else
        Q_UNUSED(from);
        Q_UNUSED(to);
        refusePlan(QStringLiteral("no_service"),
                   QCoreApplication::translate(
                       "VEdit", "this build carries no service layer (vd-ffi)"));
#endif
    }

    void startApply()
    {
        if (jobKind_ != JobKind::None) {
            return;
        }
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (service_ == nullptr || plan_.items == nullptr || plan_.count == 0) {
            refuseApply(QStringLiteral("no_plan"),
                        QCoreApplication::translate(
                            "VEdit", "there is no previewed plan to apply"));
            return;
        }
        // T72: the apply job RE-RUNS the index before writing, so it needs the
        // same project `-I`/`-D` the plan used (re-read: the project may have
        // been re-expanded since).
        QStringList includeDirs;
        QStringList defines;
        projectEnvironment(&includeDirs, &defines);
        const BorrowedList includeList(includeDirs);
        const BorrowedList defineList(defines);
        emitLine(QStringLiteral("RENAME-PROJECT include-dirs=%1 defines=%2 root='%3'")
                     .arg(includeDirs.size())
                     .arg(defines.size())
                     .arg(projectRoot_));
        VdRenameOptions options{};
        options.whole_identifier = 1;
        options.dry_run = 0;
        options.max_entries_per_file = 0;
        options.timeout_ms = static_cast<uint64_t>(kJobBudgetMs);
        options.include_dirs = includeList.list();
        options.defines = defineList.list();
        // ONE job for ONE confirmation: the plan is READ (not consumed) and the
        // engine applies every file or restores every written byte.
        const quint64 job = vd_rename_apply_start(service_, &plan_, &options);
        if (job == 0) {
            refuseApply(QStringLiteral("engine"),
                        serviceError(QCoreApplication::translate(
                            "VEdit", "the engine rejected the apply request")));
            return;
        }
        job_ = job;
        jobKind_ = JobKind::Apply;
        cancelRequested_ = false;
        jobClock_.start();
        jobTimer_.start();
        emitLine(QStringLiteral(
                     "RENAME-APPLY state=pending from='%1' to='%2' files=%3 occurrences=%4")
                     .arg(QString::fromUtf8(plan_.from != nullptr ? plan_.from : ""),
                          QString::fromUtf8(plan_.to != nullptr ? plan_.to : ""))
                     .arg(static_cast<qulonglong>(plan_.count))
                     .arg(static_cast<qulonglong>(plan_.occurrences)));
#endif
    }

    void pollJob()
    {
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (jobKind_ == JobKind::None) {
            jobTimer_.stop();
            return;
        }
        if (jobKind_ == JobKind::Plan) {
            pollPlan();
        } else {
            pollApply();
        }
#endif
    }

#if defined(VEDIT_RENAME_HAVE_FFI)
    void pollPlan()
    {
        VdRenamePlan plan{};
        const int32_t status = vd_rename_plan_poll(service_, job_, &plan);
        if (status == VD_STATUS_PENDING) {
            if (jobClock_.elapsed() > kJobHardStopMs) {
                // A cancel is a request, not a kill: the engine answers on its
                // own thread and the next poll reports the terminal state.
                cancelRequested_ = true;
                vd_rename_cancel(service_, job_);
            }
            return;
        }
        if (status == VD_STATUS_READY) {
            const qint64 elapsed = jobClock_.elapsed();
            if (cancelRequested_) {
                // The cancel reached the lane after the engine's last check. A
                // PLAN writes nothing, so the request is honoured by discarding
                // the finished plan: no preview, no apply, zero writes.
                vd_rename_plan_free(&plan);
                releaseJob();
                if (!preview_.isNull()) {
                    preview_->showPlanCancelled();
                }
                emitLine(QStringLiteral(
                    "RENAME-PREVIEW state=cancelled after='plan-finished' elapsed_ms=%1")
                             .arg(elapsed));
                return;
            }
            plan_ = plan; // the apply job reads this plan; its ownership stays here
            const RenamePreviewPlan model = previewModel(plan_);
            releaseJob();
            if (!preview_.isNull()) {
                preview_->showPlan(model);
            }
            emitPlanLines(model, elapsed);
            return;
        }
        // FAILED: a typed refusal or a cancel; never a half-built plan.
        const bool cancelled = cancelRequested_;
        const QString error = serviceError(QCoreApplication::translate(
            "VEdit", "the engine reported a failed plan job"));
        releaseJob();
        if (cancelled) {
            if (!preview_.isNull()) {
                preview_->showPlanCancelled();
            }
            emitLine(QStringLiteral("RENAME-PREVIEW state=cancelled"));
            return;
        }
        const QPair<QString, QString> split = splitEngineError(error);
        refusePlan(split.first, split.second);
    }

    void pollApply()
    {
        VdRenameApplyResult result{};
        const int32_t status = vd_rename_apply_poll(service_, job_, &result);
        if (status == VD_STATUS_PENDING) {
            if (jobClock_.elapsed() > kJobHardStopMs) {
                cancelRequested_ = true;
                vd_rename_cancel(service_, job_);
            }
            return;
        }
        if (status == VD_STATUS_READY) {
            const QString failed =
                result.failed_file != nullptr ? QString::fromUtf8(result.failed_file) : QString();
            const quint64 files = static_cast<quint64>(result.applied_files);
            const quint64 occurrences = static_cast<quint64>(result.applied_occurrences);
            const bool rolledBack = result.rolled_back != 0;
            const bool verified = result.verified != 0;
            const bool cancelRequested = cancelRequested_;
            // ONLY the engine's Cancelled outcome is a cancel: zero files written
            // and no failed file. A cancel that lost the race must report what
            // really happened (applied), never "nothing was renamed".
            const bool engineCancelled = cancelRequested && failed.isEmpty() && files == 0;
            vd_rename_apply_result_free(&result);
            releaseJob();
            freePlan();
            if (engineCancelled) {
                if (!preview_.isNull()) {
                    preview_->showApplyCancelled(rolledBack, files);
                }
                emitLine(QStringLiteral("RENAME-APPLY state=cancelled files_written=%1 "
                                        "rolled_back=%2")
                             .arg(files)
                             .arg(rolledBack ? 1 : 0));
                return;
            }
            if (!failed.isEmpty()) {
                if (!preview_.isNull()) {
                    preview_->showApplyFailed(failed, rolledBack, QString());
                }
                emitLine(QStringLiteral("RENAME-APPLY state=failed failed_file='%1' "
                                        "rolled_back=%2 applied_files=%3")
                             .arg(failed)
                             .arg(rolledBack ? 1 : 0)
                             .arg(files));
                return;
            }
            if (!preview_.isNull()) {
                preview_->showApplyApplied(files, occurrences, verified, cancelRequested);
            }
            emitLine(QStringLiteral("RENAME-APPLY state=applied files_written=%1 "
                                    "occurrences=%2 rolled_back=%3 verified=%4 "
                                    "cancel_arrived_late=%5")
                         .arg(files)
                         .arg(occurrences)
                         .arg(rolledBack ? 1 : 0)
                         .arg(verified ? 1 : 0)
                         .arg(cancelRequested ? 1 : 0));
            return;
        }
        // FAILED: a job error happens before the applier writes, so nothing was
        // written; the dialog still names the reason instead of staying silent.
        const QString error = serviceError(QCoreApplication::translate(
            "VEdit", "the engine reported a failed apply job"));
        releaseJob();
        freePlan();
        const QPair<QString, QString> split = splitEngineError(error);
        if (!preview_.isNull()) {
            preview_->showApplyRefused(split.first, split.second);
        }
        emitLine(QStringLiteral("RENAME-APPLY state=refused code='%1' reason='%2'")
                     .arg(split.first, split.second));
    }
#endif // VEDIT_RENAME_HAVE_FFI

    void cancelJob()
    {
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (jobKind_ == JobKind::None) {
            return;
        }
        cancelRequested_ = true;
        vd_rename_cancel(service_, job_);
        if (!preview_.isNull()) {
            preview_->showCancelRequested();
        }
        emitLine(QStringLiteral("RENAME-CANCEL state=requested phase=%1")
                     .arg(jobKind_ == JobKind::Plan ? QStringLiteral("preview")
                                                    : QStringLiteral("apply")));
#endif
    }

    void refusePlan(const QString &code, const QString &reason)
    {
        if (!preview_.isNull()) {
            preview_->showPlanRefused(code, reason);
        }
        emitLine(QStringLiteral("RENAME-PREVIEW state=refused code='%1' reason='%2'")
                     .arg(code, reason));
        message(QCoreApplication::translate("VEdit", "Rename preview refused: %1").arg(reason),
                12000);
    }

    void refuseApply(const QString &code, const QString &reason)
    {
        if (!preview_.isNull()) {
            preview_->showApplyRefused(code, reason);
        }
        emitLine(QStringLiteral("RENAME-APPLY state=refused code='%1' reason='%2'")
                     .arg(code, reason));
        message(QCoreApplication::translate("VEdit", "Rename apply refused: %1").arg(reason),
                12000);
        freePlan();
    }

    void releaseJob()
    {
        jobTimer_.stop();
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (job_ != 0 && service_ != nullptr) {
            vd_rename_release(service_, job_);
        }
        job_ = 0;
#endif
        jobKind_ = JobKind::None;
        cancelRequested_ = false;
    }

    void freePlan()
    {
#if defined(VEDIT_RENAME_HAVE_FFI)
        if (plan_.root != nullptr || plan_.items != nullptr) {
            vd_rename_plan_free(&plan_);
        }
        plan_ = VdRenamePlan{};
#endif
    }

    // --- rendering the engine's plan into the dialog's model ----------------
#if defined(VEDIT_RENAME_HAVE_FFI)
    void emitPlanLines(const RenamePreviewPlan &plan, qint64 elapsed) const
    {
        emitLine(QStringLiteral("RENAME-PREVIEW state=ready files=%1 occurrences=%2 "
                                "elapsed_ms=%3 from='%4' to='%5'")
                     .arg(plan.entries.size())
                     .arg(plan.occurrences)
                     .arg(elapsed)
                     .arg(plan.from, plan.to));
        for (const RenamePreviewFile &file : plan.entries) {
            emitLine(QStringLiteral("RENAME-PREVIEW-FILE file='%1' count=%2 truncated=%3")
                         .arg(file.path)
                         .arg(file.hits.size())
                         .arg(file.truncated ? 1 : 0));
            for (const RenamePreviewHit &hit : file.hits) {
                emitLine(QStringLiteral("RENAME-PREVIEW-ROW file='%1' line=%2 column=%3 "
                                        "before='%4' after='%5'")
                             .arg(file.path)
                             .arg(hit.line)
                             .arg(hit.column)
                             .arg(hit.before, hit.after));
            }
        }
        for (const QString &warning : plan.warnings) {
            emitLine(QStringLiteral("RENAME-PREVIEW-WARNING '%1'").arg(warning));
        }
    }

    RenamePreviewPlan previewModel(const VdRenamePlan &plan) const
    {
        RenamePreviewPlan model;
        model.root = QString::fromUtf8(plan.root != nullptr ? plan.root : "");
        model.from = QString::fromUtf8(plan.from != nullptr ? plan.from : "");
        model.to = QString::fromUtf8(plan.to != nullptr ? plan.to : "");
        model.files = static_cast<quint64>(plan.files);
        model.occurrences = static_cast<quint64>(plan.occurrences);
        const QDir rootDir(model.root);
        if (plan.items != nullptr) {
            for (size_t index = 0; index < plan.count; ++index) {
                const VdRenameFilePlan &source = plan.items[index];
                RenamePreviewFile file;
                file.path = QString::fromUtf8(source.file != nullptr ? source.file : "");
                file.label = rootDir.relativeFilePath(file.path);
                if (file.label.isEmpty() || file.label.startsWith(QStringLiteral(".."))) {
                    file.label = file.path;
                }
                file.truncated = source.truncated != 0;
                if (source.items != nullptr) {
                    for (size_t hitIndex = 0; hitIndex < source.count; ++hitIndex) {
                        const VdRenameHit &hit = source.items[hitIndex];
                        RenamePreviewHit entry;
                        entry.line = static_cast<int>(hit.line) + 1;
                        entry.column = static_cast<int>(hit.column) + 1;
                        entry.before =
                            QString::fromUtf8(hit.before != nullptr ? hit.before : "");
                        entry.after = QString::fromUtf8(hit.after != nullptr ? hit.after : "");
                        file.hits.append(entry);
                    }
                }
                model.entries.append(file);
            }
        }
        model.warnings = stringList(plan.warnings);
        return model;
    }

    static QStringList stringList(const VdStringList &list)
    {
        QStringList values;
        if (list.items == nullptr) {
            return values;
        }
        for (size_t index = 0; index < list.count; ++index) {
            if (list.items[index] != nullptr) {
                values.append(QString::fromUtf8(list.items[index]));
            }
        }
        return values;
    }
#endif // VEDIT_RENAME_HAVE_FFI

    /// The ABI reports `"<engine code>: <one-line reason>"`; the dialog shows
    /// both parts separately so an `ambiguous` message keeps its declaration
    /// list intact.
    static QPair<QString, QString> splitEngineError(const QString &error)
    {
        const int separator = error.indexOf(QStringLiteral(": "));
        if (separator <= 0) {
            return {QStringLiteral("engine"), error};
        }
        return {error.left(separator), error.mid(separator + 2)};
    }

    // --- the identifier under the cursor -----------------------------------
    static bool asciiLetter(QChar character)
    {
        const char16_t unit = character.unicode();
        return (unit >= u'A' && unit <= u'Z') || (unit >= u'a' && unit <= u'z');
    }

    static bool asciiDigit(QChar character)
    {
        const char16_t unit = character.unicode();
        return unit >= u'0' && unit <= u'9';
    }

    static bool identifierChar(QChar character)
    {
        return asciiLetter(character) || asciiDigit(character) || character == QLatin1Char('_')
            || character == QLatin1Char('$');
    }

    static bool isIdentifier(const QString &word)
    {
        if (word.isEmpty()) {
            return false;
        }
        if (!(asciiLetter(word.at(0)) || word.at(0) == QLatin1Char('_'))) {
            return false;
        }
        for (const QChar character : word) {
            if (!identifierChar(character)) {
                return false;
            }
        }
        return true;
    }

    QString symbolUnderCursor() const
    {
        vedit::app::EditorArea *area = shell_.editorArea();
        vedit::app::EditorPane *pane = area != nullptr ? area->activePane() : nullptr;
        QWidget *editor = pane != nullptr ? pane->editorWidget() : nullptr;
        if (editor == nullptr) {
            return {};
        }
#if defined(VEDIT_RENAME_HAVE_KTEXTEDITOR)
        if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
            if (view->document() != nullptr) {
                // The editor's OWN word boundary (the same one a double click
                // selects); the engine then decides whether it is nameable.
                const QString word = view->document()->wordAt(view->cursorPosition());
                return isIdentifier(word) ? word : QString();
            }
        }
#endif
        if (auto *plain = qobject_cast<QPlainTextEdit *>(editor)) {
            QTextCursor cursor = plain->textCursor();
            cursor.select(QTextCursor::WordUnderCursor);
            const QString word = cursor.selectedText();
            return isIdentifier(word) ? word : QString();
        }
        return {};
    }

#if defined(VEDIT_RENAME_HAVE_FFI)
    QString serviceError(const QString &fallback) const
    {
        VdError failure{};
        if (service_ != nullptr && vd_service_error(service_, &failure) != 0) {
            const QString text = QString::fromUtf8(failure.message);
            if (!text.isEmpty()) {
                return text;
            }
        }
        return fallback;
    }
#endif

    QString activeFilePath() const
    {
        vedit::app::EditorArea *area = shell_.editorArea();
        if (area == nullptr) {
            return {};
        }
        vedit::app::EditorPane *pane = area->activePane();
        return pane != nullptr ? pane->currentFilePath() : QString();
    }

    void message(const QString &text, int milliseconds = 8000)
    {
        if (QStatusBar *bar = shell_.statusBar()) {
            bar->showMessage(text, milliseconds);
        }
    }

    vedit::app::ApplicationShell &shell_;
    QAction *action_ = nullptr;
    QTimer stateTimer_;
    QTimer jobTimer_;
    bool slangFound_ = false;
    QString activeFile_;
    QString projectRoot_;
    QStringList projectFiles_;
    qulonglong projectFilesRevision_ = 0;
    RenameEntryGate gate_;
    QString lastGateLine_;
    QPointer<RenamePreviewDialog> preview_;
    QElapsedTimer jobClock_;
    bool cancelRequested_ = false;

    /// The ONLY rename job that can run: a plan (the preview) or an apply. A
    /// second job is refused while one runs, so one confirmation cannot start
    /// two writes.
    enum class JobKind { None, Plan, Apply };
    JobKind jobKind_ = JobKind::None;

#if defined(VEDIT_RENAME_HAVE_FFI)
    VdService *service_ = nullptr;
    uint64_t job_ = 0;
    /// The previewed plan stays alive until the apply job has READ it (the ABI
    /// contract); freePlan() is the one owner.
    VdRenamePlan plan_{};
#else
    uint64_t job_ = 0; // never non-zero: the build has no service layer
#endif
};

} // namespace

void installRename(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)options;
    // Owned by the shell: one instance per shell, destroyed with it.
    auto *lane = new RenameLane(shell);
    lane->install();
}

} // namespace vedit::workspace

#else // VEDIT_RENAME_HAVE_APP_SHELL

namespace vedit::workspace {

// The lane is built stand-alone (its own CMake invocation) to compile the pure
// gate; without the shell there is nothing to install, and src/app never calls
// this form (the integration TU is compiled only when the shell exists).
void installRename(vedit::app::ApplicationShell &, const IntegrationOptions &) {}

} // namespace vedit::workspace

#endif // VEDIT_RENAME_HAVE_APP_SHELL
