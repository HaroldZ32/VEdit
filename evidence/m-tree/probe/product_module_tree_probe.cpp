// T17 product-path probe: the module tree in a REAL ApplicationShell.
//
// The lane's self-test checks the panel against a synthetic and a real service
// answer; this probe checks the wiring the product actually runs:
//
//   ApplicationShell + installModuleTree(shell, options)
//     -> the "module-tree" dock exists and is visible,
//     -> the panel paints the real service answer (instance paths `top.u0`),
//     -> a double-click arrives in the process-wide jump registry with the exact
//        1-based file:line (the probe installs a recording target: the REAL
//        editor target belongs to T20's contract — "把真实 IJumpTarget 注册进
//        jump 注册表" — so this probe stands in for it and nothing here is a
//        second jump path),
//     -> "jump to source/target" reads the word under the ACTIVE editor's cursor
//        and lands on the driver/load.
//
// Runs offscreen. Exit 0 = every check passed.
#include "app_shell.h"
#include "editor_area.h"
#include "module_tree/module_tree_integration.h"
#include "module_tree/module_tree_panel.h"
#include "integration.h"
#include "panel_registry.h"

#include "jump/jump_registry.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QTest>
#include <QTextCursor>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>
#include <functional>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

class RecordingTarget final : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return true;
    }
    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
};

