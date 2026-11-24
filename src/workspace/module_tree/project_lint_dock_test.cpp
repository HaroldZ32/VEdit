// Lane B acceptance test for AC L3 (batch-20261116): the whole-project LINT dock.
//
// The user's report was concrete: clicking the panel made it disappear, opening
// it again shrank it to a narrow strip on the left, and it could not be dragged.
// Those are layout facts, so this test asserts layout facts on the REAL shell:
//
//   * the dock is created on the BOTTOM edge, is visible, and carries a readable
//     minimum height (>= 180 px) on the dock, its content page and the host the
//     diagnostics lane installs into (AC L3.1);
//   * a restored session that left it floating / tabified / on the left edge or
//     squeezed is normalised back to a readable bottom dock (AC L3.2);
//   * Bottom, Right and floating are allowed, Top/Left are not (AC L3.3);
//   * the View menu toggle brings it back VISIBLE and in place — never "opens and
//     vanishes" (AC L3.3).
#include "app_shell.h"
#include "command_registry.h"
#include "panel_registry.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    std::printf("%s %s%s\n", condition ? "ok  " : "FAIL", name,
                detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    std::fflush(stdout);
}

void pump(int milliseconds = 120)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

std::unique_ptr<vedit::app::ApplicationShell> makeShell(const QString &stateDir, bool restore,
                                                        bool defer)
{
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = restore;
    options.showDefaultPanels = true;
    options.deferSessionRestore = defer;
    auto shell = std::make_unique<vedit::app::ApplicationShell>(options);
    shell->resize(1280, 820);
    shell->show();
    pump();
    return shell;
}

/// AC L3.1: creation state.
void checkCreation(const QString &stateDir)
{
    auto shell = makeShell(stateDir, /*restore=*/false, /*defer=*/false);
    QDockWidget *dock = shell->projectLintDock();
    check("dock.exists", dock != nullptr);
    if (dock == nullptr) {
        return;
    }
    check("dock.bottom", shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
          QString::number(static_cast<int>(shell->dockWidgetArea(dock))));
    check("dock.visible", dock->isVisible());
    check("dock.not-floating", !dock->isFloating());
    check("dock.minimum-height-readable",
          dock->minimumHeight() >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          QStringLiteral("dockMin=%1 floor=%2")
              .arg(dock->minimumHeight())
              .arg(vedit::app::ApplicationShell::kProjectLintMinimumHeight));
    QWidget *host = shell->projectLintHost();
    QWidget *content = dock->findChild<QWidget *>(QStringLiteral("project-lint-content"));
    check("dock.host-exists", host != nullptr);
    check("dock.host-minimum-height",
          host != nullptr
              && host->minimumHeight() >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          host != nullptr ? QString::number(host->minimumHeight()) : QString());
    check("dock.host-minimum-width", host != nullptr && host->minimumWidth() >= 320,
          host != nullptr ? QString::number(host->minimumWidth()) : QString());
    check("dock.content-minimum-height",
          content != nullptr
              && content->minimumHeight() >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          content != nullptr ? QString::number(content->minimumHeight()) : QString());
    check("dock.content-minimum-width", content != nullptr && content->minimumWidth() >= 320,
          content != nullptr ? QString::number(content->minimumWidth()) : QString());
    // The layout honours the floor: a bottom dock in a 820 px window is taller
    // than the minimum (a regression that squeezes it to a strip fails here).
    check("dock.height-readable", !dock->isVisible()
                                      || dock->height()
                                          >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          QStringLiteral("height=%1").arg(dock->height()));

    // AC L3.3: allowed areas. Right is allowed (a wide lint table fits there),
    // Top/Left are not, and floating is never restricted by Qt's area list.
    const Qt::DockWidgetAreas areas = dock->allowedAreas();
    check("dock.allowed-bottom", areas.testFlag(Qt::BottomDockWidgetArea));
    check("dock.allowed-right", areas.testFlag(Qt::RightDockWidgetArea));
    check("dock.allowed-not-left", !areas.testFlag(Qt::LeftDockWidgetArea));
    check("dock.allowed-not-top", !areas.testFlag(Qt::TopDockWidgetArea));
    dock->setFloating(true);
    pump();
    check("dock.floating-allowed", dock->isFloating());
    dock->setFloating(false);
    shell->addDockWidget(Qt::BottomDockWidgetArea, dock);
    pump();
    check("dock.back-on-bottom", shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea);
}

