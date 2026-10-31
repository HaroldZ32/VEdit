#include "diagnostics_integration.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_APP_SHELL)

#include "diagnostics_controller.h"
#include "diagnostics_panel.h"
#include "lsp_backend.h"
#include "slang_backend.h"

#include "app_shell.h"
#include "command_registry.h"
#include "panel_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QDebug>
#include <QDockWidget>
#include <QWidget>

#include <memory>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)
#include <vd_ffi.h>
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

/// Resolve the Verible language server without a PATH search: the packaging
/// contract passes it explicitly, the environment can override it for tests,
/// and the AppDir keeps it next to the binary.
QString resolveVeribleLs(const QString &projectRoot)
{
    const QByteArray override = qgetenv("VEDIT_VERIBLE_LS");
    if (!override.isEmpty()) {
        return QString::fromLocal8Bit(override);
    }
    QStringList candidates;
    if (!projectRoot.isEmpty()) {
        candidates.append(QDir(projectRoot).filePath(QStringLiteral(".vedit/bin/verible-verilog-ls")));
    }
    candidates.append(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("bin/verible-verilog-ls")));
    candidates.append(QStringLiteral("/usr/bin/verible-verilog-ls"));
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

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
        qInfo("diagnostics: effective file set from the project model: %lld file(s)",
              static_cast<long long>(files.size()));
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

    auto lsp = diagnostics::createFfiLspSessionBackend();
    auto slang = diagnostics::createFfiSlangBackend();
    auto *holder = new ControllerHolder(
        std::make_unique<diagnostics::DiagnosticsController>(std::move(lsp), std::move(slang)),
        &shell);
    auto *controller = holder->controller();
    shell.setProperty("vedit.diagnosticsController", QVariant::fromValue<QObject *>(holder));

    const QString projectRoot = options.projectRoot;
    const QString lsPath = resolveVeribleLs(projectRoot);
    controller->setConfiguration(lsPath, projectRoot);
    if (lsPath.isEmpty()) {
        qWarning("diagnostics: no verible-verilog-ls found; diagnostics degrade diagnostically");
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
