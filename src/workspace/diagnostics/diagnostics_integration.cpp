#include "diagnostics_integration.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)

#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include "app_shell.h"
#include "command_registry.h"
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
#include "diagnostics_annotations.h"
#endif
#include "editor_area.h"
#include "panel_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QKeySequence>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QDebug>
#include <cstdio>
#include <QDockWidget>
#include <QWidget>

#include <memory>

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
///
/// The project poller above only runs when the product was started with
/// `--project`; a user who double-clicks the AppImage and opens a file never got
/// any analysis (no LSP session, an empty outline). This watcher reads the active
/// pane's file and its EDITOR BUFFER every 400 ms:
///   * a new file  -> analyze immediately (file scope, no project needed);
///   * edited text -> analyze after a 700 ms quiet period (debounced);
/// and it tells the controller whether the buffer differs from the file on disk,
/// so the panel can say which text was linted.
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
/// T131: push the analysed file's diagnostics into the ACTIVE editor view as
/// inline annotations (KTextEditor::AnnotationModel), so the error is visible on
/// the line itself - not only in a panel the user must find.
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
        // Only a KTextEditor view can show annotations; the demo provider cannot.
        auto *view = dynamic_cast<KTextEditor::View *>(pane->editorWidget());
        if (view == nullptr) {
            return;
        }
        const QString path = pane->currentFilePath();
        if (path.isEmpty()) {
            return;
        }
        QList<vedit::editor::DiagnosticAnnotation> entries;
        for (const diagnostics::DiagnosticEntry &entry : controller_->diagnostics()) {
            if (entry.file != path) {
                continue;
            }
            vedit::editor::DiagnosticAnnotation annotation;
            annotation.line = entry.line;
            annotation.severity = entry.severity;
            annotation.message = entry.message;
            annotation.rule = entry.code.isEmpty() ? entry.source : entry.code;
            entries.append(annotation);
        }
        if (entries.isEmpty()) {
            return;
        }
        const QString signature = signatureOf(entries);
        if (signature == lastSignature_ && path == lastPath_) {
            return;
        }
        lastSignature_ = signature;
        lastPath_ = path;
        vedit::editor::DiagnosticsAnnotations::install(view);
        vedit::editor::DiagnosticsAnnotations::setForView(view, entries);
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            qInfo("diagnostics: %lld inline annotation(s) on %s", static_cast<long long>(entries.size()),
                  qPrintable(path));
        }
    }

    static QString signatureOf(const QList<vedit::editor::DiagnosticAnnotation> &entries)
    {
        QStringList parts;
        for (const vedit::editor::DiagnosticAnnotation &entry : entries) {
            parts.append(QStringLiteral("%1/%2/%3").arg(entry.line).arg(entry.severity).arg(entry.message));
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
        // Reverse control (T131): VEDIT_NO_ACTIVE_FILE_WATCHER=1 proves that the
        // automatic analysis is what makes a plain "open a file" work at all.
        if (qEnvironmentVariableIsSet("VEDIT_NO_ACTIVE_FILE_WATCHER")) {
            return;
        }
        debounce_->setSingleShot(true);
        debounce_->setInterval(700);
        connect(debounce_, &QTimer::timeout, this, [this]() { analyze(); });
        connect(tick_, &QTimer::timeout, this, [this]() { poll(); });
        tick_->start(400);
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

    void poll()
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
            lastPath_.clear();
            lastText_.clear();
            return;
        }
        const QString text = bufferText(widget);
        if (text.isEmpty() && lastPath_ == path) {
            return; // the widget is not readable (yet); wait for the next tick
        }
        if (path != lastPath_) {
            lastPath_ = path;
            lastText_ = text;
            analyze(); // opening a file is an analysis trigger, not a manual step
            return;
        }
        if (text != lastText_) {
            lastText_ = text;
            debounce_->start(); // typing: analyze once the user pauses
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
        QFile file(path);
        QString disk;
        if (file.open(QIODevice::ReadOnly)) {
            disk = QString::fromUtf8(file.readAll());
        }
        const bool dirty = disk != text;
        controller_->analyzeFile(path, text, dirty);
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            qInfo("diagnostics: analyze %s (%s)",
                  qPrintable(path), dirty ? "editor buffer, not saved" : "saved file");
        }
    }

    vedit::app::ApplicationShell *shell_ = nullptr;
    diagnostics::DiagnosticsController *controller_ = nullptr;
    QTimer *tick_ = nullptr;
    QTimer *debounce_ = nullptr;
    QString lastPath_;
    QString lastText_;
};

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
/// Ask the project model for the effective file set and hand it to the
/// controller. The poll is timer-driven, so `installPanels()` never blocks.
class ProjectFileSetPoller final : public QObject {
public:
    ProjectFileSetPoller(QObject *parent,
                         diagnostics::DiagnosticsController *controller,
                         const QString &projectRoot)
        : QObject(parent)
        , controller_(controller)
        , projectRoot_(projectRoot)
        , timer_(new QTimer(this))
    {
        service_ = vd_service_create();
        if (service_ == nullptr) {
            qWarning("diagnostics: cannot create the Rust service handle for the project model");
            return;
        }
        const QByteArray root = projectRoot_.toUtf8();
        job_ = vd_project_parse(service_, root.constData());
        if (job_ == 0) {
            qWarning("diagnostics: project parse was rejected: %s",
                     qPrintable(serviceError()));
            return;
        }
        connect(timer_, &QTimer::timeout, this, [this]() { poll(); });
        timer_->start(50);
    }