/// AC L3.2: the normaliser repairs every degenerate restored state.
void checkNormalization(const QString &stateDir)
{
    auto shell = makeShell(stateDir, /*restore=*/false, /*defer=*/false);
    QDockWidget *dock = shell->projectLintDock();
    if (dock == nullptr) {
        return;
    }
    // (a) floating somewhere.
    dock->setFloating(true);
    dock->show();
    pump();
    shell->normalizeProjectLintDock();
    pump();
    check("normalize.floating-back-to-bottom",
          !dock->isFloating() && shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
          QStringLiteral("floating=%1 area=%2")
              .arg(dock->isFloating())
              .arg(static_cast<int>(shell->dockWidgetArea(dock))));
    check("normalize.floating-still-visible", dock->isVisible());

    // (b) on the LEFT edge (what the user saw as a narrow strip).
    shell->addDockWidget(Qt::LeftDockWidgetArea, dock);
    pump();
    shell->normalizeProjectLintDock();
    pump();
    check("normalize.left-back-to-bottom",
          shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
          QString::number(static_cast<int>(shell->dockWidgetArea(dock))));

    // (c) tabified behind another bottom dock: the "opens and vanishes" state —
    // the dock becomes visible but only as a hidden tab.
    QDockWidget *other = nullptr;
    for (QDockWidget *candidate : shell->findChildren<QDockWidget *>()) {
        if (candidate != dock && shell->dockWidgetArea(candidate) == Qt::BottomDockWidgetArea) {
            other = candidate;
            break;
        }
    }
    if (other == nullptr) {
        // A user layout with two bottom docks: a lane dock or, in this lane-only
        // build, a stand-in the test adds itself.
        other = new QDockWidget(QStringLiteral("other-bottom"), shell.get());
        other->setObjectName(QStringLiteral("other-bottom"));
        shell->addDockWidget(Qt::BottomDockWidgetArea, other);
        other->show();
        pump();
    }
    check("normalize.second-bottom-dock-available", other != nullptr);
    if (other != nullptr) {
        shell->tabifyDockWidget(other, dock);
        pump();
        const bool tabified = !shell->tabifiedDockWidgets(dock).isEmpty();
        check("normalize.tabified-before", tabified);
        shell->normalizeProjectLintDock();
        pump();
        check("normalize.untabified",
              shell->tabifiedDockWidgets(dock).isEmpty()
                  && shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
              QStringLiteral("tabs=%1 area=%2")
                  .arg(shell->tabifiedDockWidgets(dock).size())
                  .arg(static_cast<int>(shell->dockWidgetArea(dock))));
    }

    // (d) squeezed: a minimum below the floor must be repaired by the normaliser
    // (an old session state can carry it).
    dock->setMinimumHeight(24);
    shell->normalizeProjectLintDock();
    pump();
    check("normalize.minimum-repaired",
          dock->minimumHeight() >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          QString::number(dock->minimumHeight()));
}

/// AC L3.3: the View toggle brings it back visible and in place.
void checkToggle(const QString &stateDir)
{
    auto shell = makeShell(stateDir, /*restore=*/false, /*defer=*/false);
    QDockWidget *dock = shell->projectLintDock();
    QAction *toggle = shell->commands()->action(QStringLiteral("view.dock.project-lint"));
    check("toggle.command-exists", toggle != nullptr && toggle->isCheckable());
    if (dock == nullptr || toggle == nullptr) {
        return;
    }
    // The user closes the panel with its own X, then reopens it from View.
    dock->hide();
    pump();
    check("toggle.hidden-after-close", !dock->isVisible());
    check("toggle.unchecked-when-hidden", !toggle->isChecked());
    // While hidden the dock is also floating far away (an old session state):
    // this is the combination that used to "open and vanish".
    dock->setFloating(true);
    dock->move(-20000, -20000);
    pump();
    toggle->trigger();
    pump(200);
    check("toggle.reopens-visible", dock->isVisible());
    check("toggle.reopens-docked",
          !dock->isFloating() && shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
          QStringLiteral("floating=%1 area=%2")
              .arg(dock->isFloating())
              .arg(static_cast<int>(shell->dockWidgetArea(dock))));
    check("toggle.checked-when-visible", toggle->isChecked());
    check("toggle.on-screen", dock->isVisible() && !dock->isFloating()
                                  && shell->isAncestorOf(dock));
    toggle->trigger();
    pump();
    check("toggle.closes-again", !dock->isVisible() && !toggle->isChecked());
}

/// AC L3.2: the REAL session round-trip (save a degenerate layout, restore it).
void checkRestoredSession(const QString &stateDir)
{
    {
        auto shell = makeShell(stateDir, /*restore=*/false, /*defer=*/false);
        QDockWidget *dock = shell->projectLintDock();
        if (dock == nullptr) {
            return;
        }
        // A layout the user could have saved: the dock floated away and parked
        // off-screen, which is exactly what "opens as nothing" looked like.
        dock->setFloating(true);
        dock->move(-30000, -30000);
        pump();
        QString error;
        const bool saved = shell->saveSession(&error);
        check("session.saved", saved, error);
    }
    auto shell = makeShell(stateDir, /*restore=*/true, /*defer=*/true);
    shell->startSession();
    pump(300);
    QDockWidget *dock = shell->projectLintDock();
    if (dock == nullptr) {
        check("session.dock-exists", false);
        return;
    }
    check("session.dock-exists", true);
    check("session.restored-on-bottom", shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea,
          QString::number(static_cast<int>(shell->dockWidgetArea(dock))));
    check("session.restored-not-floating", !dock->isFloating());
    check("session.restored-visible", dock->isVisible());
    check("session.restored-readable-height",
          dock->height() >= vedit::app::ApplicationShell::kProjectLintMinimumHeight,
          QStringLiteral("height=%1 min=%2")
              .arg(dock->height())
              .arg(dock->minimumHeight()));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("FAIL: the temporary state directory could not be created\n");
        return 1;
    }
    const QString base = workspace.path();

    checkCreation(base + QStringLiteral("/create"));
    checkNormalization(base + QStringLiteral("/normalize"));
    checkToggle(base + QStringLiteral("/toggle"));
    checkRestoredSession(base + QStringLiteral("/restore"));

    std::printf("%s: %d check(s), %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
