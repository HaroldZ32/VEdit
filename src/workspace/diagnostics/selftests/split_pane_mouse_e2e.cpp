// RM-10 follow-up (T46) — SPLIT PANES, REAL MOUSE, no shortcuts.
//
// Two independent reviewers named the same gap: the hover-follows-the-pane and
// the multi-cursor / Ctrl+click behaviour were code-verified only; no test ever
// drove them with a real mouse on a TWO-PANE layout. This binary builds the
// split through the REAL product command (`view.split.horizontal`), opens two
// documents, and then:
//
//   1. moves the REAL mouse (into KatePart's inner widget) onto a squiggle in
//      the INACTIVE pane B: the popup must appear, parented to B's view, inside
//      B's geometry, with B's diagnostic (never A's);
//   2. waits for the 400 ms live poll with the mouse stationary: the poll must
//      NOT steal the cross-pane bubble;
//   3. switches the ACTIVE pane with the product's keyboard action
//      (`view.focus.next`, no mouse): the bubble must not jump panes;
//   4. drives Ctrl+Alt+click inside B: a cursor is added in B's view only;
//   5. drives Ctrl+click inside B: the definition action dispatches from B's
//      document and B's caret moves.
//
// A deterministic fake project-lint backend supplies the squiggles (installed
// BEFORE the first event-loop turn, exactly like the other e2e harnesses).
// Run offscreen.

#include "diagnostics_controller.h"
#include "diagnostics_integration.h"

#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "ktexteditor_provider.h"
#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QWidget>

#include <csignal>
#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

void spin(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(text.toUtf8());
    file.close();
    return true;
}

using vedit::workspace::diagnostics::DiagnosticEntry;

DiagnosticEntry row(const QString &file, int severity, const QString &code)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = 1;
    entry.column = 2;
    entry.endLine = 1;
    entry.endColumn = 10;
    entry.severity = severity; // CLI scale: 0 error, 1 warning
    entry.code = code;
    entry.message = code;
    entry.source = QStringLiteral("fake-lint");
    return entry;
}

class FakeProjectLint final : public vedit::workspace::diagnostics::ProjectLintBackend {
public:
    QVector<DiagnosticEntry> rows;
    QStringList filesSeen;
    int startCount = 0;
    quint64 job_ = 0;
    vedit::workspace::diagnostics::LintPassSnapshot snapshot;

    bool available() const override { return true; }
    QString toolName() const override { return QStringLiteral("fake-lint"); }
    quint64 start(const vedit::workspace::diagnostics::LintPassRequest &request,
                  QString *) override
    {
        ++startCount;
        filesSeen = request.files;
        ++job_;
        snapshot = vedit::workspace::diagnostics::LintPassSnapshot{};
        snapshot.phase = vedit::workspace::diagnostics::LintPhase::Finished;
        snapshot.total = request.files.size();
        snapshot.done = request.files.size();
        snapshot.tool = toolName();
        return job_;
    }
    vedit::workspace::diagnostics::LintPassSnapshot progress(quint64) const override
    {
        return snapshot;
    }
    vedit::workspace::diagnostics::LspPolled poll(quint64) override
    {
        vedit::workspace::diagnostics::LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = vedit::workspace::diagnostics::ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        return polled;
    }
    void cancel(quint64) override {}
    void release(quint64) override {}
};

/// Send a real mouse move/click to whatever widget KatePart puts under the
/// VIEW-space point (the inner KateViewInternal, never the public View).
QWidget *targetAt(KTextEditor::View *view, const QPoint &viewPoint)
{
    QWidget *target = view->childAt(viewPoint);
    return target != nullptr ? target : view;
}

void sendMove(KTextEditor::View *view, const QPoint &viewPoint)
{
    QWidget *target = targetAt(view, viewPoint);
    const QPoint local = target->mapFrom(view, viewPoint);
    QMouseEvent move(QEvent::MouseMove, local, target->mapToGlobal(local), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &move);
    QCoreApplication::processEvents();
}

