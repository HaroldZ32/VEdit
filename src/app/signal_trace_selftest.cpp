#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_context_menu.h"
#include "integration.h"
#include "jump/jump_registry.h"
#include "ktexteditor_provider.h"
#include "project/project_integration.h"
#include "project/project_model.h"
#include "runtime_init.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QLabel>
#include <QScreen>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <memory>
#include <cstdio>

namespace {
using vedit::app::ApplicationShell;
int failures = 0;

bool check(const char *name, bool ok)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += !ok;
    return ok;
}

bool until(const auto &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 8000) {
        QTest::qWait(10);
    }
    return predicate();
}

KTextEditor::View *view(ApplicationShell &shell)
{
    return qobject_cast<KTextEditor::View *>(shell.editorArea()->activePane()->editorWidget());
}

QTreeWidget *tree(ApplicationShell &shell)
{
    for (auto *dialog : shell.findChildren<QDialog *>(QStringLiteral("signal-trace-results"))) {
        if (dialog->isVisible()) {
            return dialog->findChild<QTreeWidget *>();
        }
    }
    return nullptr;
}

QTreeWidgetItem *row(QTreeWidget *tree, const QString &signal, const QString &detail = {})
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->text(0) == signal && (*it)->text(1).contains(detail)) {
            return *it;
        }
    }
    return nullptr;
}

