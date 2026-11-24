// batch-20261115 (lane C): the shell-side regression tests for the frozen
// contract .mpd/plans/batch-20261115-ui-env.md.
//
//   * AC-4.1: the bottom `project-lint` dock, its `project-lint-host` content
//     seam, the empty state (AC-4.5) and the dock's persistence in the session
//     archive;
//   * AC-7.4: the rendered stylesheet's QMainWindow::separator is NOT the canvas
//     colour, is >= 6 px and has hover feedback, on EVERY product theme;
//   * AC-7.5: QSplitter::handle uses a visible style too, and a restored layout
//     whose right dock column has no visible, non-floating dock is normalised
//     back to a right-docked, visible dock (the precondition is asserted first,
//     so the normalising restore is proven to matter);
//   * AC-5.3: closing 10 tabs is ONE state notification and < 200 ms on the UI
//     thread (the demo provider always, the real KTextEditor provider when the
//     editor lane is present).
//
//   vedit-app-batch-layout-selftest
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "panel_registry.h"
#include "product_theme.h"
#include "session_store.h"

#if defined(VEDIT_BATCH_LAYOUT_HAVE_EDIT_CORE)
#include "ktexteditor_provider.h"
#endif

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", qPrintable(detail));
    std::fflush(stdout);
}

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

QString writeFile(const QString &directory, const QString &name, const QString &text)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
    return path;
}

// 40 lines per module; 10 modules is enough for the close benchmark (the open
// benchmark in editor_open_bench_test.cpp owns the 10k sample).
QString smallModule(int index)
{
    const QString n = QString::number(index);
    return QStringLiteral("module m%1 (input logic clk);\n"
                          "  // filler %1\n"
                          "endmodule\n")
        .arg(n);
}

QAction *menuAction(QMenu *menu, const QString &objectName)
{
    if (menu == nullptr) {
        return nullptr;
    }
    for (QAction *action : menu->actions()) {
        if (action->objectName() == objectName) {
            return action;
        }
    }
    return nullptr;
}

