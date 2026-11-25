#include "app_shell.h"
#include "command_registry.h"
#include "diagnostics_controller.h"
#include "diagnostics_integration.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "module_tree_integration.h"
#include "module_tree_panel.h"
#include "project_integration.h"
#include "project_model.h"
#include "runtime_init.h"
#include "search/search_integration.h"
#include "toolchain.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

namespace {
using namespace Qt::StringLiterals;
namespace app = vedit::app;
namespace workspace = vedit::workspace;
namespace diagnostics = workspace::diagnostics;

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly)
        && file.write(bytes) == bytes.size();
}

bool waitFor(auto predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(10);
    } while (timer.elapsed() < timeoutMs);
    return false;
}

void ctrlClick(KTextEditor::View &view, int line, int column)
{
    view.setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    const QPoint point = view.cursorToCoordinate({line, column});
    QWidget *target = view.childAt(point);
    if (target == nullptr) {
        target = &view;
    }
    const QPoint local = target->mapFrom(&view, point);
    QMouseEvent press(QEvent::MouseButtonPress, local, target->mapToGlobal(local),
                      Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, target->mapToGlobal(local),
                        Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(target, &release);
}
} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication qt(argc, argv);
    const bool verible = qt.arguments().contains(u"--verible"_s);
    const auto tools = diagnostics::resolveToolchain(diagnostics::applicationToolchainContext());
    if (!tools.slang.found() || (verible ? !tools.verible.found() : !tools.slangServer.found())) {
        std::puts("SKIP: requires slang and the selected HDL language server");
        return 77;
    }
    const QByteArray topText("module top;\n  child u_child (.correct(1'b0));\n"
                             "  `include \"signal.svh\"\n  assign observed = local_signal;\nendmodule\n");
    QTemporaryDir temp;
    const QString root = temp.filePath(u"project"_s);
    const QString state = temp.filePath(u"state"_s);
    const QString top = root + u"/rtl/top.sv"_s;
    const QString wanted = root + u"/included/deep/child.sv"_s;
    const QString decoy = root + u"/rtl/child.sv"_s;
    const QString filelist = root + u"/sources.f"_s;
    const QString nested = root + u"/included/nested.f"_s;
    const QString header = root + u"/include/signal.svh"_s;
    const QString replacement = root + u"/lib/child.sv"_s;
    if (!temp.isValid() || !writeFile(top, topText)
        || !writeFile(wanted, "module child(input logic correct);\nendmodule\n")
        || !writeFile(replacement, "module child(input logic correct);\nendmodule : child\n")
        || !writeFile(decoy, "module child(input logic decoy);\nendmodule\n")
        || !writeFile(header, "logic local_signal;\nlogic observed;\n")
        || !writeFile(filelist, "rtl/top.sv\n+incdir+include\n-F included/nested.f\n")
        || !writeFile(nested, "deep/child.sv\n")
        || !writeFile(root + u"/verible.filelist"_s, "rtl/top.sv\nrtl/child.sv\n")
        || !writeFile(state + u"/settings.toml"_s, verible ? "\"tools.lsp_backend\" = \"verible\"\n"
                                                          : "\"tools.lsp_backend\" = \"slang\"\n")) {
        std::puts("FAIL: fixture creation");
        return 1;
    }
    auto editor = vedit::editor::makeEditCoreProvider();
    app::ShellOptions options;
    options.projectRoot = root;
    options.stateDir = state;
    options.restoreSession = false;
    app::ApplicationShell shell(options);
    shell.editorArea()->setProvider(editor.get());
    shell.editorArea()->ensurePane();
    workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = state;
    workspace::installSearch(shell, integration);
    workspace::installProject(shell, integration);
    workspace::installModuleTree(shell, integration);
    workspace::installDiagnostics(shell, integration);
    shell.resize(1200, 800);
    shell.show();

    auto *model = workspace::projectModelForShell(shell);
    auto *tree = shell.findChild<workspace::module_tree::ModuleTreePanel *>();
    auto *controller = workspace::diagnosticsControllerForShell(shell);
    auto *pane = shell.editorArea()->activePane();
    QString error;
    if (model == nullptr || tree == nullptr || controller == nullptr || pane == nullptr
        || !model->importFilelists({filelist}, &error)) {
        std::printf("FAIL: import: %s\n", qPrintable(error));
        return 1;
    }
    const bool listed = waitFor([&] {
        const auto catalog = tree->catalog();
        const auto *definition = catalog.find(u"child"_s);
        return definition != nullptr && QFileInfo(definition->file).canonicalFilePath() == wanted
            && controller->projectFiles().contains(wanted) && !controller->projectFiles().contains(decoy);
    });
    std::printf("TREE imported=%d path=%s excluded=%s\n", listed, qPrintable(wanted), qPrintable(decoy));
    if (!listed || !pane->openFile(decoy, &error)
        || !waitFor([&] {
            return controller->session().state == diagnostics::BackendState::Ready
                && controller->fileSetReport().files == QStringList{decoy};
        }) || !pane->openFile(top, &error)) {
        std::printf("FAIL: open fixture: %s\n", qPrintable(error));
        return 1;
    }
    auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    auto *definitionAction = shell.commands()->action(u"diagnostics.definition"_s);
    const bool ready = waitFor([&] {
        return controller->session().state == diagnostics::BackendState::Ready
            && controller->fileSetReport().files.contains(wanted)
            && definitionAction != nullptr && definitionAction->isEnabled();
    });
    if (!ready || view == nullptr) {
        std::printf("FAIL: LSP not ready: %s\n", qPrintable(controller->session().message));
        return 1;
    }
    if (verible) {
        ctrlClick(*view, 1, 4);
        const bool jumped = waitFor([&] { return pane->currentFilePath() != top; });
        const QString target = QFileInfo(pane->currentFilePath()).canonicalFilePath();
        const bool matched = jumped && target == wanted && controller->session().backendName == u"verible";
        std::printf("VERIBLE CTRL pass=%d canonical=%s\n", matched, qPrintable(target));
        return matched ? 0 : 1;
    }

    // Force the server to analyze editor text and show one positive semantic diagnostic.
    QString edited = QString::fromUtf8(topText);
    edited.insert(edited.lastIndexOf(u"endmodule"), u"  child u_probe (.intentional_missing(1'b0));\n");
    view->document()->setText(edited);
    QStringList messages;
    const bool diagnosed = waitFor([&] {
        messages.clear();
        for (const auto &row : controller->lowerRowsForFile(top)) {
            messages.append(row.message);
        }
        return messages.join(u'\n').contains(u"intentional_missing");
    });
    const QString diagnosticsText = messages.join(u'\n');
    const bool lint = diagnosed && !diagnosticsText.contains(u"'decoy'")
        && !diagnosticsText.contains(u"port 'correct' does not exist")
        && !controller->fileSetReport().files.contains(decoy);
    std::printf("LINT pass=%d backend=%s inputs=[%s] rows=[%s]\n", lint,
                qPrintable(controller->session().backendName),
                qPrintable(controller->fileSetReport().files.join(u',')), qPrintable(messages.join(u'|')));

    ctrlClick(*view, 1, 4);
    const bool jumped = waitFor([&] { return pane->currentFilePath() != top; });
    const QString target = QFileInfo(pane->currentFilePath()).canonicalFilePath();
    const bool navigation = jumped && target == wanted;
    std::printf("CTRL pass=%d canonical=%s\n", navigation, qPrintable(target));

    const auto jumpFromTop = [&](int line, int column, const QString &expected) {
        if (!pane->openFile(top, &error) || !waitFor([&] {
                return controller->session().state == diagnostics::BackendState::Ready
                    && definitionAction->isEnabled();
            })) {
            return false;
        }
        ctrlClick(*view, line, column);
        waitFor([&] { return pane->currentFilePath() != top; });
        const QString actual = QFileInfo(pane->currentFilePath()).canonicalFilePath();
        std::printf("CTRL expected=%s canonical=%s\n", qPrintable(expected), qPrintable(actual));
        return actual == expected;
    };
    // Resolve an ordinary signal whose declaration is in the included header.
    const bool include = jumpFromTop(3, 23, header);
    // Editing a nested file list changes the answer with the old target and decoy still open.
    if (!pane->openFile(top, &error) || !writeFile(nested, "../lib/child.sv\n")) {
        std::puts("FAIL: refresh fixture");
        return 1;
    }
    const quint64 starts = controller->lspStartCount();
    model->refresh();
    const bool refreshed = waitFor([&] {
        const auto catalog = tree->catalog();
        const auto *definition = catalog.find(u"child"_s);
        return definition != nullptr && QFileInfo(definition->file).canonicalFilePath() == replacement
            && controller->lspStartCount() > starts
            && controller->fileSetReport().files == QStringList{replacement, top}
            && controller->session().state == diagnostics::BackendState::Ready;
    });
    std::printf("REFRESH ready=%d inputs=[%s]\n", refreshed,
                qPrintable(controller->fileSetReport().files.join(u',')));
    const bool refreshNavigation = refreshed && jumpFromTop(1, 4, replacement);

    const QString looseRoot = temp.filePath(u"loose"_s);
    const QString looseTop = looseRoot + u"/loose.sv"_s;
    const QString looseChild = looseRoot + u"/child.sv"_s;
    if (!writeFile(looseTop, "module loose;\n  loose_child u_child ();\nendmodule\n")
        || !writeFile(looseChild, "module loose_child;\nendmodule\n")) {
        std::puts("FAIL: loose fixture");
        return 1;
    }
    shell.setProjectRoot(looseRoot);
    const bool looseReady = pane->openFile(looseTop, &error) && waitFor([&] {
        return !model->hasFilelist() && controller->fileSetReport().files.contains(looseTop)
            && controller->session().state == diagnostics::BackendState::Ready
            && definitionAction->isEnabled();
    });
    if (looseReady) {
        auto *looseView = qobject_cast<KTextEditor::View *>(pane->editorWidget());
        if (looseView != nullptr) {
            looseView->setCursorPosition({1, 4});
            definitionAction->trigger();
        }
    }
    const bool looseNavigation = looseReady && waitFor([&] {
        return QFileInfo(pane->currentFilePath()).canonicalFilePath() == looseChild;
    });
    std::printf("LOOSE pass=%d canonical=%s\n", looseNavigation, qPrintable(pane->currentFilePath()));
    return lint && navigation && include && refreshNavigation && looseNavigation ? 0 : 1;
}
