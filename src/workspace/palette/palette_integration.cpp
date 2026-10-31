#include "palette_integration.h"

#include "command_palette.h"
#include "file_index.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QStatusBar>

namespace vedit::workspace {
namespace {

constexpr qint64 kMaxDemoEditorBytes = 4 * 1024 * 1024;

} // namespace

PaletteIntegration::PaletteIntegration(vedit::app::ApplicationShell *shell)
    : QObject(shell)
    , shell_(shell)
{
    index_ = std::make_unique<WorkspaceFileIndex>();
    palette_ = new CommandPalette(shell); // owned by the shell
    palette_->setOpenHandler([this](const QString &path) { openInEditor(path); });
}

PaletteIntegration::~PaletteIntegration() = default;

PaletteIntegration *PaletteIntegration::install(vedit::app::ApplicationShell &shell,
                                                const QString &projectRoot)
{
    if (PaletteIntegration *existing = instance(shell)) {
        existing->setProjectRoot(projectRoot);
        return existing;
    }
    auto *integration = new PaletteIntegration(&shell); // owned by the shell
    integration->setProjectRoot(projectRoot);
    integration->registerCommand();
    return integration;
}

PaletteIntegration *PaletteIntegration::instance(const vedit::app::ApplicationShell &shell)
{
    return shell.findChild<PaletteIntegration *>();
}

WorkspaceFileIndex *PaletteIntegration::fileIndex()
{
    return index_.get();
}

const WorkspaceFileIndex *PaletteIntegration::fileIndex() const
{
    return index_.get();
}

CommandPalette *PaletteIntegration::palette()
{
    return palette_;
}

QAction *PaletteIntegration::action() const
{
    return action_;
}

const QString &PaletteIntegration::openedPath() const
{
    return openedPath_;
}

void PaletteIntegration::setProjectRoot(const QString &projectRoot)
{
    index_->setRoot(projectRoot);
}

void PaletteIntegration::setFileOpenHandler(std::function<void(const QString &)> handler)
{
    openHandler_ = std::move(handler);
}

void PaletteIntegration::registerCommand()
{
    auto *commands = shell_->commands();
    if (commands == nullptr) {
        return;
    }

    vedit::app::CommandSpec spec;
    spec.id = QStringLiteral("file.goto-file");
    spec.text = QStringLiteral("&Go to File…");
    spec.menu = QStringLiteral("file");
    spec.defaultShortcut = QKeySequence(QStringLiteral("Ctrl+P"));
    spec.order = 25;

    action_ = commands->add(spec);
    if (action_ == nullptr) {
        action_ = commands->action(spec.id); // already registered
        return;
    }
    commands->setHandler(spec.id, [this]() { openFilePalette(); });

    // The shell built its menus in its constructor, before this integration ran,
    // so the new command is inserted into the File menu here (before Quit).
    if (QMenuBar *menuBar = shell_->menuBar()) {
        const QString fileLabel = commands->menuLabel(QStringLiteral("file"));
        const QList<QAction *> barActions = menuBar->actions();
        for (QAction *menuAction : barActions) {
            if (menuAction->text() != fileLabel) {
                continue;
            }
            QMenu *fileMenu = menuAction->menu();
            if (fileMenu == nullptr) {
                break;
            }
            QAction *quit = commands->action(QStringLiteral("file.quit"));
            if (quit != nullptr && fileMenu->actions().contains(quit)) {
                fileMenu->insertAction(quit, action_);
            } else {
                fileMenu->addAction(action_);
            }
            break;
        }
    }
    // QAction shortcuts fire only once the action is attached to a widget.
    shell_->addAction(action_);
}

void PaletteIntegration::openFilePalette()
{
    ensureIndexReady();
    palette_->setFileIndex(index_.get());
    palette_->openPalette();
}

void PaletteIntegration::ensureIndexReady()
{
    if (index_->root().isEmpty()) {
        return;
    }
    if (index_->scanCount() > 0) {
        return; // already walked once; the palette filters the cache
    }
    if (!index_->isEmpty()) {
        return; // a snapshot was published by the file tree: do not scan again
    }
    QElapsedTimer timer;
    timer.start();
    QString error;
    if (!index_->scan(&error)) {
        qWarning().noquote() << QStringLiteral("palette: cannot index workspace: %1").arg(error);
        return;
    }
    qInfo().noquote() << QStringLiteral("palette: indexed %1 files under %2 in %3 ms")
                             .arg(index_->size())
                             .arg(index_->root())
                             .arg(timer.elapsed());
}

void PaletteIntegration::openInEditor(const QString &absolutePath)
{
    openedPath_ = absolutePath;
    if (openHandler_) {
        openHandler_(absolutePath);
        return;
    }
    if (shell_ == nullptr) {
        return;
    }

    QPlainTextEdit *editor = nullptr;
    if (vedit::app::EditorPane *pane = shell_->editorArea()->activePane()) {
        editor = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
    }

    QString message;
    const QFileInfo info(absolutePath);
    if (!info.exists() || !info.isFile()) {
        message = QStringLiteral("Cannot open %1: not a file").arg(absolutePath);
    } else if (info.size() > kMaxDemoEditorBytes) {
        message = QStringLiteral("%1 is too large for the demo editor (%2 bytes)")
                      .arg(absolutePath)
                      .arg(info.size());
    } else if (editor == nullptr) {
        message = QStringLiteral("Open %1 (no editor seam connected yet)").arg(absolutePath);
    } else {
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly)) {
            message = QStringLiteral("Cannot read %1: %2").arg(absolutePath, file.errorString());
        } else {
            const QByteArray data = file.readAll();
            if (data.contains('\0')) {
                message = QStringLiteral("%1 looks binary; not opened in the demo editor")
                              .arg(absolutePath);
            } else {
                editor->setPlainText(QString::fromUtf8(data));
                editor->setProperty("vedit.openFilePath", absolutePath);
                message = QStringLiteral("Opened %1").arg(absolutePath);
            }
        }
    }
    shell_->statusBar()->showMessage(message, 5000);
}

void installPalette(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    PaletteIntegration::install(shell, options.projectRoot);
}

} // namespace vedit::workspace
