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
#include "settings/settings_dialog.h"
#include "settings/settings_panel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
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

bool until(const auto &predicate, qint64 timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
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

QTreeWidgetItem *hop(QTreeWidget *tree, const char *from, const char *to, const char *detail = "")
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->parent() && (*it)->text(0) == QLatin1String(from)
            && (*it)->text(1) == QLatin1String(to) && (*it)->text(2).contains(QLatin1String(detail))) {
            return *it;
        }
    }
    return nullptr;
}

void pressShortcut(ApplicationShell &shell, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    shell.activateWindow();
    view(shell)->setFocus();
    QTest::qWait(20);
    QTest::keyClick(view(shell), key, modifiers);
}

void dragFirstColumn(QTreeWidget *tree)
{
    auto *header = tree->header();
    auto *viewport = header->viewport();
    const QPoint start(header->sectionViewportPosition(0) + header->sectionSize(0) - 1,
                       viewport->height() / 2);
    const QPoint end = start + QPoint(84, 0);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    QApplication::processEvents();
}

int shortcutRow(QTableWidget *table, const QString &id)
{
    if (table != nullptr) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text().endsWith(QStringLiteral("(%1)").arg(id))) {
                return row;
            }
        }
    }
    return -1;
}

void capture(QTreeWidget *tree, const QString &file)
{
    if (tree && !file.isEmpty()) {
        QWidget *window = tree->window();
        QTest::qWait(60);
        check("xcb screenshot", window->screen()->grabWindow(window->winId()).save(file));
    }
}

QString prepareOversizeFixture(const QDir &root, const QString &slang)
{
    QFile noise(root.filePath(QStringLiteral("noise.sv")));
    if (!noise.open(QIODevice::WriteOnly)) {
        return {};
    }
    for (int i = 0; i < 1000; ++i) {
        QByteArray module = "module noise_" + QByteArray::number(i)
            + " (input logic clk, input logic [7:0] d, output logic [7:0] q);\n"
              "  logic [7:0] acc;\n  always_ff @(posedge clk) acc <= d;\n  assign q = acc;\n";
        for (int signal = 0; signal < 20; ++signal) {
            const auto id = QByteArray::number(signal);
            module += "  wire [7:0] n" + id + ";\n  assign n" + id + " = d ^ 8'd" + id + ";\n";
        }
        if (i % 10 != 0) {
            module += "  noise_" + QByteArray::number(i - 1) + " child (.clk(clk), .d(d ^ 8'd1), .q());\n";
        }
        module += "endmodule\n";
        if (noise.write(module) != module.size()) {
            return {};
        }
    }
    noise.close();
    QFile filelist(root.filePath(QStringLiteral("sources.f")));
    if (!filelist.open(QIODevice::Append) || filelist.write("noise.sv\n") != 9) {
        return {};
    }
    filelist.close();

    // Read the size of the actual trace dump while its temp file is still alive.
    // Catalog invocations have no --Mall and do not contribute to the count.
    QFile wrapper(root.filePath(QStringLiteral("trace-slang")));
    const QByteArray script = R"(#!/bin/sh
previous=
for argument do
    case "$previous" in
        --ast-json) output=$argument ;;
        --Mall) trace=yes ;;
    esac
    previous=$argument