    ~ProjectFileSetPoller() override
    {
        if (service_ != nullptr) {
            // Running jobs finish into memory nobody reads (vd-ffi contract).
            vd_service_destroy(service_);
        }
    }

private:
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
        if (++ticks_ > 200) { // 10 s: a broken project job must not poll forever
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
        vd_project_info_free(&info);
        if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
            qInfo("diagnostics: effective file set from the project model: %lld file(s)",
                  static_cast<long long>(files.size()));
        }
        controller_->setFileSet(files);
    }

    diagnostics::DiagnosticsController *controller_ = nullptr;
    QString projectRoot_;
    QTimer *timer_ = nullptr;
    VdService *service_ = nullptr;
    quint64 job_ = 0;
    int ticks_ = 0;
};
#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

} // namespace

void installDiagnostics(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }

    // Discovery order is bundled > local checkout > PATH > env; the chosen source,
    // the local candidates and the missing list are printed and shown, so "no
    // diagnostics" is never silent — and never a wall of text in the UI.
    const diagnostics::ToolchainSearchContext toolchainContext =
        diagnostics::applicationToolchainContext();
    const diagnostics::ToolchainReport toolchain =
        diagnostics::resolveToolchain(toolchainContext);
    if (!qEnvironmentVariableIsSet("VEDIT_QUIET")) {
        std::printf("TOOLCHAIN %s local=[%s]\n", qPrintable(toolchain.statusLine()),
                    qPrintable(toolchainContext.localDirs.join(QLatin1Char(','))));
    }

    auto lsp = diagnostics::createFfiLspSessionBackend();
    auto slang = diagnostics::createFfiSlangBackend(toolchain.slang.path);
    auto *holder = new ControllerHolder(
        std::make_unique<diagnostics::DiagnosticsController>(std::move(lsp), std::move(slang)),
        &shell);
    auto *controller = holder->controller();
    controller->setToolchainReport(toolchain);
    shell.setProperty("vedit.diagnosticsController", QVariant::fromValue<QObject *>(holder));

    const QString projectRoot = options.projectRoot;
    controller->setConfiguration(toolchain.verible.path, projectRoot);
    // Exactly ONE user-facing status line for a missing toolchain: the shell
    // diagnostic (deduplicated by the shell) and the panel status read the same
    // concise message. The probed candidate list is a log detail, not UI text,
    // and is only emitted at debug level.
    shell.addShellDiagnostic(toolchain.userMessage());
    if (!toolchain.missing().isEmpty() && qEnvironmentVariableIsSet("VEDIT_TOOLCHAIN_TRACE")) {
        // Opt-in diagnosis only: the probed-candidate list must never be part of
        // the panel status or of a default console run.
        qDebug("diagnostics: %s", qPrintable(toolchain.probeDetail()));
    }

    const auto factory = [controller]() {
        auto *panel = new diagnostics::DiagnosticsPanel(controller);
        controller->refresh();
        return panel;
    };
    shell.panels()->setFactory(QStringLiteral("outline"), factory);

    // The shell inserts panels on demand; replace an existing placeholder now.
    if (QDockWidget *dock = shell.panels()->dock(QStringLiteral("outline"))) {
        QWidget *placeholder = dock->widget();
        if (placeholder != nullptr
            && placeholder->objectName().startsWith(QStringLiteral("panel-placeholder-"))) {
            dock->setWidget(factory());
            delete placeholder;
        }
    }

    if (shell.commands() != nullptr) {
        vedit::app::CommandSpec spec;
        spec.id = QStringLiteral("project.reparse");
        spec.text = QStringLiteral("Re&parse Project (Verible + SLANG)");
        spec.menu = QStringLiteral("project");
        spec.order = 50;
        if (shell.commands()->add(spec) == nullptr) {
            shell.commands()->action(spec.id);
        }
        shell.commands()->setHandler(spec.id, [controller]() { controller->reparse(); });
    }

    // T118: the active editor is followed whether or not a project was given.
    // (Before this, a plain "open a file" session had no analysis at all.)
    new ActiveFileWatcher(&shell, &shell, controller);
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
    new AnnotationBridge(&shell, &shell, controller);
#endif

    // T131: the user must FIND the diagnostics view. There was no "Diagnostics"
    // entry anywhere (the dock was only "Outline"), so add an explicit View-menu
    // command that shows the same dock, titled "Diagnostics".
    if (shell.commands() != nullptr) {
        vedit::app::CommandSpec spec;
        spec.id = QStringLiteral("view.panel.diagnostics");
        spec.text = QStringLiteral("&Diagnostics");
        spec.menu = QStringLiteral("view");
        spec.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+Alt+D"));
        spec.order = 160;
        shell.commands()->add(spec);
        shell.commands()->setHandler(spec.id, [&shell]() {
            shell.panels()->showPanel(QStringLiteral("outline"), &shell);
        });
    }

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
    if (!projectRoot.isEmpty()) {
        new ProjectFileSetPoller(&shell, controller, projectRoot);
    }
#else
    (void)projectRoot;
#endif
}

} // namespace vedit::workspace

#endif // VEDIT_DIAGNOSTICS_HAVE_APP_SHELL
