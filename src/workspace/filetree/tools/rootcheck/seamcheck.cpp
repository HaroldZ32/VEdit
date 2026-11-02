// Root-context seam check (task T21): proves the file tree panel really becomes
// the shell's `file-tree` dock content through installFileTree(), without the
// repository's src/app files being modified by this lane.
//
// The double-click contract is checked on the PRODUCT side of the seam
// (verdict §8.1, repair task T39): a double click on a *file* row must load the
// file into the shell's active editor pane and record the path there; a double
// click on a *folder* row must only expand it and leave the editor untouched.
// The checks below therefore assert editor content and the `vedit.openFilePath`
// property, not just that a signal was emitted or that the status bar changed.

#include "app_shell.h"
#include "editor_area.h"
#include "panel_registry.h"

#include "file_tree_model.h"
#include "file_tree_widget.h"
#include "filetree_integration.h"
#include "integration.h"

#if defined(VEDIT_ROOTCHECK_HAVE_EDIT_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Document>
#endif

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTreeView>
#include <QtTest/QTest>

#include <cstdio>

using vedit::app::ApplicationShell;
using vedit::app::ShellOptions;
using vedit::workspace::FileTreeWidget;
using vedit::workspace::IntegrationOptions;

namespace {

void pump(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-filetree-seamcheck"));

    QString project;
    QString stateDir;
    QString screenshot;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const auto value = [&args, &i]() { return i + 1 < args.size() ? args.at(++i) : QString(); };
        if (arg == QLatin1String("--project")) {
            project = value();
        } else if (arg == QLatin1String("--state")) {
            stateDir = value();
        } else if (arg == QLatin1String("--screenshot")) {
            screenshot = value();
        } else {
            std::printf("unknown argument: %s\n", qPrintable(arg));
            return 2;
        }
    }
    if (project.isEmpty() || stateDir.isEmpty()) {
        std::printf("usage: vedit-filetree-seamcheck --project <dir> --state <dir>"
                    " [--screenshot <png>]\n");
        return 2;
    }
    // Compare absolute paths everywhere: the recorded file path is absolute, so
    // a relative --project argument must not turn a real open into a failure.
    project = QFileInfo(project).absoluteFilePath();
    stateDir = QFileInfo(stateDir).absoluteFilePath();
    QDir().mkpath(stateDir);

    ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = true;
    ApplicationShell shell(options);
    shell.resize(1280, 800);

    QDockWidget *dock = shell.panels()->dock(QStringLiteral("file-tree"));
    const QWidget *beforeInstall = dock != nullptr ? dock->widget() : nullptr;
    std::printf("SEAM dock-inserted=%d content-before-install=%s\n", dock != nullptr ? 1 : 0,
                beforeInstall != nullptr ? qPrintable(beforeInstall->objectName()) : "none");

    IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = stateDir;
    // The production entry point: src/app/main.cpp calls exactly this, and it
    // dispatches to installFileTree() through src/workspace/integration.cpp.
    vedit::workspace::installPanels(shell, integration);
    vedit::workspace::installPanels(shell, integration); // idempotent by design

    auto *panel = qobject_cast<FileTreeWidget *>(dock != nullptr ? dock->widget() : nullptr);
    auto *found = vedit::workspace::fileTreeWidgetForShell(shell);
    std::printf("SEAM panel-installed=%d dock-content-is-panel=%d find-child=%d root=%s\n",
                panel != nullptr ? 1 : 0, panel != nullptr ? 1 : 0, found != nullptr ? 1 : 0,
                panel != nullptr ? qPrintable(panel->rootPath()) : "none");

    int checks = 0;
    int failures = 0;
    const auto check = [&checks, &failures](const char *name, bool condition,
                                            const QString &detail = QString()) {
        ++checks;
        if (!condition) {
            ++failures;
        }
        std::printf("SEAM CHECK %s ok=%d%s%s\n", name, condition ? 1 : 0,
                    detail.isEmpty() ? "" : " detail=", qPrintable(detail));
    };

    check("panel-installed", panel != nullptr && found != nullptr);
    check("root-path", panel != nullptr && panel->rootPath() == QDir(project).absolutePath(),
          panel != nullptr ? panel->rootPath() : QString());

#if defined(VEDIT_ROOTCHECK_HAVE_EDIT_CORE)
    // Regression T60: the PRODUCT installs the KTextEditor editor provider. The
    // double-click assertions below must therefore run against that provider,
    // not against the demo QPlainTextEdit (which is what made T39's evidence
    // green while the real product stopped opening files).
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    check("editor.provider", shell.editorProvider() != nullptr
                                 && shell.editorProvider()->id() == QStringLiteral("ktexteditor"),
          shell.editorProvider() != nullptr ? shell.editorProvider()->id() : QString());
#endif

    shell.show();
    pump(250);
    if (panel != nullptr) {
        shell.panels()->hidePanel(QStringLiteral("outline"));
        shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
        pump(150);
    }

    // --- the real product behaviour of a double click (verdict §8.1 H1) ------
    // The editor is whatever provider the product installs: the widget is read
    // through the pane's provider seam, never through a QPlainTextEdit cast.
    vedit::app::EditorPane *activePane = shell.editorArea()->activePane();
    check("editor.active-pane-widget", activePane != nullptr);
#if defined(VEDIT_ROOTCHECK_HAVE_EDIT_CORE)
    check("editor.ktexteditor-widget",
          activePane != nullptr && vedit::editor::isEditCoreWidget(activePane->editorWidget()));
#endif

