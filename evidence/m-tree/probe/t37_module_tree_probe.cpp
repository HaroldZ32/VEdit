// T37 recheck probe: the module tree (T17 lane) against the CURRENT hierarchy
// service after the T46 walker fix. The contract (prep-t37) requires THREE path
// segments to be visible in the REAL product shell and a double-click on each to
// land on the exact instantiation `file:line:column`:
//
//   top.u_mid.g_leaf[0].u   generate-for (labelled loop block `g_leaf`)
//   top.u_mid.g_if.u_if     generate-if  (labelled block `g_if`)
//   top.u_arr[3]            instance array element under the top root
//
// Fixture: evidence/m-tree/fixture-t37/{top,mid,leaf}.sv (this directory).
//
// The probe is the product path, not a stand-in: `ApplicationShell` +
// `installModuleTree()` (the same call `src/app/main.cpp` makes when the panel is
// registered), a real QTreeWidget, and REAL mouse events (press + double-click +
// release on the viewport, exactly the sequence the T49 evidence established).
// The jump target is a recording IJumpTarget in the process-wide registry: the
// real editor target belongs to T20's contract, and the probe must not open a
// second file-open path.
//
// Exit 0 = every check passed. argv[1] = fixture dir, argv[2] = screenshot path.
#include "app_shell.h"
#include "editor_area.h"
#include "integration.h"
#include "module_tree/module_tree_integration.h"
#include "module_tree/module_tree_panel.h"
#include "panel_registry.h"

#include "jump/jump_registry.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTextStream>
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

// The same event sequence the T49 repair established for this Qt build: it is
// built without QTEST_QPA_MOUSE_HANDLING, so mouseDClick delivers the
// MouseButtonDblClick event alone and QAbstractItemView needs the press of the
// first click to have recorded `pressedIndex`; press + dblclick + release are
// exactly the event types a real double click sends.
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

/// The instantiation site of an instance, read from the fixture source (NOT from
/// the hierarchy answer, so the assertion cannot be circular): the line that
/// carries `needle`, and the 1-based column of `token` inside that same line.
struct ExpectedSite {
    QString file;
    int line = 0;
    int column = 0;
};

bool locateSite(const QString &path, const QString &needle, const QString &token,
                ExpectedSite *site)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    int lineNumber = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        ++lineNumber;
        const int needleAt = line.indexOf(needle);
        if (needleAt < 0) {
            continue;
        }
        const int tokenAt = line.indexOf(token, needleAt);
        if (tokenAt < 0) {
            return false;
        }
        site->file = QFileInfo(path).filePath();
        site->line = lineNumber;
        site->column = tokenAt + 1;
        return true;
    }
    return false;
}

struct PathCase {
    const char *checkName;   ///< suffix for the check names
    const char *instancePath;///< the row label the tree must carry
    const char *segment;     ///< the path segment this recheck is about
    const char *moduleFile;  ///< fixture file that instantiates it
    const char *needle;      ///< a substring unique to the instantiating line
    const char *token;       ///< the instance identifier inside that line
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-t37-module-tree-probe"));

    const QString fixture = argc > 1 && *argv[1] != '\0'
                                ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()
                                : QString();
    const QString shot = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (fixture.isEmpty() || !QFileInfo::exists(fixture)) {
        std::printf("FATAL: pass the fixture directory as argv[1]\n");
        return 2;
    }
    const QString stateDir = QDir::tempPath() + QStringLiteral("/vedit-t37-module-tree-probe");

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
    check("t37.dock-inserted", dock != nullptr);
    check("t37.dock-visible", dock != nullptr && dock->isVisible());
    if (dock == nullptr) {
        return 1;
    }
    auto *panel = qobject_cast<vedit::workspace::module_tree::ModuleTreePanel *>(dock->widget());
    check("t37.panel-content", panel != nullptr);
    if (panel == nullptr) {
        return 1;
    }

    const bool painted = spinUntil([&]() { return panel->tree()->topLevelItemCount() > 0; }, 120000);
    check("t37.hierarchy-painted", painted, QStringLiteral("status=%1").arg(panel->statusText()));
    check("t37.summary", panel->summaryText().contains(QLatin1String("instance")),
          panel->summaryText());
    std::printf("     rows=%d summary=%s\n", countItems(panel->tree()),
                panel->summaryText().toStdString().c_str());