void capture(QTreeWidget *tree, const QString &file)
{
    if (tree && !file.isEmpty()) {
        QWidget *window = tree->window();
        QTest::qWait(60);
        check("xcb screenshot", window->screen()->grabWindow(window->winId()).save(file));
    }
}
}

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3 || !QFileInfo(args[2]).isExecutable()) {
        std::printf("SKIP: real pinned slang binary required\n");
        return 77;
    }
    QTemporaryDir project;
    QTemporaryDir state;
    const QDir source(args[1]);
    const QDir root(project.path());
    for (const auto &name : {"sources.f", "top.sv", "leaf.sv", "cap.sv"}) {
        if (!QFile::copy(source.filePath(QString::fromLatin1(name)), root.filePath(QString::fromLatin1(name)))) {
            return 1;
        }
    }
    vedit::app::ShellOptions options;
    options.projectRoot = project.path();
    options.stateDir = state.path();
    options.restoreSession = false;
    options.showDefaultPanels = false;
    ApplicationShell shell(options);
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir, true});
    shell.setProperty("vedit.diagnostics.toolchainSlang", args[2]);
    shell.startSession();
    shell.show();
    const auto top = root.filePath(QStringLiteral("top.sv"));
    const auto leaf = root.filePath(QStringLiteral("leaf.sv"));
    if (!check("open editor", shell.openPaths({top}) && view(shell))) {
        return 1;
    }
    auto *driver = shell.commands()->action(QStringLiteral("signal.trace.driver"));
    auto *load = shell.commands()->action(QStringLiteral("signal.trace.load"));
    auto *drivers = shell.commands()->action(QStringLiteral("signal.trace.driver.chain"));
    auto *loads = shell.commands()->action(QStringLiteral("signal.trace.load.chain"));
    auto *model = vedit::workspace::projectModelForShell(shell);
    if (!check("trace actions and project model", driver && load && drivers && loads && model)) {
        return 1;
    }
    check("folder alone disables trace", !driver->isEnabled() && model->files().isEmpty());
    check("real filelist import", model->importFilelists({root.filePath(QStringLiteral("sources.f"))}));
    if (!check("import enables trace", until([&] { return driver->isEnabled(); }))) {
        return 1;
    }
    view(shell)->setCursorPosition({5, 19});
    auto *context = vedit::editor::EditorContextMenu::instanceFor(view(shell));
    std::unique_ptr<QMenu> menu(context ? context->buildMenu() : nullptr);
    check("editor menu leaves definition and references and adds trace", menu
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.definition"))
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.references"))
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.signal.trace.driver")));
    QAction *direct = menu ? menu->findChild<QAction *>(QStringLiteral("vedit.action.signal.trace.driver")) : nullptr;
    if (!direct) { return 1; }
    QElapsedTimer cold;
    cold.start();
    direct->trigger();
    if (!check("single driver opens across files", until([&] {
            return view(shell)->document()->url().toLocalFile() == leaf;
        }))) {
        std::printf("status: %s\n", qPrintable(shell.statusBar()->currentMessage()));
        return 1;
    }
    std::printf("observed action to jump ms=%lld\n", static_cast<long long>(cold.elapsed()));
    check("driver jumps to child port declaration", view(shell)->cursorPosition() == KTextEditor::Cursor(0, 56));
    view(shell)->setCursorPosition({0, 56});
    load->trigger();
    if (!check("duplicate instances open direct chooser", until([&] { return tree(shell); }))) { return 1; }
    check("chooser distinguishes both paths", row(tree(shell), QStringLiteral("top.u0.q"))
          && row(tree(shell), QStringLiteral("top.u1.q")));
    capture(tree(shell), args.value(3));
    tree(shell)->window()->close();
    view(shell)->setCursorPosition({0, 41});
    loads->trigger();
    if (!check("load chain across module boundary", until([&] { return tree(shell); }))) { return 1; }
    auto *loadTree = tree(shell);
    check("load chain branches to both sinks", row(loadTree, QStringLiteral("top.out1"))
          && row(loadTree, QStringLiteral("top.out2")));
    capture(loadTree, args.value(5));
    loadTree->window()->close();
    check("open top again", shell.openPaths({top}));
    view(shell)->setCursorPosition({5, 10});
    drivers->trigger();
    if (!check("driver chain opens", until([&] { return tree(shell); }))) { return 1; }
    auto *driverTree = tree(shell);
    check("driver walks both instance bodies", row(driverTree, QStringLiteral("top.u0.state"))
          && row(driverTree, QStringLiteral("top.u1.state")));
    capture(driverTree, args.value(4));
    if (auto *target = row(driverTree, QStringLiteral("top.u0.q"))) {
        const auto before = vedit::workspace::jump::Registry::instance().deliveredCount();
        driverTree->setCurrentItem(target);
        QTest::keyClick(driverTree, Qt::Key_Return);
        check("keyboard activates a tree endpoint", until([&] {
            return view(shell)->document()->url().toLocalFile() == leaf;
        }));
        check("one keyboard activation delivers one jump",
              vedit::workspace::jump::Registry::instance().deliveredCount() == before + 1);
    }
    driverTree->window()->close();
    check("return to top", shell.openPaths({top}));
    view(shell)->setCursorPosition({8, 10});
    drivers->trigger();
    if (!check("cycle result opens", until([&] { return tree(shell); }))) { return 1; }
    check("cycle terminates visibly", row(tree(shell), QStringLiteral("top.cycle_a"), QStringLiteral("Cycle")));
    tree(shell)->window()->close();
    view(shell)->setCursorPosition({11, 16});
    drivers->trigger();
    if (!check("inout driver tree opens", until([&] { return tree(shell); }))) { return 1; }
    check("inout includes a child driver", row(tree(shell), QStringLiteral("top.io0.pad"), QStringLiteral("assign")));
    tree(shell)->window()->close();
    loads->trigger();
    if (!check("inout load tree opens", until([&] { return tree(shell); }))) { return 1; }
    check("inout includes a child load", row(tree(shell), QStringLiteral("top.io0.readback")));
    QFile changed(leaf);
    const bool edited = changed.open(QIODevice::Append) && changed.write("\n") == 1;
    changed.close();
    check("external source edit invalidates displayed cache", edited && until([&] { return !tree(shell); }));
    view(shell)->setCursorPosition({0, 7});
    drivers->trigger();
    check("non-signal cursor has explicit empty feedback", until([&] {
        return shell.statusBar()->currentMessage().contains(QStringLiteral("No elaborated signal"));
    }));
    const auto capped = root.filePath(QStringLiteral("cap.sv"));
    check("open cap fixture", shell.openPaths({capped}));
    view(shell)->setCursorPosition({1, 7});
    loads->trigger();
    if (!check("capped load tree opens", until([&] { return tree(shell); }))) { return 1; }
    bool warned = false;
    for (auto *label : tree(shell)->window()->findChildren<QLabel *>()) {
        warned |= label->text().contains(QStringLiteral("32 levels"));
    }
    check("depth cap is visible and omits deep endpoints", warned
          && row(tree(shell), QStringLiteral("cap.n32"))
          && !row(tree(shell), QStringLiteral("cap.n40")));
    std::printf("failures=%d\n", failures);
    return failures != 0;
}
