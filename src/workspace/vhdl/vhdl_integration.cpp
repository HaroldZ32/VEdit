#include "vhdl_format.h"
#include "vhdl_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "vhdl_lint.h"
#include "vhdl_lsp.h"

#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
#include "../diagnostics/autofix.h"
#include "../diagnostics/diagnostics_controller.h"
#include "../diagnostics/diagnostics_panel.h"
#include "../diagnostics/toolchain.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

namespace vedit::workspace {

namespace {

#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)

/// AC-6.7: exactly ONE line carrying the three facts the criterion names — what
/// is missing, what it disables and how to fix it.
///
/// T60: the impact clause is NOT one generic sentence. It is derived from the
/// tools that are actually absent (`vhdlImpact()`), because only `vhdl_ls` owns
/// navigation — saying "navigation is off" when just GHDL is missing tells the
/// user something untrue. The result stays a single line; the full sentence
/// (`vhdlUserMessage()`) is the tooltip and the shell diagnostic.
QString statusBarLine(const diagnostics::ToolchainReport &report)
{
    const QStringList missing = report.vhdlMissing();
    if (missing.isEmpty()) {
        return {};
    }
    return QCoreApplication::translate(
               "VEdit", "VHDL tools missing: %1 — %2 Fix: bin/, PATH, or "
                        "VEDIT_VHDL_LS/VEDIT_GHDL/VEDIT_VSG.")
        .arg(missing.join(QStringLiteral(", ")), report.vhdlImpact());
}

#endif

} // namespace

QString vhdlLspPath()
{
#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
    const diagnostics::ToolchainSearchContext context =
        diagnostics::applicationToolchainContext();
    const diagnostics::ToolchainReport report = diagnostics::resolveToolchain(context);
    return report.vhdlLs.found() ? report.vhdlLs.path : QString();
#else
    return {};
#endif
}

VhdlLspSession *vhdlSessionForShell(vedit::app::ApplicationShell &shell)
{
    return shell.findChild<VhdlLspSession *>(QStringLiteral("veditVhdlLsp"));
}

QString vhdlToolchainStatusLine()
{
#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
    const diagnostics::ToolchainSearchContext context =
        diagnostics::applicationToolchainContext();
    return statusBarLine(diagnostics::resolveToolchain(context));
#else
    // A build without the diagnostics lane has no resolver at all; saying so is
    // better than an empty status that looks like "everything is installed".
    return QCoreApplication::translate(
        "VEdit", "VHDL tools unavailable: this build has no diagnostics lane, so no "
                 "toolchain can be resolved.");
#endif
}

namespace {

/// The lane's live state: ONE session per shell, created on the first VHDL open.
/// The badge lives as long as the shell; the session is re-created after a
/// project switch, so no consumer may hold a session pointer across it.
struct VhdlLaneState {
    VhdlLspSession *session = nullptr;
    QLabel *badge = nullptr;
    QStringList pending; // documents opened while the handshake was in flight
    bool missingReported = false;
    // AC-6.4: the merge lint (GHDL + VSG). ONE run at a time: a second open
    // supersedes the first, and a superseded job's late answer is ignored.
    VhdlLintJob *lint = nullptr;
    // AC-6.6: the vsg --fix run. ONE at a time, same supersede rule.
    VhdlFormatJob *format = nullptr;
    QString ghdlPath;
    QString vsgPath;
};

/// The root the session must run in. A server reads `vhdl_ls.toml` from its ROOT
/// only, so with no project open the nearest ancestor holding that file is the
/// real boundary; falling back to the document's directory is what leaves the
/// library mapping unknown.
QString sessionRootFor(const vedit::app::ApplicationShell &shell, const QString &path)
{
    const QString projectRoot = shell.projectRoot();
    if (!projectRoot.isEmpty() && QFileInfo(projectRoot).isDir()) {
        return projectRoot;
    }
    QDir directory = QFileInfo(path).absoluteDir();
    for (int depth = 0; depth < 8; ++depth) {
        if (QFileInfo::exists(directory.filePath(QStringLiteral("vhdl_ls.toml")))) {
            return directory.absolutePath();
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return QFileInfo(path).absolutePath();
}

/// One status-bar text for the lane's own surface (the "角标"): it must name
/// `vhdl_ls`, because that is the fact a user checks after opening a VHDL file.
QString badgeText(const VhdlLspSession &session)
{
    return QCoreApplication::translate("VEdit", "vhdl_ls · %1").arg(session.stateName());
}

void installSession(vedit::app::ApplicationShell &shell, const QString &ghdlPath,
                    const QString &vsgPath)
{
    auto state = std::make_shared<VhdlLaneState>();
    // AC-6.4: the two CLI tools the merge lint runs. Empty = not installed (the
    // AC-6.7 status line already names it; the lint leg is skipped, never faked).
    state->ghdlPath = ghdlPath;
    state->vsgPath = vsgPath;

    if (QStatusBar *statusBar = shell.statusBar()) {
        state->badge = new QLabel(statusBar);
        state->badge->setObjectName(QStringLiteral("veditVhdlBackend"));
        state->badge->setVisible(false);
        statusBar->addPermanentWidget(state->badge);
    }

    const auto showBadge = [state](bool visible) {
        if (state->badge == nullptr) {
            return;
        }
        state->badge->setVisible(visible);
        if (visible && state->session != nullptr) {
            state->badge->setText(badgeText(*state->session));
            state->badge->setToolTip(QStringLiteral("VHDL language server %1 (pid %2)")
                                         .arg(state->session->program())
                                         .arg(state->session->processId()));
        }
    };

    // AC-6.4: the merged GHDL + VSG rows of ONE file, published into the product's
    // ONE diagnostics view (the panel renders `displayDiagnostics()`, so the rows
    // go through the controller; nothing else can make them visible).
    //
    // A note (a tool that refused or could not be parsed) is surfaced as a shell
    // diagnostic: "no output" and "clean" must never look the same.
    const auto publishLint = [&shell](const QString &path,
                                      const vedit::workspace::VhdlLintReport &report) {
        if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
            std::printf("VHDL-LINT file='%s' %s\n", qPrintable(path),
                        qPrintable(report.summary()));
            for (const auto &row : report.rows) {
                std::printf("VHDL-ROW file='%s' source=%s sev=%d line=%d col=%d "
                            "code='%s' msg='%s'\n",
                            qPrintable(row.file), qPrintable(row.source), row.severity,
                            row.line + 1, row.column + 1, qPrintable(row.code),
                            qPrintable(row.message));
            }
            for (const QString &note : report.notes) {
                std::printf("VHDL-NOTE %s\n", qPrintable(note));
            }
            for (const QString &missing : report.missing) {
                std::printf("VHDL-MISSING %s\n", qPrintable(missing));
            }
            std::fflush(stdout);
        }
#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
        int published = 0;
        if (auto *panel = shell.findChild<diagnostics::DiagnosticsPanel *>()) {
            if (auto *controller = panel->controller()) {
                controller->setLaneDiagnostics(QStringLiteral("vhdl"), report.rows);
                published = report.rows.size();
            }
        }
        if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
            std::printf("VHDL-LANE published=%d source=vhdl\n", published);
            std::fflush(stdout);
        }
#endif
        for (const QString &note : report.notes) {
            shell.addShellDiagnostic(
                QCoreApplication::translate("VEdit", "VHDL lint: %1").arg(note));
        }
        for (const QString &missing : report.missing) {
            shell.addShellDiagnostic(QCoreApplication::translate(
                "VEdit", "VHDL lint: %1 is not installed, so its half is missing").arg(missing));
        }
    };

    /// AC-6.6 evidence seam: the SAME format dispatch the Project menu calls,
    /// triggered for the file just opened when the environment names it (like
    /// the diagnostics lane's waiver probe). Empty in a build without the
    /// toolchain lane, which has no formatter at all.
    std::function<void()> formatProbe;
#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
    // AC-6.6: VSG --fix on the ACTIVE VHDL document.
    //
    // The temp-copy/diff/apply protocol lives in vhdl_format.*: the job writes
    // the CURRENT BUFFER to a temp file, validates it with GHDL, runs
    // `vsg --fix` on that copy, re-validates the fixed copy and yields the
    // diff. Only an `ok` result is applied, as ONE undoable edit; the user's
    // file on disk is never written and nothing is saved.
    const auto formatVhdl = [&shell, state](KTextEditor::Document *document, const QString &path) {
        if (document == nullptr) {
            return;
        }
        if (state->format != nullptr) {
            state->format->deleteLater(); // superseded; its dtor kills the child
            state->format = nullptr;
        }
        auto *job = new VhdlFormatJob(&shell);
        state->format = job;
        const QString snapshot = document->text();
        QPointer<KTextEditor::Document> guarded(document);
        QObject::connect(
            job, &VhdlFormatJob::finished, &shell,
            [&shell, state, job, guarded, path, snapshot](const VhdlFormatResult &result) {
                if (state->format != job) {
                    return; // a newer run superseded this one
                }
                std::printf("VHDL-FORMAT file='%s' %s\n", qPrintable(path),
                            qPrintable(result.summary()));
                if (!result.diff.isEmpty()) {
                    QString diff = result.diff;
                    diff.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
                    std::printf("VHDL-FORMAT-DIFF file='%s' diff='%s'\n", qPrintable(path),
                                qPrintable(diff));
                }
                for (const QString &note : result.notes) {
                    std::printf("VHDL-FORMAT-NOTE %s\n", qPrintable(note));
                }
                std::fflush(stdout);
                if (!result.ok) {
                    std::printf("VHDL-FORMAT-FAILED stage=%s reason='%s' file='%s'\n",
                                qPrintable(result.stage), qPrintable(result.error),
                                qPrintable(path));
                    std::fflush(stdout);
                    if (QStatusBar *statusBar = shell.statusBar()) {
                        statusBar->showMessage(
                            QCoreApplication::translate("VEdit", "VHDL formatting failed: %1")
                                .arg(result.error),
                            8000);
                    }
                    return;
                }
                if (!result.changed) {
                    std::printf("VHDL-FORMAT-UNCHANGED file='%s'\n", qPrintable(path));
                    std::fflush(stdout);
                    if (QStatusBar *statusBar = shell.statusBar()) {
                        statusBar->showMessage(
                            QCoreApplication::translate("VEdit", "Already formatted (VSG)"), 5000);
                    }
                    return;
                }
                if (guarded.isNull() || guarded->text() != snapshot) {
                    // The user closed or edited the document while VSG ran; a
                    // diff computed for the old text must never be applied.
                    std::printf("VHDL-FORMAT-SKIPPED reason='buffer changed while formatting' "
                                "file='%s'\n",
                                qPrintable(path));
                    std::fflush(stdout);
                    if (QStatusBar *statusBar = shell.statusBar()) {
                        statusBar->showMessage(
                            QCoreApplication::translate(
                                "VEdit", "The buffer changed while VSG ran; nothing was applied."),
                            8000);
                    }
                    return;
                }
                const bool applied = diagnostics::applyAutofixToDocument(guarded.data(),
                                                                         result.fixedText);
                std::printf("VHDL-FORMAT-APPLIED applied=%d saved=0 in-bytes=%lld "
                            "out-bytes=%lld file='%s'\n",
                            applied ? 1 : 0,
                            static_cast<long long>(snapshot.toUtf8().size()),
                            static_cast<long long>(result.fixedText.toUtf8().size()),
                            qPrintable(path));
                std::fflush(stdout);
                if (QStatusBar *statusBar = shell.statusBar()) {
                    statusBar->showMessage(
                        applied ? QCoreApplication::translate("VEdit", "Formatted with VSG (VHDL)")
                                : QCoreApplication::translate(
                                      "VEdit",
                                      "The formatting could not be applied: the document "
                                      "refused the edit (is it read-only?)"),
                        applied ? 5000 : 8000);
                }
            });
        job->start(snapshot, path, state->vsgPath, state->ghdlPath);
    };

    // ONE "Format Document" command for every language: this lane takes the
    // command over and dispatches on the active document, because a `.vhd`
    // handed to verible-verilog-format can only fail. Everything that is not
    // VHDL keeps the SAME verible path the diagnostics lane ran (same helper,
    // same machine tokens, same status texts).
    const auto formatActive = [&shell, formatVhdl]() {
        vedit::app::EditorPane *pane =
            shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
        if (pane == nullptr) {
            return;
        }
        auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        if (view == nullptr || view->document() == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        if (isVhdlFile(path)) {
            formatVhdl(view->document(), path);
            return;
        }
        const diagnostics::ToolchainReport toolchain =
            diagnostics::resolveToolchain(diagnostics::applicationToolchainContext());
        const QString original = view->document()->text();
        QString formatted;
        QString error;
        if (!diagnostics::formatVerilog(toolchain.veribleFormat.path, original, &formatted,
                                        &error)) {
            std::printf("FORMAT-FAILED backend=verible reason='%s'\n", qPrintable(error));
            std::fflush(stdout);
            if (QStatusBar *statusBar = shell.statusBar()) {
                statusBar->showMessage(
                    QCoreApplication::translate("VEdit", "Formatting failed: %1").arg(error), 8000);
            }
            return;
        }
        const bool changed = formatted != original;
        if (changed) {
            diagnostics::applyAutofixToDocument(view->document(), formatted);
        }
        std::printf("FORMAT backend=verible formatter=%s changed=%d "
                    "enabled-by='always-verible-format' in-bytes=%lld out-bytes=%lld file='%s'\n",
                    qPrintable(toolchain.veribleFormat.path), changed ? 1 : 0,
                    static_cast<long long>(original.toUtf8().size()),
                    static_cast<long long>(formatted.toUtf8().size()), qPrintable(path));
        std::fflush(stdout);
        if (QStatusBar *statusBar = shell.statusBar()) {
            statusBar->showMessage(
                changed
                    ? QCoreApplication::translate("VEdit", "Formatted with verible-verilog-format")
                    : QCoreApplication::translate("VEdit",
                                                  "Already formatted (verible-verilog-format)"),
                5000);
        }
    };
    formatProbe = formatActive;
#endif // VEDIT_VHDL_HAVE_TOOLCHAIN

    const auto openVhdl = [state, &shell, showBadge, publishLint, formatProbe]
        (const QString &path) {
        if (!isVhdlFile(path)) {
            return;
        }
        // AC-6.6 evidence seam (env-gated, like the diagnostics waiver probe):
        // drive the SAME Project-menu dispatch for the file that just opened.
        // It runs before the LSP branch below because a missing `vhdl_ls` makes
        // that branch return, and formatting must not depend on navigation.
        const QString formatProbeTarget = qEnvironmentVariable("VEDIT_VHDL_FORMAT_PROBE");
        if (formatProbe && !formatProbeTarget.isEmpty() && path.endsWith(formatProbeTarget)) {
            QTimer::singleShot(700, &shell, formatProbe);
        }
        // AC-6.4: the CLI merge runs beside the language server. When neither CLI
        // tool is installed nothing runs here (the AC-6.7 line already names the
        // missing tools); a missing tool is never turned into "no findings".
        if (!state->ghdlPath.isEmpty() || !state->vsgPath.isEmpty()) {
            if (state->lint != nullptr) {
                state->lint->deleteLater(); // superseded; its dtor kills the child
                state->lint = nullptr;
            }
            auto *job = new VhdlLintJob(&shell);
            state->lint = job;
            QObject::connect(job, &VhdlLintJob::finished, &shell,
                             [state, publishLint, job, path](const VhdlLintReport &report) {
                                 if (state->lint != job) {
                                     return; // a newer file superseded this run
                                 }
                                 publishLint(path, report);
                             });
            job->start(path, state->ghdlPath, state->vsgPath);
        }
        if (state->session == nullptr) {
            auto *session = new VhdlLspSession(&shell);
            session->setObjectName(QStringLiteral("veditVhdlLsp"));
            QObject::connect(session, &VhdlLspSession::readyChanged, &shell,
                             [state, showBadge](bool ready) {
                                 showBadge(ready);
                                 if (!ready || state->session == nullptr) {
                                     return;
                                 }
                                 std::printf("VHDL-BADGE lsp=vhdl_ls state=ready pid=%lld "
                                             "files=%d\n",
                                             static_cast<long long>(state->session->processId()),
                                             state->session->filesOpened());
                                 std::fflush(stdout);
                                 for (const QString &pending : state->pending) {
                                     state->session->openDocument(pending);
                                 }
                                 state->pending.clear();
                             });
            QObject::connect(session, &VhdlLspSession::configRefused, &shell,
                             [&shell](const QString &reason) {
                                 shell.addShellDiagnostic(
                                     QCoreApplication::translate(
                                         "VEdit", "VHDL library configuration rejected: %1")
                                         .arg(reason));
                             });
            QObject::connect(session, &VhdlLspSession::failed, &shell,
                             [state, showBadge, &shell](const QString &reason) {
                                 showBadge(false);
                                 std::printf("VHDL-BADGE lsp=vhdl_ls state=failed reason='%s'\n",
                                             qPrintable(reason));
                                 std::fflush(stdout);
                                 shell.addShellDiagnostic(
                                     QCoreApplication::translate(
                                         "VEdit", "VHDL language server: %1")
                                         .arg(reason));
                                 state->pending.clear();
                             });
            state->session = session;
        }
        VhdlLspSession *session = state->session;
        if (session->state() == VhdlLspSession::State::Failed) {
            return; // the failure is already reported once; never retried in a loop
        }
        if (session->ready()) {
            session->openDocument(path);
            return;
        }
        if (session->state() == VhdlLspSession::State::Starting) {
            state->pending.append(path);
            return;
        }
        const QString program = vhdlLspPath();
        if (program.isEmpty()) {
            if (!state->missingReported) {
                state->missingReported = true;
                std::printf("VHDL-LSP-MISSING file='%s' reason='vhdl_ls is not installed'\n",
                            qPrintable(path));
                std::fflush(stdout);
            }
            return;
        }
        const QString root = sessionRootFor(shell, path);
        state->pending.append(path);
        // AC-6.3: the workspace's `vhdl_ls.toml` wins; without one the lane writes
        // the default single-library config INTO THE ROOT — the only channel the
        // server reports as its workspace mapping (coverage for unopened files, and
        // no "semantic analysis might be incorrect" warning) — and says so in one
        // line. This is a policy choice backed by measurement, not a navigation
        // requirement: every arrangement resolves when the documents are opened.
        const VhdlConfigReport config = prepareVhdlConfig(root);
        std::printf("VHDL-CONFIG %s\n", qPrintable(config.text()));
        std::fflush(stdout);
        if (!config.error.isEmpty()) {
            // A config the server will refuse, or one that cannot exist, is an
            // explicit state — never a crash and never a silent downgrade.
            std::printf("VHDL-CONFIG-ERROR reason='%s'\n", qPrintable(config.error));
            std::fflush(stdout);
            shell.addShellDiagnostic(
                QCoreApplication::translate("VEdit", "VHDL language configuration: %1")
                    .arg(config.error));
        }
        std::printf("VHDL-ROUTE file='%s' lsp=vhdl_ls verilog-backends=not-contacted\n",
                    qPrintable(path));
        std::fflush(stdout);
        if (!session->start(program, root)) {
            state->pending.clear();
        }
    };

    QObject::connect(&shell, &vedit::app::ApplicationShell::fileOpened, &shell, openVhdl);

    // A project switch invalidates the library mapping: the old session describes
    // the OLD root, so it is stopped and the next VHDL open starts a fresh one.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [state, showBadge](const QString &root) {
                         if (state->session == nullptr) {
                             return;
                         }
                         if (state->session->workspaceRoot() == root) {
                             return;
                         }
                         std::printf("VHDL-LSP-ROOT-CHANGED root='%s'\n", qPrintable(root));
                         std::fflush(stdout);
                         showBadge(false);
                         state->pending.clear();
                         state->session->stop();
                         state->session->deleteLater();
                         state->session = nullptr;
                     });

#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
    // AC-6.6: ONE "Format Document" command, dispatched by language. The
    // diagnostics lane installed the command; this lane owns its VHDL branch
    // and keeps the non-VHDL branch byte-for-byte the verible path.
    if (shell.commands() != nullptr
        && shell.commands()->action(QStringLiteral("diagnostics.format")) != nullptr) {
        shell.commands()->setHandler(QStringLiteral("diagnostics.format"), formatActive);
    }
#endif
}

} // namespace