done
if [ "$trace" = yes ]; then
    probe_dir=${VEDIT_TRACE_TEST_SIZES%/*}
    if [ -f "$probe_dir/hold-trace" ]; then
        printf '%s\n' "$$" > "$probe_dir/held-trace-pid"
        exec sleep 30
    fi
    if [ -s "$probe_dir/held-trace-pid" ]; then
        read -r stale < "$probe_dir/held-trace-pid"
        if kill -0 "$stale" 2>/dev/null; then
            : > "$probe_dir/trace-overlap"
        fi
    fi
fi
"$VEDIT_TRACE_TEST_SLANG" "$@"
result=$?
if [ "$trace" = yes ] && [ -f "$output" ]; then
    wc -c < "$output" >> "$VEDIT_TRACE_TEST_SIZES"
fi
exit "$result"
)";
    if (!wrapper.open(QIODevice::WriteOnly) || wrapper.write(script) != script.size()) {
        return {};
    }
    wrapper.close();
    if (!wrapper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        return {};
    }
    qputenv("VEDIT_TRACE_TEST_SLANG", QFile::encodeName(QFileInfo(slang).absoluteFilePath()));
    qputenv("VEDIT_TRACE_TEST_SIZES", QFile::encodeName(root.filePath(QStringLiteral("trace-sizes"))));
    return wrapper.fileName();
}

int verifyOversize(std::unique_ptr<ApplicationShell> &owner, const QDir &root,
                   QAction *driver, QAction *load, QAction *loads)
{
    auto &shell = *owner;
    const auto top = root.filePath(QStringLiteral("top.sv"));
    const auto leaf = root.filePath(QStringLiteral("leaf.sv"));
    QTimer heartbeat;
    QElapsedTimer clock;
    qint64 previous = 0;
    qint64 longestGap = 0;
    int beats = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
        const auto now = clock.elapsed();
        longestGap = std::max(longestGap, now - previous);
        previous = now;
        ++beats;
    });
    view(shell)->setCursorPosition({1, 7});
    clock.start();
    heartbeat.start(10);
    driver->trigger();
    const bool jumpedToDriver = until([&] {
        return view(shell)->document()->url().toLocalFile() == leaf;
    }, 20000);
    heartbeat.stop();
    std::printf("oversize cold action elapsed_ms=%lld event_ticks=%d max_gap_ms=%lld\n",
                static_cast<long long>(clock.elapsed()), beats, static_cast<long long>(longestGap));
    check("oversize driver jumps to child output declaration", jumpedToDriver
          && view(shell)->cursorPosition() == KTextEditor::Cursor(0, 56));
    if (!jumpedToDriver || !shell.openPaths({top})) {
        std::printf("status: %s\n", qPrintable(shell.statusBar()->currentMessage()));
        return 1;
    }
    view(shell)->setCursorPosition({1, 7});
    clock.restart();
    load->trigger();
    check("oversize load jumps to the same signal's RHS occurrence", until([&] {
        return view(shell)->document()->url().toLocalFile() == top
            && view(shell)->cursorPosition() == KTextEditor::Cursor(5, 18);
    }));
    std::printf("oversize warm action elapsed_ms=%lld\n", static_cast<long long>(clock.elapsed()));
    QFile sizes(root.filePath(QStringLiteral("trace-sizes")));
    if (!check("measure the real trace AST", sizes.open(QIODevice::ReadOnly))) {
        return 1;
    }
    const auto bytes = sizes.readLine().trimmed().toLongLong();
    std::printf("oversize trace ast_bytes=%lld\n", static_cast<long long>(bytes));
    check("compiled trace AST exceeds 96 MiB", bytes > 100663296);
    check("driver and load reuse one elaboration", sizes.atEnd());
    sizes.close();

    const auto changeSource = [&leaf] {
        QFile input(leaf);
        return input.open(QIODevice::Append) && input.write("\n") == 1 && input.flush();
    };
    view(shell)->setCursorPosition({1, 7});
    loads->trigger();
    if (!check("cached trace can show its result before invalidation", tree(shell))) {
        return 1;
    }
    clock.restart();
    check("source edit invalidates the cached large trace", changeSource() && until([&] {
        return tree(shell) == nullptr;
    }));
    std::printf("oversize cached invalidation elapsed_ms=%lld\n", static_cast<long long>(clock.elapsed()));

    QFile hold(root.filePath(QStringLiteral("hold-trace")));
    if (!hold.open(QIODevice::WriteOnly)) {
        return 1;
    }
    hold.close();
    view(shell)->setCursorPosition({1, 7});
    driver->trigger();
    QFile heldPid(root.filePath(QStringLiteral("held-trace-pid")));
    if (!check("trace starts a controlled pending job", until([&] { return heldPid.size() > 0; }))) {
        return 1;
    }
    if (!heldPid.open(QIODevice::ReadOnly) || !hold.remove()) {
        return 1;
    }
    const auto process = QStringLiteral("/proc/") + QString::fromUtf8(heldPid.readAll().trimmed());
    const bool invalidated = changeSource() && until([&] {
        return shell.statusBar()->currentMessage().contains(QStringLiteral("Project inputs changed"));
    });
    check("second edit cancels the active generation", invalidated);
    if (!invalidated) {
        return 1;
    }
    clock.restart();
    driver->trigger();
    check("replacement trace returns the driver", until([&] {
        return view(shell)->document()->url().toLocalFile() == leaf
            && view(shell)->cursorPosition() == KTextEditor::Cursor(0, 56);
    }, 20000));
    std::printf("oversize replacement elapsed_ms=%lld\n", static_cast<long long>(clock.elapsed()));
    check("stale compiler exited before replacement started", !QFileInfo::exists(process)
          && !root.exists(QStringLiteral("trace-overlap")));

    clock.restart();
    owner.reset();
    std::printf("oversize window destruction elapsed_ms=%lld\n", static_cast<long long>(clock.elapsed()));
    return failures != 0;
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
    const bool oversize = args.value(3) == QStringLiteral("--oversize");
    const auto traceSlang = oversize ? prepareOversizeFixture(root, args[2]) : args[2];
    if (traceSlang.isEmpty()) {
        std::printf("could not prepare oversize fixture\n");
        return 1;
    }
    vedit::app::ShellOptions options;
    options.projectRoot = project.path();
    options.stateDir = state.path();
    options.restoreSession = false;
    options.deferSessionRestore = true;
    options.showDefaultPanels = false;
    auto ownedShell = std::make_unique<ApplicationShell>(options);
    auto &shell = *ownedShell;
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    vedit::workspace::installPanels(shell, {options.projectRoot, options.stateDir, true});
    shell.setProperty("vedit.diagnostics.toolchainSlang", traceSlang);
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
    const auto *projectMenu = shell.findChild<QMenu *>(QStringLiteral("menu-project"));
    check("Project menu owns the four live trace actions", projectMenu
          && projectMenu->actions().contains(driver) && projectMenu->actions().contains(load)
          && projectMenu->actions().contains(drivers) && projectMenu->actions().contains(loads));
    check("folder alone disables trace", !driver->isEnabled() && model->files().isEmpty());
    check("real filelist import", model->importFilelists({root.filePath(QStringLiteral("sources.f"))}));
    if (!check("import enables trace", until([&] { return driver->isEnabled(); }))) {
        return 1;
    }
    if (oversize) {
        return verifyOversize(ownedShell, root, driver, load, loads);
    }
    vedit::workspace::settings::SettingsDialog preferences(shell.themes(), shell.commands(),
                                                          state.filePath(QStringLiteral("settings.toml")));
    check("Preferences loaded", preferences.panel()->flush());
    auto *keys = preferences.findChild<QTableWidget *>(QStringLiteral("settings-keys"));
    check("all four trace defaults are unique and conflict-free", [&] {
        for (auto *action : {driver, load, drivers, loads}) {
            const QString id = action->objectName().sliced(7);
            const auto sequence = action->shortcut();
            const int index = shortcutRow(keys, id);
            if (sequence.isEmpty() || shell.commands()->shortcutOwnerIds(sequence) != QStringList{id}
                || !shell.commands()->foreignShortcutOwners(sequence).isEmpty() || index < 0
                || keys->item(index, 1)->text() != sequence.toString()
                || keys->item(index, 2)->text() != sequence.toString()) {
                return false;
            }
        }
        return true;
    }());
    view(shell)->setCursorPosition({5, 19});
    auto *context = vedit::editor::EditorContextMenu::instanceFor(view(shell));
    std::unique_ptr<QMenu> menu(context ? context->buildMenu() : nullptr);
    check("editor menu leaves definition and references and adds trace", menu
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.definition"))
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.references"))
          && menu->findChild<QAction *>(QStringLiteral("vedit.action.signal.trace.driver")));
    check("editor menu displays all four trace shortcuts", menu && [&] {
        for (auto *action : {driver, load, drivers, loads}) {
            const auto *item = menu->findChild<QAction *>(QStringLiteral("vedit.action.") + action->objectName().sliced(7));
            if (item == nullptr || !item->text().endsWith(QLatin1Char('\t') + action->shortcut().toString())) {
                return false;
            }
        }
        return true;
    }());
    QElapsedTimer cold;
    cold.start();
    pressShortcut(shell, Qt::Key_F7);
    if (!check("F7 single driver opens across files", until([&] {
            return view(shell)->document()->url().toLocalFile() == leaf;
        }))) {
        std::printf("status: %s\n", qPrintable(shell.statusBar()->currentMessage()));
        return 1;
    }
    std::printf("observed action to jump ms=%lld\n", static_cast<long long>(cold.elapsed()));
    check("driver jumps to child port declaration", view(shell)->cursorPosition() == KTextEditor::Cursor(0, 56));
    view(shell)->setCursorPosition({0, 56});
    pressShortcut(shell, Qt::Key_F9);
    if (!check("F9 duplicate instances open direct chooser", until([&] { return tree(shell); }))) { return 1; }
    auto *directTree = tree(shell);
    check("chooser distinguishes both paths", row(directTree, QStringLiteral("top.u0.q"))
          && row(directTree, QStringLiteral("top.u1.q")));
    check("direct chooser stays one hop", directTree->columnCount() == 3
          && directTree->topLevelItem(0)->child(0)->childCount() == 0);
    capture(directTree, args.value(3));
    const int directInitialWidth = directTree->columnWidth(0);
    dragFirstColumn(directTree);
    const int directWidth = directTree->columnWidth(0);
    directTree->window()->close();
    pressShortcut(shell, Qt::Key_F9);
    if (!check("direct chooser reopens", until([&] { return tree(shell); }))) { return 1; }
    check("direct column size survives reopening", directWidth > directInitialWidth
          && tree(shell)->columnWidth(0) == directWidth);
    tree(shell)->window()->close();
    view(shell)->setCursorPosition({0, 41});
    pressShortcut(shell, Qt::Key_F9, Qt::ShiftModifier);
    if (!check("Shift+F9 load chain across module boundary", until([&] { return tree(shell); }))) { return 1; }
    auto *loadTree = tree(shell);
    auto *merger = hop(loadTree, "top.first", "top.merged", "assign");
    auto *sink1 = hop(loadTree, "top.merged", "top.out1", "assign");
    auto *sink2 = hop(loadTree, "top.merged", "top.out2", "assign");
    check("load chain follows the actual signal and port sequence", hop(loadTree, "top.u0.d", "top.u0.state", "AlwaysFF")
          && hop(loadTree, "top.u0.state", "top.u0.q", "assign")
          && hop(loadTree, "top.u0.q", "top.first", "Port q (Out)")
          && hop(loadTree, "top.u1.q", "top.second", "Port q (Out)")
          && !hop(loadTree, "top.u0.q", "top.second"));
    check("load chain branches at the same signal", merger && sink1 && sink2
          && sink1->parent() == merger && sink2->parent() == merger);
    capture(loadTree, args.value(5));
    const int initialWidth = loadTree->columnWidth(0);
    dragFirstColumn(loadTree);
    const int chainWidth = loadTree->columnWidth(0);
    check("chain header drag enables horizontal scrolling", chainWidth > initialWidth
          && loadTree->horizontalScrollBar()->maximum() > 0);
    loadTree->window()->close();
    check("open top again", shell.openPaths({top}));
    view(shell)->setCursorPosition({5, 10});
    pressShortcut(shell, Qt::Key_F7, Qt::ShiftModifier);
    if (!check("Shift+F7 driver chain opens", until([&] { return tree(shell); }))) { return 1; }
    auto *driverTree = tree(shell);
    auto *first = hop(driverTree, "top.first", "top.merged", "assign");
    auto *second = hop(driverTree, "top.second", "top.merged", "assign");
    auto *port = hop(driverTree, "top.u0.q", "top.first", "Port q (Out)");
    auto *assignment = hop(driverTree, "top.u0.state", "top.u0.q", "assign");
    check("driver upstream walk preserves direction and both branches", first && second && port && assignment
          && first->parent() == second->parent() && port->parent() == first && assignment->parent() == port
          && hop(driverTree, "top.a", "top.u0.d", "Port d (In)")
          && hop(driverTree, "top.b", "top.u1.d", "Port d (In)")
          && !hop(driverTree, "top.first", "top.u0.q"));
    check("chain column size survives reopening in the opposite direction", driverTree->columnWidth(0) == chainWidth);
    capture(driverTree, args.value(4));
    if (auto *target = port) {
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
    check("cycle terminates visibly", hop(tree(shell), "top.cycle_a", "top.cycle_b", "Cycle"));
    tree(shell)->window()->close();
    view(shell)->setCursorPosition({11, 16});
    drivers->trigger();
    if (!check("inout driver tree opens", until([&] { return tree(shell); }))) { return 1; }
    check("inout child-to-parent driver and terminal assign are explicit", hop(tree(shell), "top.io0.pad", "top.bidi", "Port pad (InOut)")
          && hop(tree(shell), "No upstream signal identified", "top.io0.pad", "assign"));
    tree(shell)->window()->close();
    loads->trigger();
    if (!check("inout load tree opens", until([&] { return tree(shell); }))) { return 1; }
    check("inout parent-to-child load is explicit", hop(tree(shell), "top.bidi", "top.io0.pad", "Port pad (InOut)")
          && hop(tree(shell), "top.io0.pad", "top.io0.readback", "assign"));
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
          && hop(tree(shell), "cap.n31", "cap.n32")
          && !hop(tree(shell), "cap.n39", "cap.n40"));
    capture(tree(shell), args.value(6));
    tree(shell)->window()->close();

    const QString id = QStringLiteral("signal.trace.load.chain");
    const QKeySequence custom(Qt::CTRL | Qt::SHIFT | Qt::Key_F9);
    auto *keyEditor = preferences.findChild<QKeySequenceEdit *>(QStringLiteral("settings-shortcut-editor"));
    auto *assign = preferences.findChild<QPushButton *>(QStringLiteral("settings-assign"));
    const int bindingRow = shortcutRow(keys, id);
    if (!check("Preferences exposes the trace rebind controls", keyEditor && assign && bindingRow >= 0)) {
        return 1;
    }
    preferences.show();
    keys->selectRow(bindingRow);
    keyEditor->setKeySequence(custom);
    assign->click();
    check("rebind immediately updates the live QAction", loads->shortcut() == custom);
    preferences.accept();
    const bool flushed = preferences.panel()->flush();
    vedit::app::JsonShortcutStore persisted(state.filePath(QStringLiteral("shortcuts.json")));
    QString loadError;
    persisted.load(&loadError);
    check("trace rebind persisted", flushed && loadError.isEmpty() && persisted.shortcut(id, {}) == custom);
    const auto traceReady = [&] {
        return !model->isPending() && loads->isEnabled() && loads->shortcut() == custom;
    };
    check("load chain is ready after Preferences refresh", until(traceReady));
    QSignalSpy traceTriggered(loads, &QAction::triggered);
    pressShortcut(shell, Qt::Key_F9, Qt::ShiftModifier);
    QTest::qWait(120);
    check("rebind removes the old key", traceTriggered.isEmpty() && tree(shell) == nullptr);
    until(traceReady);
    pressShortcut(shell, Qt::Key_F9, Qt::ControlModifier | Qt::ShiftModifier);
    if (!check("new shortcut triggers the same chain in the live editor", until([&] { return tree(shell); })
               && traceTriggered.size() == 1)) {
        std::printf("rebind triggers=%lld enabled=%d effective=%s shell-active=%d status=%s\n",
                    static_cast<long long>(traceTriggered.size()), loads->isEnabled(),
                    qPrintable(loads->shortcut().toString()), shell.isActiveWindow(),
                    qPrintable(shell.statusBar()->currentMessage()));
    }
    std::printf("failures=%d\n", failures);
    return failures != 0;
}