// --- QSS readers -----------------------------------------------------------
// The acceptance is a TOKEN assertion (AC-7.6): read the stylesheet the product
// really renders for a theme, then check the separator rule's own values.
QString ruleBody(const QString &sheet, const QString &selector)
{
    const QRegularExpression expression(QStringLiteral("(?:^|\\n)\\s*") +
                                        QRegularExpression::escape(selector) +
                                        QStringLiteral("\\s*\\{([^}]*)\\}"));
    const QRegularExpressionMatch match = expression.match(sheet);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString backgroundColour(const QString &body)
{
    const QRegularExpression expression(
        QStringLiteral("background(?:-color)?\\s*:\\s*(#[0-9a-fA-F]{6})"));
    const QRegularExpressionMatch match = expression.match(body);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

int pixelSize(const QString &body, const QString &property)
{
    const QRegularExpression expression(QStringLiteral("\\b") + property +
                                        QStringLiteral("\\s*:\\s*(\\d+)\\s*px"));
    const QRegularExpressionMatch match = expression.match(body);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

bool hasVisibleRightDock(const QMainWindow &window)
{
    for (QDockWidget *dock : window.findChildren<QDockWidget *>()) {
        if (dock == nullptr || !dock->isVisible() || dock->isFloating()) {
            continue;
        }
        if (window.dockWidgetArea(dock) == Qt::RightDockWidgetArea) {
            return true;
        }
    }
    return false;
}

// --- AC-7.4 / AC-7.5: stylesheet tokens ------------------------------------
void checkThemeStylesheet()
{
    const QVector<vedit::app::ProductTheme> &themes = vedit::app::productThemes();
    check(QStringLiteral("qss.themes"), themes.size() >= 6, QString::number(themes.size()));
    bool hasLight = false;
    bool hasDark = false;
    for (const vedit::app::ProductTheme &theme : themes) {
        (theme.dark ? hasDark : hasLight) = true;
        const QString sheet = vedit::app::renderStyleSheet(theme);
        check(QStringLiteral("qss.sheet.%1").arg(theme.id), !sheet.isEmpty());
        if (sheet.isEmpty()) {
            continue;
        }

        const QString separator = ruleBody(sheet, QStringLiteral("QMainWindow::separator"));
        const QString separatorHover =
            ruleBody(sheet, QStringLiteral("QMainWindow::separator:hover"));
        const QString restColour = backgroundColour(separator);
        const QString hoverColour = backgroundColour(separatorHover);
        check(QStringLiteral("qss.separator.rest-visible.%1").arg(theme.id),
              restColour == theme.border.name(QColor::HexRgb).toLower(),
              QStringLiteral("rest=%1 border=%2")
                  .arg(restColour, theme.border.name(QColor::HexRgb)));
        check(QStringLiteral("qss.separator.not-canvas.%1").arg(theme.id),
              !restColour.isEmpty() && restColour != theme.canvas.name(QColor::HexRgb).toLower(),
              QStringLiteral("rest=%1 canvas=%2")
                  .arg(restColour, theme.canvas.name(QColor::HexRgb)));
        check(QStringLiteral("qss.separator.size.%1").arg(theme.id),
              pixelSize(separator, QStringLiteral("width")) >= 6 &&
                  pixelSize(separator, QStringLiteral("height")) >= 6,
              QStringLiteral("width=%1 height=%2")
                  .arg(pixelSize(separator, QStringLiteral("width")))
                  .arg(pixelSize(separator, QStringLiteral("height"))));
        check(QStringLiteral("qss.separator.hover.%1").arg(theme.id),
              !hoverColour.isEmpty() && hoverColour != restColour, hoverColour);

        const QString handle = ruleBody(sheet, QStringLiteral("QSplitter::handle"));
        const QString handleHover =
            ruleBody(sheet, QStringLiteral("QSplitter::handle:hover"));
        check(QStringLiteral("qss.splitter.visible.%1").arg(theme.id),
              !backgroundColour(handle).isEmpty() &&
                  backgroundColour(handle) != theme.canvas.name(QColor::HexRgb).toLower(),
              QStringLiteral("handle=%1 canvas=%2")
                  .arg(backgroundColour(handle), theme.canvas.name(QColor::HexRgb)));
        check(QStringLiteral("qss.splitter.hover.%1").arg(theme.id),
              !backgroundColour(handleHover).isEmpty() &&
                  backgroundColour(handleHover) != backgroundColour(handle),
              backgroundColour(handleHover));
    }
    // "both themes visible" in the acceptance means at least one light and one
    // dark product theme, not only the default.
    check(QStringLiteral("qss.light-and-dark"), hasLight && hasDark,
          QStringLiteral("light=%1 dark=%2").arg(hasLight).arg(hasDark));
}

// --- AC-4.1 / AC-4.5: the project-lint dock shell --------------------------
void checkProjectLintDock(const QString &stateDir)
{
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = true;
    auto shell = std::make_unique<vedit::app::ApplicationShell>(options);
    shell->resize(1280, 820);
    shell->show();
    pump();

    QWidget *host = shell->projectLintHost();
    check(QStringLiteral("dock.host"), host != nullptr &&
                                            host->objectName() ==
                                                QStringLiteral("project-lint-host"),
          host != nullptr ? host->objectName() : QStringLiteral("<null>"));
    QDockWidget *dock = shell->findChild<QDockWidget *>(QStringLiteral("project-lint"));
    check(QStringLiteral("dock.exists"), dock != nullptr);
    check(QStringLiteral("dock.title"),
          dock != nullptr &&
              dock->windowTitle() == QCoreApplication::translate("VEdit", "Project Lint"),
          dock != nullptr ? dock->windowTitle() : QString());
    check(QStringLiteral("dock.bottom"),
          dock != nullptr && shell->dockWidgetArea(dock) == Qt::BottomDockWidgetArea);
    check(QStringLiteral("dock.visible"), dock != nullptr && dock->isVisible());
    check(QStringLiteral("dock.host-inside"), dock != nullptr && host != nullptr &&
                                                  dock->isAncestorOf(host));
    // The dock must NOT be a PanelRegistry panel: the --selfcheck contract is
    // "five registered panels" (panels.registry.five), and the diagnostics lane
    // receives this host through the ONE frozen accessor instead.
    check(QStringLiteral("dock.not-a-registry-panel"),
          !shell->panels()->contains(QStringLiteral("project-lint")));
    // View -> Toggle "Project Lint" Panel: a dock the user closes must be
    // reopenable (the shell adopts every shell-owned dock in syncDockPanelCommands).
    check(QStringLiteral("dock.toggle-command"),
          shell->commands()->action(QStringLiteral("view.dock.project-lint")) != nullptr);

    QWidget *empty = dock != nullptr
                         ? dock->findChild<QWidget *>(QStringLiteral("project-lint-empty-state"))
                         : nullptr;
    check(QStringLiteral("dock.empty-state"), empty != nullptr && empty->isVisible());

    // The frozen install seam (contract §4.1.1): a content widget parented to
    // the host owns the dock body from then on; the empty state must yield.
    auto *installed = new QLabel(QStringLiteral("stub project lint panel"), host);
    installed->setObjectName(QStringLiteral("project-lint-stub"));
    pump();
    check(QStringLiteral("dock.install-hides-empty"), empty != nullptr && !empty->isVisible());
    check(QStringLiteral("dock.install-visible"), installed->isVisible());

    // AC-4.1 persistence: the user's hidden state must survive save/restore.
    dock->hide();
    pump();
    check(QStringLiteral("dock.hidden"), !shell->isVisible() || !dock->isVisible());
    QString error;
    check(QStringLiteral("dock.save-session"), shell->saveSession(&error), error);
}

void checkProjectLintDockRestored(const QString &stateDir)
{
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = true;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto shell = std::make_unique<vedit::app::ApplicationShell>(options);
    shell->show();
    pump();
    shell->startSession();
    pump();
    QDockWidget *dock = shell->findChild<QDockWidget *>(QStringLiteral("project-lint"));
    check(QStringLiteral("dock.restored-exists"), dock != nullptr);
    check(QStringLiteral("dock.restored-window-title"),
          dock != nullptr &&
              dock->windowTitle() == QCoreApplication::translate("VEdit", "Project Lint"));
    check(QStringLiteral("dock.restored-hidden"), dock != nullptr && !dock->isVisible());
    check(QStringLiteral("dock.restored-host"), shell->projectLintHost() != nullptr);
}

// --- AC-7.5 / AC-7.6: restoreState normalisation ---------------------------
// `floating` selects the two ways the AC names: a floating dock (an off-screen
// window after a monitor change) and a hidden one (the user closed it). Both
// must be normalised back to a visible, right-docked dock.
void checkDockNormalisation(const QString &stateDir, bool floating)
{
    const QString label = floating ? QStringLiteral("floating") : QStringLiteral("hidden");
    const auto outlineFactory = []() -> QWidget * {
        auto *label = new QLabel(QStringLiteral("outline stub"));
        label->setObjectName(QStringLiteral("outline-stub"));
        return label;
    };

    // Seed: a saved layout whose ONLY right-area dock is floating or hidden.
    {
        vedit::app::ShellOptions options;
        options.stateDir = stateDir;
        options.restoreSession = false;
        options.showDefaultPanels = false;
        auto seed = std::make_unique<vedit::app::ApplicationShell>(options);
        seed->panels()->setFactory(QStringLiteral("outline"), outlineFactory);
        seed->panels()->showPanel(QStringLiteral("outline"), seed.get());
        seed->resize(1280, 820);
        seed->show();
        pump();
        QDockWidget *outline = seed->panels()->dock(QStringLiteral("outline"));
        check(QStringLiteral("normalize.seed-outline.") + label, outline != nullptr);
        if (outline == nullptr) {
            return;
        }
        if (floating) {
            outline->setFloating(true);
            outline->move(seed->x() + seed->width() + 200, seed->y() + 40);
        } else {
            outline->hide();
        }
        pump();
        check(QStringLiteral("normalize.seed-") + label,
              floating ? outline->isFloating() : !outline->isVisible());
        QString error;
        check(QStringLiteral("normalize.seed-save.") + label, seed->saveSession(&error), error);
    }
    pump();

    // Restore in the PRODUCT order (defer -> register factories -> start). The
    // raw restoreState is applied first so the test PROVES the precondition the
    // normalisation exists for: the right column has no visible non-floating
    // dock.
    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    auto shell = std::make_unique<vedit::app::ApplicationShell>(options);
    shell->panels()->setFactory(QStringLiteral("outline"), outlineFactory);
    shell->panels()->insert(QStringLiteral("outline"), shell.get());
    shell->resize(1280, 820);
    shell->show();
    pump();

    QString error;
    vedit::app::SessionStore store(stateDir + QStringLiteral("/session.json"));
    const QJsonObject archive = store.load(&error);
    const vedit::app::SessionState state = vedit::app::SessionState::fromJson(archive);
    check(QStringLiteral("normalize.archive.") + label,
          state.isValid() && !state.dockState.isEmpty(), error);
    check(QStringLiteral("normalize.raw-restore.") + label,
          shell->restoreState(state.dockState));
    pump();
    check(QStringLiteral("normalize.precondition-right-empty.") + label,
          !hasVisibleRightDock(*shell));
    QDockWidget *outline = shell->panels()->dock(QStringLiteral("outline"));
    check(QStringLiteral("normalize.precondition-out-of-place.") + label,
          outline != nullptr && (outline->isFloating() || !outline->isVisible()));

    // The product path: the same archive, now through restoreSession(), which
    // applies restoreState and then normalises the right column.
    check(QStringLiteral("normalize.restore-session.") + label,
          shell->restoreSession(&error), error);
    pump();
    outline = shell->panels()->dock(QStringLiteral("outline"));
    check(QStringLiteral("normalize.docked-right.") + label,
          outline != nullptr &&
              shell->dockWidgetArea(outline) == Qt::RightDockWidgetArea);
    check(QStringLiteral("normalize.not-floating.") + label,
          outline != nullptr && !outline->isFloating());
    check(QStringLiteral("normalize.visible.") + label,
          outline != nullptr && outline->isVisible());
    check(QStringLiteral("normalize.right-column-present.") + label,
          hasVisibleRightDock(*shell));
}

// --- AC-5.3: closing 10 tabs is one refresh, well under 200 ms -------------
void openTen(vedit::app::EditorPane *pane, const QString &directory, const QString &prefix,
             const QString &body, int count, bool *allOpened)
{
    for (int index = 0; index < count; ++index) {
        const QString path = writeFile(directory, QStringLiteral("%1-%2.sv").arg(prefix).arg(index),
                                      body.arg(index));
        QString error;
        if (!pane->openFile(path, &error)) {
            *allOpened = false;
            std::printf("CLOSE-BENCH open-failed path=%s error=%s\n", qPrintable(path),
                        qPrintable(error));
        }
    }
    pump();
}

void measureCloseAll(vedit::app::EditorPane *pane, const QString &label, int *stateChanges,
                     bool *closeCountOk, qint64 *elapsedMs, bool *stateMerged)
{
    QMenu *menu = pane->createTabContextMenu(0);
    QAction *closeAll = menuAction(menu, QStringLiteral("tab-menu.close-all"));
    check(QStringLiteral("close.%1.menu").arg(label), closeAll != nullptr);
    if (closeAll == nullptr) {
        if (menu != nullptr) {
            menu->deleteLater();
        }
        return;
    }
    *stateChanges = 0;
    QElapsedTimer timer;
    timer.start();
    closeAll->trigger();
    *elapsedMs = timer.elapsed();
    menu->deleteLater();
    pump();
    *closeCountOk = pane->tabCount() == 1 && pane->currentFilePath().isEmpty();
    // AC-5.3 wants "<= 1 batch refresh"; exactly ONE is what makes it a refresh
    // at all, so zero (no status-bar update) is a failure too. Anything above
    // one is the per-tab storm the batch exists to remove.
    *stateMerged = *stateChanges == 1;
}

void checkCloseBatch(const QString &directory)
{
    {
        vedit::app::DemoEditorProvider provider;
        vedit::app::EditorArea area;
        area.setProvider(&provider);
        int stateChanges = 0;
        // Installed BEFORE the pane exists: the area forwards every pane's state
        // notification, exactly like ApplicationShell::editorArea() does.
        area.setOnStateChanged([&stateChanges]() { ++stateChanges; });
        area.resize(1000, 700);
        area.show();
        pump();
        vedit::app::EditorPane *pane = area.ensurePane();
        check(QStringLiteral("close.demo-pane"), pane != nullptr);
        if (pane == nullptr) {
            return;
        }
        bool allOpened = true;
        openTen(pane, directory, QStringLiteral("demo"), smallModule(1), 10, &allOpened);
        check(QStringLiteral("close.demo-opened"), allOpened && pane->tabCount() == 10,
              QString::number(pane->tabCount()));
        check(QStringLiteral("close.demo.open-notified"), stateChanges > 0,
              QString::number(stateChanges));
        bool closeCountOk = false;
        bool stateMerged = false;
        qint64 elapsedMs = -1;
        // The SAME counter the area callback increments: measureCloseAll resets
        // it before the trigger and the batch end must bump it exactly once.
        measureCloseAll(pane, QStringLiteral("demo"), &stateChanges, &closeCountOk,
                        &elapsedMs, &stateMerged);
        check(QStringLiteral("close.demo.count"), closeCountOk,
              QString::number(pane->tabCount()));
        check(QStringLiteral("close.demo.state-merged"), stateMerged,
              QString::number(stateChanges));
        check(QStringLiteral("close.demo.under-200ms"), elapsedMs >= 0 && elapsedMs < 200,
              QString::number(elapsedMs));
        std::printf("CLOSE-BENCH provider=demo tabs=10 ms=%lld state-callbacks=%d\n",
                    static_cast<long long>(elapsedMs), stateChanges);
    }

#if defined(VEDIT_BATCH_LAYOUT_HAVE_EDIT_CORE)
    {
        auto provider = vedit::editor::makeEditCoreProvider();
        check(QStringLiteral("close.real-provider"), provider != nullptr);
        if (provider == nullptr) {
            return;
        }
        vedit::app::EditorArea area;
        area.setProvider(provider.get());
        int stateChanges = 0;
        area.setOnStateChanged([&stateChanges]() { ++stateChanges; });
        area.resize(1000, 700);
        area.show();
        pump();
        vedit::app::EditorPane *pane = area.ensurePane();
        check(QStringLiteral("close.real-pane"), pane != nullptr);
        if (pane == nullptr) {
            return;
        }
        bool allOpened = true;
        openTen(pane, directory, QStringLiteral("real"),
                QStringLiteral("module r%1 (input logic clk);\n"
                               "  // filler %1\n"
                               "endmodule\n"),
                10, &allOpened);
        check(QStringLiteral("close.real-opened"), allOpened && pane->tabCount() == 10,
              QString::number(pane->tabCount()));
        bool closeCountOk = false;
        bool stateMerged = false;
        qint64 elapsedMs = -1;
        measureCloseAll(pane, QStringLiteral("real"), &stateChanges, &closeCountOk,
                        &elapsedMs, &stateMerged);
        check(QStringLiteral("close.real.count"), closeCountOk,
              QString::number(pane->tabCount()));
        check(QStringLiteral("close.real.state-merged"), stateMerged,
              QString::number(stateChanges));
        check(QStringLiteral("close.real.under-200ms"), elapsedMs >= 0 && elapsedMs < 200,
              QString::number(elapsedMs));
        std::printf("CLOSE-BENCH provider=ktexteditor tabs=10 ms=%lld state-callbacks=%d\n",
                    static_cast<long long>(elapsedMs), stateChanges);
    }
#else
    std::printf("CLOSE-BENCH SKIP real provider (edit-core lane not linked)\n");
#endif
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-app-batch-layout-selftest"));

    checkThemeStylesheet();

    QTemporaryDir dockState;
    check(QStringLiteral("dock.temp-dir"), dockState.isValid());
    if (dockState.isValid()) {
        checkProjectLintDock(dockState.path());
        checkProjectLintDockRestored(dockState.path());
    }

    QTemporaryDir normalizeState;
    check(QStringLiteral("normalize.temp-dir"), normalizeState.isValid());
    if (normalizeState.isValid()) {
        checkDockNormalisation(normalizeState.path() + QStringLiteral("/floating"), true);
        checkDockNormalisation(normalizeState.path() + QStringLiteral("/hidden"), false);
    }

    QTemporaryDir closeFiles;
    check(QStringLiteral("close.temp-dir"), closeFiles.isValid());
    if (closeFiles.isValid()) {
        checkCloseBatch(closeFiles.path());
    }

    std::printf("BATCH-LAYOUT-SELFTEST %s checks=%d failures=%d\n",
                g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