void installVhdl(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)options;

#if defined(VEDIT_VHDL_HAVE_TOOLCHAIN)
    const diagnostics::ToolchainSearchContext context =
        diagnostics::applicationToolchainContext();
    const diagnostics::ToolchainReport report = diagnostics::resolveToolchain(context);
    if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
        std::printf("VHDL-TOOLCHAIN %s\n", qPrintable(report.vhdlStatusLine()));
    }
    const QString line = statusBarLine(report);
    const QString detail = report.vhdlUserMessage();
    const QString ghdlPath = report.ghdl.found() ? report.ghdl.path : QString();
    const QString vsgPath = report.vsg.found() ? report.vsg.path : QString();
#else
    const QString line = vhdlToolchainStatusLine();
    const QString detail = line;
    // No resolver in this build: the merge lint has no tools to run and says so
    // through the AC-6.7 line instead of pretending the file is clean.
    const QString ghdlPath;
    const QString vsgPath;
#endif

    if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
        const QString logged = line.isEmpty()
            ? QStringLiteral("(none: every VHDL tool is present)")
            : line;
        std::printf("VHDL-STATUS %s\n", qPrintable(logged));
    }

    // AC-6.2: the session is wired even when the toolchain line is silent (a
    // healthy toolchain used to `return` here and would have skipped the wiring).
    // AC-6.4: the same installation carries the two CLI tools the merge lint runs.
    installSession(shell, ghdlPath, vsgPath);

    if (line.isEmpty()) {
        // Healthy is silent: no status-bar line and no shell diagnostic.
        return;
    }

    // AC-6.7: ONE line. The label is a CHILD of the shell's status bar, so it is
    // destroyed with it and this lane keeps no pointer that could dangle.
    if (QStatusBar *statusBar = shell.statusBar()) {
        auto *notice = statusBar->findChild<QLabel *>(QStringLiteral("veditVhdlStatus"));
        if (notice == nullptr) {
            notice = new QLabel(statusBar);
            notice->setObjectName(QStringLiteral("veditVhdlStatus"));
            statusBar->addPermanentWidget(notice);
        }
        notice->setText(line);
        notice->setToolTip(detail);
        notice->setVisible(true);
    }
    // The same fact in the channel `--selfcheck` and the log read, so a missing
    // VHDL tool is observable without a GUI (the shell drops duplicates).
    shell.addShellDiagnostic(detail);
}

} // namespace vedit::workspace