// --- real input events (T49) ------------------------------------------------
// The product probe sends the same events a user does: a double-click on the
// tree viewport at the row rectangle, and a mouse click on each tool button.
// QTestWidgets here is built WITHOUT QTEST_QPA_MOUSE_HANDLING, so mouseDClick
// delivers the MouseButtonDblClick event alone; QAbstractItemView needs the
// press of the first click to have recorded pressedIndex, hence press + dblclick
// + release (the event types of a real double click).
bool doubleClickTreeRow(vedit::workspace::module_tree::ModuleTreePanel *panel,
                        QTreeWidgetItem *item)
{
    if (panel == nullptr || item == nullptr || !panel->isVisible() || panel->tree() == nullptr
        || !panel->tree()->viewport()->isVisible()) {
        return false;
    }
    panel->tree()->scrollToItem(item);
    QApplication::processEvents();
    const QRect rect = panel->tree()->visualItemRect(item);
    if (rect.isEmpty()) {
        return false;
    }
    QTest::mousePress(panel->tree()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QTest::mouseDClick(panel->tree()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QTest::mouseRelease(panel->tree()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    QApplication::processEvents();
    return true;
}

bool clickPanelButton(vedit::workspace::module_tree::ModuleTreePanel *panel, const char *name)
{
    if (panel == nullptr) {
        return false;
    }
    auto *button = panel->findChild<QToolButton *>(QString::fromLatin1(name));
    if (button == nullptr || !button->isVisible()) {
        return false;
    }
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();
    return true;
}

QTreeWidgetItem *findItem(QTreeWidget *tree, const QString &label)
{
    const auto walk = [&label](QTreeWidgetItem *item, const auto &self) -> QTreeWidgetItem * {
        if (item->text(0) == label) {
            return item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            if (QTreeWidgetItem *found = self(item->child(index), self)) {
                return found;
            }
        }
        return nullptr;
    };
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (QTreeWidgetItem *found = walk(tree->topLevelItem(index), walk)) {
            return found;
        }
    }
    return nullptr;
}

int countItems(QTreeWidget *tree)
{
    const auto walk = [](QTreeWidgetItem *item, const auto &self) -> int {
        int total = 1;
        for (int index = 0; index < item->childCount(); ++index) {
            total += self(item->child(index), self);
        }
        return total;
    };
    int total = 0;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        total += walk(tree->topLevelItem(index), walk);
    }
    return total;
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(20);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return ok && done();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-module-tree-product-probe"));

    const QString fixture =
        argc > 1 && *argv[1] != '\0'
            ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
            : QString();
    const QString shot = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (fixture.isEmpty() || !QFileInfo::exists(fixture)) {
        std::printf("FATAL: pass the fixture directory as argv[1]\n");
        return 2;
    }
    const QString stateDir = QDir::tempPath() + QStringLiteral("/vedit-module-tree-product-probe");

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = fixture;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    vedit::app::ApplicationShell shell(options);
    shell.startSession();
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = fixture;
    integrationOptions.stateDir = stateDir;
    vedit::workspace::installModuleTree(shell, integrationOptions);
    shell.resize(1280, 820);
    shell.show();
    QApplication::processEvents();

    QDockWidget *dock = shell.panels()->dock(QStringLiteral("module-tree"));
    check("product.dock-inserted", dock != nullptr);
    check("product.dock-visible", dock != nullptr && dock->isVisible());
    if (dock == nullptr) {
        return 1;
    }
    auto *panel = qobject_cast<vedit::workspace::module_tree::ModuleTreePanel *>(dock->widget());
    check("product.panel-content", panel != nullptr);
    if (panel == nullptr) {
        return 1;
    }

    const bool painted = spinUntil([&]() { return panel->tree()->topLevelItemCount() > 0; }, 60000);
    check("product.hierarchy-painted", painted,
          QStringLiteral("status=%1").arg(panel->statusText()));
    check("product.summary-mentions-project", panel->summaryText().contains(fixture),
          panel->summaryText());
    check("product.rows", countItems(panel->tree()) > 6,
          QStringLiteral("rows=%1").arg(countItems(panel->tree())));
    check("product.instance-paths", findItem(panel->tree(), QStringLiteral("top")) != nullptr
                                        && findItem(panel->tree(), QStringLiteral("top.u0")) != nullptr
                                        && findItem(panel->tree(), QStringLiteral("top.u1")) != nullptr,
          panel->summaryText());

    // The probe stands in for T20's real editor target (declared dependency).
    vedit::workspace::jump::Registry::instance().reset();
    RecordingTarget recorder;
    vedit::workspace::jump::setTarget(&recorder);

    QTreeWidgetItem *u0 = findItem(panel->tree(), QStringLiteral("top.u0"));
    check("product.event.row-clickable",
          u0 != nullptr && !panel->tree()->visualItemRect(u0).isEmpty(),
          u0 != nullptr ? QStringLiteral("rect=%1x%2")
                              .arg(panel->tree()->visualItemRect(u0).width())
                              .arg(panel->tree()->visualItemRect(u0).height())
                        : QString());
    const int beforeDoubleClick = recorder.calls;
    const bool instanceJumped = doubleClickTreeRow(panel, u0);
    check("product.event.double-click-instance",
          instanceJumped && recorder.calls == beforeDoubleClick + 1
              && recorder.lastFile.endsWith(QLatin1String("top.sv"))
              && recorder.lastLine == 11 && recorder.lastColumn == 7,
          QStringLiteral("%1:%2:%3").arg(recorder.lastFile).arg(recorder.lastLine)
              .arg(recorder.lastColumn));

    // The cursor actions read the ACTIVE editor: open a project file through the
    // shell's own open path (demo provider here), put the cursor on `link`.
    QString openError;
    const QString topFile = fixture + QStringLiteral("/top.sv");
    check("product.open-file", shell.openPaths({topFile}, &openError), openError);
    QApplication::processEvents();
    auto *editor = qobject_cast<QPlainTextEdit *>(
        shell.editorArea()->activePane() != nullptr
            ? shell.editorArea()->activePane()->editorWidget()
            : nullptr);
    check("product.active-editor", editor != nullptr);
    if (editor != nullptr) {
        const QString text = editor->toPlainText();
        const int lastLink = text.lastIndexOf(QStringLiteral("link"));
        if (lastLink >= 0) {
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(lastLink + 1);
            editor->setTextCursor(cursor);
        }
        QApplication::processEvents();
        const int beforeSource = recorder.calls;
        const bool source = clickPanelButton(panel, "module-tree-jump-source");
        check("product.event.button-source",
              source && recorder.calls == beforeSource + 1
                  && recorder.lastFile.endsWith(QLatin1String("top.sv"))
                  && recorder.lastLine == 11 && recorder.lastColumn == 38,
              panel->lastActionStatus());
        const int beforeTarget = recorder.calls;
        const bool target = clickPanelButton(panel, "module-tree-jump-target");
        check("product.event.button-target",
              target && recorder.calls == beforeTarget + 1
                  && recorder.lastFile.endsWith(QLatin1String("top.sv"))
                  && recorder.lastLine == 12,
              panel->lastActionStatus());
    }

    if (!shot.isEmpty()) {
        QDir().mkpath(QFileInfo(shot).absolutePath());
        const bool saved = dock->grab().save(shot);
        check("product.screenshot", saved, shot);
    }

    // Negative control on the REAL tree. Every node the service reports for
    // this fixture does carry a position (even `top.rst` is the module port), so
    // the row without a location is added here: activating it must not touch the
    // registry and must not crash.
    auto *synthetic = new QTreeWidgetItem();
    synthetic->setText(0, QStringLiteral("node without a location"));
    panel->tree()->addTopLevelItem(synthetic);
    const int beforeNoop = recorder.calls;
    const bool deliveredNoop = doubleClickTreeRow(panel, synthetic);
    check("product.event.no-location-double-click",
          deliveredNoop && recorder.calls == beforeNoop,
          QStringLiteral("delivered=%1 calls=%2->%3 status=%4")
              .arg(deliveredNoop ? 1 : 0)
              .arg(beforeNoop)
              .arg(recorder.calls)
              .arg(panel->lastActionStatus()));
    const int beforeNull = recorder.calls;
    check("product.null-row-safe", !panel->activateItem(nullptr) && recorder.calls == beforeNull);
    panel->tree()->takeTopLevelItem(panel->tree()->topLevelItemCount() - 1);

    vedit::workspace::jump::clearTargetIf(&recorder);
    vedit::workspace::jump::Registry::instance().reset();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