    // The ABI answer must now carry the three shapes; print every instance row
    // whose label starts with `top`, so a missing shape is visible in the log
    // even before the per-case check names it.
    std::printf("     instance rows (top*):");
    const std::function<void(QTreeWidgetItem *)> printTop = [&](QTreeWidgetItem *item) {
        if (item->text(0).startsWith(QLatin1String("top"))) {
            std::printf(" %s", item->text(0).toStdString().c_str());
        }
        for (int index = 0; index < item->childCount(); ++index) {
            printTop(item->child(index));
        }
    };
    for (int index = 0; index < panel->tree()->topLevelItemCount(); ++index) {
        printTop(panel->tree()->topLevelItem(index));
    }
    std::printf("\n");

    // The probe stands in for T20's real editor target (declared dependency).
    vedit::workspace::jump::Registry::instance().reset();
    RecordingTarget recorder;
    vedit::workspace::jump::setTarget(&recorder);

    const PathCase cases[] = {
        {"g_leaf0", "top.u_mid.g_leaf[0].u", "g_leaf[0]", "mid.sv", "leaf u (", "u"},
        {"g_if", "top.u_mid.g_if.u_if", "g_if.u_if", "mid.sv", "leaf u_if", "u_if"},
        {"u_arr3", "top.u_arr[3]", "u_arr[3]", "top.sv", "leaf u_arr", "u_arr"},
    };
    for (const PathCase &pathCase : cases) {
        const QString label = QString::fromLatin1(pathCase.instancePath);
        const QString segment = QString::fromLatin1(pathCase.segment);
        QTreeWidgetItem *row = findItem(panel->tree(), label);
        check((QStringLiteral("t37.row.") + pathCase.checkName).toLatin1().constData(),
              row != nullptr, label);
        if (row == nullptr) {
            check((QStringLiteral("t37.jump.") + pathCase.checkName).toLatin1().constData(),
                  false, QStringLiteral("row missing: %1").arg(label));
            continue;
        }
        check((QStringLiteral("t37.segment.") + pathCase.checkName).toLatin1().constData(),
              row->text(0).contains(segment),
              QStringLiteral("row=%1 segment=%2").arg(row->text(0), segment));

        ExpectedSite site;
        const QString modulePath = fixture + QLatin1Char('/') + QString::fromLatin1(pathCase.moduleFile);
        const bool located = locateSite(modulePath, QString::fromLatin1(pathCase.needle),
                                        QString::fromLatin1(pathCase.token), &site);
        check((QStringLiteral("t37.ground-truth.") + pathCase.checkName).toLatin1().constData(),
              located,
              QStringLiteral("%1: %2/%3 line %4 col %5")
                  .arg(pathCase.checkName, pathCase.moduleFile, pathCase.token)
                  .arg(site.line)
                  .arg(site.column));

        const int before = recorder.calls;
        const bool delivered = doubleClickTreeRow(panel, row);
        const QString observed = QStringLiteral("%1:%2:%3")
                                     .arg(QFileInfo(recorder.lastFile).fileName())
                                     .arg(recorder.lastLine)
                                     .arg(recorder.lastColumn);
        const bool jumped = delivered && recorder.calls == before + 1
                            && QFileInfo(recorder.lastFile).fileName()
                                   == QString::fromLatin1(pathCase.moduleFile)
                            && recorder.lastLine == site.line && recorder.lastColumn == site.column;
        check((QStringLiteral("t37.jump.") + pathCase.checkName).toLatin1().constData(), jumped,
              jumped ? QStringLiteral("%1 -> %2").arg(label, observed)
                     : QStringLiteral("%1 -> %2 (delivered=%3 expected %4:%5:%6 calls=%7->%8)")
                           .arg(label, observed)
                           .arg(delivered ? 1 : 0)
                           .arg(QString::fromLatin1(pathCase.moduleFile))
                           .arg(site.line)
                           .arg(site.column)
                           .arg(before)
                           .arg(recorder.calls));
        std::printf("     %s -> %s (expected %s:%d:%d)\n", qPrintable(label), qPrintable(observed),
                    pathCase.moduleFile, site.line, site.column);
    }

    if (!shot.isEmpty()) {
        // The evidence screenshot must show the three path segments in one
        // frame. Every instance row is kept visible and every signal subtree is
        // collapsed: `top` is expanded (its instance children `top.u_mid` and
        // `top.u_arr[0..3]` are nested under it), while each instance row's nets
        // are folded away; the generate instances are top-level rows.
        panel->tree()->collapseAll();
        if (QTreeWidgetItem *topRow = findItem(panel->tree(), QStringLiteral("top"))) {
            topRow->setExpanded(true);
        }
        panel->tree()->scrollToTop();
        QApplication::processEvents();
        QDir().mkpath(QFileInfo(shot).absolutePath());
        const bool saved = dock->grab().save(shot);
        check("t37.screenshot", saved, shot);
    }

    vedit::workspace::jump::clearTargetIf(&recorder);
    vedit::workspace::jump::Registry::instance().reset();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