void sendClick(KTextEditor::View *view, const QPoint &viewPoint, Qt::KeyboardModifiers modifiers)
{
    QWidget *target = targetAt(view, viewPoint);
    const QPoint local = target->mapFrom(view, viewPoint);
    QMouseEvent press(QEvent::MouseButtonPress, local, target->mapToGlobal(local), Qt::LeftButton,
                      Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, target->mapToGlobal(local),
                        Qt::LeftButton, Qt::NoButton, modifiers);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN); // AGENT.md §4: the host links Rust
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("split.tempdir", false);
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    const QString project = dir.filePath(QStringLiteral("project"));
    QDir().mkpath(project);
    const QString aPath = QDir(project).filePath(QStringLiteral("a.sv"));
    const QString bPath = QDir(project).filePath(QStringLiteral("b.sv"));
    const bool fixture =
        writeFile(aPath, QStringLiteral("module a;\n  logic x;   \nendmodule\n"))
        && writeFile(bPath, QStringLiteral("module b;\n  logic y;\nendmodule\n"));
    check("split.fixture", fixture);
    if (!fixture) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    vedit::app::ShellOptions options;
    options.stateDir = dir.filePath(QStringLiteral("state"));
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.projectRoot = project;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    std::unique_ptr<vedit::app::ApplicationShell> shell(
        new vedit::app::ApplicationShell(options));
    if (provider != nullptr) {
        shell->editorArea()->setProvider(provider.get());
    }
    shell->editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = dir.filePath(QStringLiteral("state"));
    vedit::workspace::installDiagnostics(*shell, integration);

    auto *controller = vedit::workspace::diagnosticsControllerForShell(*shell);
    check("split.controller", controller != nullptr);
    auto fakeOwned = std::make_unique<FakeProjectLint>();
    FakeProjectLint *fake = fakeOwned.get();
    // A and B carry DIFFERENT rules: the hover test can then prove whose
    // diagnostic is on screen, not merely that some squiggle exists.
    fake->rows.append(row(aPath, 0, QStringLiteral("no-trailing-spaces")));
    fake->rows.append(row(bPath, 1, QStringLiteral("line-length")));
    if (controller != nullptr) {
        controller->setProjectLintBackend(std::move(fakeOwned));
    }
    if (controller == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    shell->resize(1280, 820);
    shell->show();
    QCoreApplication::processEvents();

    // ---- the project-open pass covers both files (deterministic squiggles) --
    const bool covered = waitUntil(
        [controller, &aPath, &bPath]() {
            return controller->hasUpperLayer(aPath) && controller->hasUpperLayer(bPath)
                   && !controller->projectPassRunning();
        },
        20000);
    check("split.covered", covered,
          QStringLiteral("a=%1 b=%2 pass=%3")
              .arg(controller->hasUpperLayer(aPath) ? 1 : 0)
              .arg(controller->hasUpperLayer(bPath) ? 1 : 0)
              .arg(controller->projectPassRunning() ? 1 : 0));

    // ---- the REAL split: the user's command, two documents ------------------
    vedit::app::EditorPane *paneA = shell->editorArea()->activePane();
    check("split.pane-a", paneA != nullptr);
    if (paneA == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    QString openError;
    check("split.open-a", paneA->openFile(aPath, &openError), openError);
    paneA->setActiveTab(paneA->tabIndexForPath(aPath));
    QAction *splitAction = shell->commands() != nullptr
                               ? shell->commands()->action(QStringLiteral("view.split.horizontal"))
                               : nullptr;
    check("split.action-exists", splitAction != nullptr);
    if (splitAction != nullptr) {
        splitAction->trigger();
    }
    QCoreApplication::processEvents();
    check("split.pane-count", shell->editorArea()->paneCount() == 2,
          QStringLiteral("panes=%1").arg(shell->editorArea()->paneCount()));
    vedit::app::EditorPane *paneB = shell->editorArea()->activePane();
    check("split.new-pane-active", paneB != nullptr && paneB != paneA);
    if (paneB == nullptr || paneB == paneA) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    check("split.open-b", paneB->openFile(bPath, &openError), openError);
    paneB->setActiveTab(paneB->tabIndexForPath(bPath));
    QCoreApplication::processEvents();

    auto *viewA = qobject_cast<KTextEditor::View *>(paneA->editorWidget());
    auto *viewB = qobject_cast<KTextEditor::View *>(paneB->editorWidget());
    check("split.views", viewA != nullptr && viewB != nullptr);
    check("split.view-docs",
          viewA != nullptr && viewB != nullptr && viewA->document() != nullptr
              && viewB->document() != nullptr
              && QFileInfo(viewA->document()->url().toLocalFile()).canonicalFilePath()
                     == QFileInfo(aPath).canonicalFilePath()
              && QFileInfo(viewB->document()->url().toLocalFile()).canonicalFilePath()
                     == QFileInfo(bPath).canonicalFilePath());
    check("split.b-visible", viewB != nullptr && viewB->isVisible()
                                && viewB->width() > 0 && viewB->height() > 0);
    if (viewA == nullptr || viewB == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }

    // Make pane A the ACTIVE one while B stays visible: the product's own
    // keyboard command (no mouse involved).
    QAction *focusNext = shell->commands() != nullptr
                             ? shell->commands()->action(QStringLiteral("view.focus.next"))
                             : nullptr;
    check("split.focus-action", focusNext != nullptr);
    if (focusNext != nullptr) {
        focusNext->trigger();
    }
    QCoreApplication::processEvents();
    check("split.a-active", shell->editorArea()->activePane() == paneA,
          QStringLiteral("activeIsA=%1")
              .arg(shell->editorArea()->activePane() == paneA ? 1 : 0));

    // Wait for the live poll to install the hover filter on EVERY pane view and
    // publish the state map.
    auto hoverState = [&shell]() { return shell->property("vedit.diagnostics.hover").toMap(); };
    check("hover.state-known",
          waitUntil([&hoverState]() { return hoverState().value(QStringLiteral("known")).toBool(); },
                    5000));
    spin(700); // one poll cycle with the split layout in place

    // =======================================================================
    // 1) REAL mouse over the squiggle in the INACTIVE pane B
    // =======================================================================
    const QPoint bSquiggle = viewB->cursorToCoordinate(KTextEditor::Cursor(1, 6));
    check("hover.b-point", bSquiggle.x() >= 0 && bSquiggle.y() >= 0,
          QStringLiteral("%1,%2").arg(bSquiggle.x()).arg(bSquiggle.y()));
    sendMove(viewB, bSquiggle);
    const QVariantMap afterMove = hoverState();
    std::printf("OBSERVED hover.after-move alive=%d diagnostic=%d rule='%s' path='%s' line=%d "
                "parentIsActiveView=%d\n",
                afterMove.value(QStringLiteral("alive")).toBool() ? 1 : 0,
                afterMove.value(QStringLiteral("diagnostic")).toBool() ? 1 : 0,
                qPrintable(afterMove.value(QStringLiteral("rule")).toString()),
                qPrintable(afterMove.value(QStringLiteral("path")).toString()),
                afterMove.value(QStringLiteral("line")).toInt(),
                afterMove.value(QStringLiteral("parentIsActiveView")).toBool() ? 1 : 0);
    check("hover.popup-alive", afterMove.value(QStringLiteral("alive")).toBool());
    check("hover.shows-b-diagnostic",
          afterMove.value(QStringLiteral("diagnostic")).toBool()
              && afterMove.value(QStringLiteral("rule")).toString() == QStringLiteral("line-length")
              && afterMove.value(QStringLiteral("line")).toInt() == 1
              && QFileInfo(afterMove.value(QStringLiteral("path")).toString()).canonicalFilePath()
                     == QFileInfo(bPath).canonicalFilePath(),
          QStringLiteral("rule='%1' line=%2 path='%3'")
              .arg(afterMove.value(QStringLiteral("rule")).toString())
              .arg(afterMove.value(QStringLiteral("line")).toInt())
              .arg(afterMove.value(QStringLiteral("path")).toString()));
    auto *popup = shell->findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
    check("hover.popup-widget", popup != nullptr);
    if (popup != nullptr) {
        check("hover.popup-parented-to-inactive-b",
              popup->parentWidget() == static_cast<QWidget *>(viewB),
              popup->parentWidget() == static_cast<QWidget *>(viewB)
                  ? QStringLiteral("B")
                  : (popup->parentWidget() == static_cast<QWidget *>(viewA) ? QStringLiteral("A")
                                                                           : QStringLiteral("other")));
        check("hover.popup-inside-b-geometry",
              viewB->rect().contains(popup->geometry().topLeft())
                  && viewB->rect().intersects(popup->geometry()),
              QStringLiteral("popup=%1,%2 %3x%4 bView=%5x%6")
                  .arg(popup->x()).arg(popup->y()).arg(popup->width()).arg(popup->height())
                  .arg(viewB->width()).arg(viewB->height()));
        check("hover.popup-visible", popup->isVisible());
        check("hover.parent-is-not-the-active-view",
              !afterMove.value(QStringLiteral("parentIsActiveView")).toBool());
    }

    // =======================================================================
    // 2) the 400 ms poll must NOT steal the cross-pane bubble (mouse stationary)
    // =======================================================================
    spin(900);
    const QVariantMap afterPoll = hoverState();
    popup = shell->findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
    std::printf("OBSERVED hover.after-poll alive=%d visible=%d rule='%s' path='%s' "
                "parent='%s'\n",
                afterPoll.value(QStringLiteral("alive")).toBool() ? 1 : 0,
                popup != nullptr && popup->isVisible() ? 1 : 0,
                qPrintable(afterPoll.value(QStringLiteral("rule")).toString()),
                qPrintable(afterPoll.value(QStringLiteral("path")).toString()),
                popup != nullptr && popup->parentWidget() == static_cast<QWidget *>(viewB) ? "B" : "?");
    check("poll.does-not-steal-bubble",
          afterPoll.value(QStringLiteral("diagnostic")).toBool()
              && popup != nullptr && popup->isVisible()
              && popup->parentWidget() == static_cast<QWidget *>(viewB),
          QStringLiteral("diagnostic=%1 visible=%2 parentB=%3")
              .arg(afterPoll.value(QStringLiteral("diagnostic")).toBool() ? 1 : 0)
              .arg(popup != nullptr && popup->isVisible() ? 1 : 0)
              .arg(popup != nullptr && popup->parentWidget() == static_cast<QWidget *>(viewB) ? 1
                                                                                              : 0));

    // =======================================================================
    // 3) switch the ACTIVE pane WITHOUT moving the mouse
    // =======================================================================
    if (focusNext != nullptr) {
        focusNext->trigger(); // A -> B
    }
    spin(900);
    check("poll.bubble-survives-b-active",
          hoverState().value(QStringLiteral("diagnostic")).toBool()
              && popup != nullptr && popup->isVisible()
              && popup->parentWidget() == static_cast<QWidget *>(viewB)
              && shell->editorArea()->activePane() == paneB,
          QStringLiteral("activeB=%1 parentB=%2")
              .arg(shell->editorArea()->activePane() == paneB ? 1 : 0)
              .arg(popup != nullptr && popup->parentWidget() == static_cast<QWidget *>(viewB) ? 1
                                                                                              : 0));
    if (focusNext != nullptr) {
        focusNext->trigger(); // B -> A
    }
    spin(900);
    popup = shell->findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
    check("poll.bubble-does-not-jump-to-active-a",
          shell->editorArea()->activePane() == paneA
              && hoverState().value(QStringLiteral("diagnostic")).toBool()
              && popup != nullptr && popup->isVisible()
              && popup->parentWidget() == static_cast<QWidget *>(viewB),
          QStringLiteral("activeA=%1 parentB=%2 visible=%3")
              .arg(shell->editorArea()->activePane() == paneA ? 1 : 0)
              .arg(popup != nullptr && popup->parentWidget() == static_cast<QWidget *>(viewB) ? 1
                                                                                              : 0)
              .arg(popup != nullptr && popup->isVisible() ? 1 : 0));

    // Control: the mouse really drives it - a non-diagnostic position in B
    // hides the bubble.
    const QPoint bBlank = viewB->cursorToCoordinate(KTextEditor::Cursor(2, 0));
    sendMove(viewB, bBlank);
    spin(200);
    popup = shell->findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
    check("hover.control-non-diagnostic-hides",
          !hoverState().value(QStringLiteral("diagnostic")).toBool()
              && (popup == nullptr || !popup->isVisible()),
          QStringLiteral("diagnostic=%1 visible=%2")
              .arg(hoverState().value(QStringLiteral("diagnostic")).toBool() ? 1 : 0)
              .arg(popup != nullptr && popup->isVisible() ? 1 : 0));

    // =======================================================================
    // 4) Ctrl+Alt+click inside B adds a cursor in B, never in A
    // =======================================================================
    auto *multiA = vedit::editor::MultiCursorController::instanceFor(paneA->editorWidget());
    auto *multiB = vedit::editor::MultiCursorController::instanceFor(paneB->editorWidget());
    check("gesture.controllers", multiA != nullptr && multiB != nullptr);
    if (multiA != nullptr && multiB != nullptr) {
        const int beforeA = multiA->cursorCount();
        const int beforeB = multiB->cursorCount();
        const QPoint bClick = viewB->cursorToCoordinate(KTextEditor::Cursor(2, 4));
        sendClick(viewB, bClick, Qt::ControlModifier | Qt::AltModifier);
        const QList<KTextEditor::Cursor> cursorsB = multiB->cursors();
        const bool hasClicked = cursorsB.contains(KTextEditor::Cursor(2, 4));
        std::printf("OBSERVED gesture.ctrl-alt-click a=%d->%d b=%d->%d clicked-in-b=%d\n",
                    beforeA, multiA->cursorCount(), beforeB, multiB->cursorCount(),
                    hasClicked ? 1 : 0);
        check("gesture.b-gained-cursor", multiB->cursorCount() == beforeB + 1,
              QStringLiteral("b=%1->%2").arg(beforeB).arg(multiB->cursorCount()));
        check("gesture.cursor-at-click-point", hasClicked,
              QStringLiteral("cursors=%1").arg(cursorsB.size()));
        check("gesture.a-unchanged", multiA->cursorCount() == beforeA,
              QStringLiteral("a=%1->%2").arg(beforeA).arg(multiA->cursorCount()));
        check("gesture.native-view-cursors",
              vedit::editor::viewCursors(viewB).size() == 2
                  && vedit::editor::viewCursors(viewA).size() == 1,
              QStringLiteral("b=%1 a=%2")
                  .arg(vedit::editor::viewCursors(viewB).size())
                  .arg(vedit::editor::viewCursors(viewA).size()));
    }

    // =======================================================================
    // 5) Ctrl+click inside B dispatches the definition action from B's doc
    // =======================================================================
    QAction *definitionAction = shell->commands() != nullptr
                                    ? shell->commands()->action(
                                          QStringLiteral("diagnostics.definition"))
                                    : nullptr;
    check("definition.action-exists", definitionAction != nullptr);
    if (definitionAction != nullptr) {
        int triggers = 0;
        QObject::connect(definitionAction, &QAction::triggered, definitionAction,
                         [&triggers]() { ++triggers; });
        const bool enabled =
            waitUntil([definitionAction]() { return definitionAction->isEnabled(); }, 8000);
        const KTextEditor::Cursor caretA = viewA->cursorPosition();
        const QPoint bWord = viewB->cursorToCoordinate(KTextEditor::Cursor(0, 8));
        sendClick(viewB, bWord, Qt::ControlModifier);
        std::printf("OBSERVED definition.ctrl-click enabled=%d triggers=%d b-caret=%d:%d "
                    "a-caret=%d:%d doc='%s'\n",
                    enabled ? 1 : 0, triggers, viewB->cursorPosition().line(),
                    viewB->cursorPosition().column(), viewA->cursorPosition().line(),
                    viewA->cursorPosition().column(),
                    qPrintable(viewB->document()->url().toLocalFile()));
        check("definition.action-enabled", enabled,
              definitionAction->statusTip().isEmpty() ? QString()
                                                       : definitionAction->statusTip());
        check("definition.ctrl-click-dispatches", triggers >= 1,
              QStringLiteral("triggers=%1").arg(triggers));
        check("definition.b-caret-at-click",
              viewB->cursorPosition() == KTextEditor::Cursor(0, 8),
              QStringLiteral("b-caret=%1:%2")
                  .arg(viewB->cursorPosition().line())
                  .arg(viewB->cursorPosition().column()));
        check("definition.b-document-is-b",
              QFileInfo(viewB->document()->url().toLocalFile()).canonicalFilePath()
                  == QFileInfo(bPath).canonicalFilePath());
        check("definition.a-carets-untouched", viewA->cursorPosition() == caretA,
              QStringLiteral("a=%1:%2").arg(viewA->cursorPosition().line())
                  .arg(viewA->cursorPosition().column()));
    }

    std::printf("VE-SPLIT-PANE-MOUSE-E2E checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