    const auto editorPath = [activePane]() {
        return activePane != nullptr ? activePane->currentFilePath() : QString();
    };
    const auto editorText = [activePane]() {
#if defined(VEDIT_ROOTCHECK_HAVE_EDIT_CORE)
        KTextEditor::Document *document =
            activePane != nullptr ? vedit::editor::documentFor(activePane->editorWidget())
                                  : nullptr;
        if (document != nullptr) {
            return document->text();
        }
#endif
        return QString();
    };
    const QString initialText = editorText();
    std::printf("SEAM editor-initial-bytes=%d\n", static_cast<int>(initialText.size()));
    check("editor.initially-no-file", activePane != nullptr && editorPath().isEmpty());

    if (panel != nullptr) {
        // A real mouse double click on the row, not a direct call to activateIndex:
        // the point is the whole path from the view event to the editor content.
        const QString topPath = QDir(project).filePath(QStringLiteral("rtl/top.v"));
        const QByteArray topOnDisk = readFile(topPath);
        const QModelIndex fileIndex = panel->model()->indexForPath(topPath, true);
        std::printf("SEAM file-index-valid=%d\n", fileIndex.isValid() ? 1 : 0);
        panel->selectPath(topPath);
        pump(80);
        const QRect fileRect = panel->view()->visualRect(fileIndex);
        check("dblclick.file-row-visible", fileRect.isValid() && !fileRect.isEmpty());
        QTest::mouseClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          fileRect.center());
        QTest::mouseDClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                           fileRect.center());
        pump(150);
        const QString message = shell.statusBar()->currentMessage();
        std::printf("SEAM status-after-file-dblclick=\"%s\"\n", qPrintable(message));
        check("dblclick.file-loads-bytes",
              !topOnDisk.isEmpty() && editorText() == QString::fromUtf8(topOnDisk)
                  && editorText() != initialText,
              QString::number(topOnDisk.size()) + QStringLiteral(" bytes on disk"));
        check("dblclick.file-records-path", editorPath() == topPath, editorPath());
        // The pre-repair evidence asserted this status-bar text; it is kept
        // unchanged next to the editor-state assertions above.
        check("dblclick.file-status-message",
              message.contains(QStringLiteral("Opened"))
                  && message.contains(QStringLiteral("top.v")),
              message);

        // A second file must replace the content: this proves the bytes are read
        // per open, not a property written once.
        const QString aluPath = QDir(project).filePath(QStringLiteral("rtl/alu.sv"));
        const QByteArray aluOnDisk = readFile(aluPath);
        const QModelIndex aluIndex = panel->model()->indexForPath(aluPath, true);
        panel->selectPath(aluPath);
        pump(80);
        const QRect aluRect = panel->view()->visualRect(aluIndex);
        check("dblclick.second-file-row-visible", aluRect.isValid() && !aluRect.isEmpty());
        QTest::mouseClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          aluRect.center());
        QTest::mouseDClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                           aluRect.center());
        pump(150);
        check("dblclick.second-file-loads",
              !aluOnDisk.isEmpty() && aluOnDisk != topOnDisk
                  && editorText() == QString::fromUtf8(aluOnDisk));
        check("dblclick.second-file-records-path", editorPath() == aluPath, editorPath());

        // A folder double click expands the row and must not open anything: the
        // editor keeps the last file and the panel emits no open request.
        const QString tbPath = QDir(project).filePath(QStringLiteral("tb"));
        const QModelIndex tbIndex = panel->model()->indexForPath(tbPath, true);
        QSignalSpy openSpy(panel, &FileTreeWidget::openFileRequested);
        panel->view()->setExpanded(tbIndex, false);
        panel->selectPath(tbPath);
        pump(80);
        const QRect tbRect = panel->view()->visualRect(tbIndex);
        check("dblclick.folder-row-visible", tbRect.isValid() && !tbRect.isEmpty());
        QTest::mouseClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tbRect.center());
        QTest::mouseDClick(panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                           tbRect.center());
        pump(150);
        check("dblclick.folder-expands", panel->view()->isExpanded(tbIndex));
        check("dblclick.folder-emits-no-open", openSpy.isEmpty(),
              QString::number(openSpy.count()));
        check("dblclick.folder-keeps-editor",
              editorPath() == aluPath && editorText() == QString::fromUtf8(aluOnDisk));

        // Laziness is observable from the real shell too.
        const QModelIndex rootIndex = panel->model()->index(0, 0, QModelIndex());
        std::printf("SEAM rows=%d scanned-dirs=%d\n",
                    panel->model()->rowCount(rootIndex), panel->model()->scannedDirectories());
    }

    bool ok = failures == 0;

    shell.panels()->showPanel(QStringLiteral("file-tree"), &shell);
    pump(150);
    if (!screenshot.isEmpty()) {
        const bool saved = shell.grab().save(screenshot);
        std::printf("SCREENSHOT ok=%d %s\n", saved ? 1 : 0, qPrintable(screenshot));
        ok = saved && ok;
    }

    std::printf("FILETREE-SEAMCHECK ok=%d checks=%d failures=%d\n", ok ? 1 : 0, checks, failures);
    return ok ? 0 : 1;
}
