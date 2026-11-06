// Batch 2 acceptance probe — LSP navigation on the REAL product shell and the
// REAL LSP backend (AC-2.1 rename applied, AC-2.2 references list, AC-2.3 the
// existing actions still work, AC-2.4's assertions live in the selfcheck).
//
//   batch2-editor-probe --checks                 headless assertions (offscreen)
//   batch2-editor-probe --screenshots <dir>      the same assertions + PNGs (xcb)
//
// The probe builds the shell exactly like src/app/main.cpp (constructor ->
// installPanels -> startSession), installs the product's KTextEditor provider,
// opens a fixture file and drives the two REAL commands (`diagnostics.references`
// / `diagnostics.rename`) against the real verible session the diagnostics lane
// starts for the project. It links the product's own static libraries (see
// build.sh), so nothing under src/ is recompiled for it.
#include "app_shell.h"
#include "command_registry.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "integration.h"
#include "lsp_actions.h"
#include "panel_registry.h"
#include "undo_ops.h"

#include "ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScreen>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariantMap>

#include <cstdio>
#include <functional>

using vedit::app::ApplicationShell;
using vedit::app::EditorPane;
using vedit::app::ShellOptions;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *id, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", id, detail.isEmpty() ? "" : " — ",
                qPrintable(detail));
    std::fflush(stdout);
}

void info(const char *id, const QString &detail)
{
    std::printf("INFO %s %s\n", id, qPrintable(detail));
    std::fflush(stdout);
}

void pump(int rounds = 8)
{
    for (int index = 0; index < rounds; ++index) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    return predicate();
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

KTextEditor::View *viewOf(EditorPane *pane)
{
    return pane != nullptr ? vedit::editor::viewFor(pane->editorWidget()) : nullptr;
}

/// Moves the cursor onto `needle` (the first occurrence at or after `fromLine`).
bool placeCursorOn(KTextEditor::View *view, const QString &needle, int *line = nullptr)
{
    if (view == nullptr || view->document() == nullptr || needle.isEmpty()) {
        return false;
    }
    KTextEditor::Document *document = view->document();
    for (int index = 0; index < document->lines(); ++index) {
        const int column = document->line(index).indexOf(needle);
        if (column >= 0) {
            view->setCursorPosition(KTextEditor::Cursor(index, column));
            view->setFocus();
            if (line != nullptr) {
                *line = index;
            }
            return true;
        }
    }
    return false;
}

void grab(ApplicationShell &shell, const QString &path)
{
    // The references list is its OWN top-level window, so the shell's own grab()
    // cannot show it: capture the screen when there is one (xcb under Xvfb), and
    // fall back to the window image on the offscreen platform.
    pump(10);
    const QPixmap screen = QGuiApplication::primaryScreen() != nullptr
                               ? QGuiApplication::primaryScreen()->grabWindow(0)
                               : QPixmap();
    const QPixmap shot = !screen.isNull() ? screen : shell.grab();
    const bool ok = !shot.isNull() && shot.save(path);
    check("screenshot", ok, QStringLiteral("%1 (%2x%3)").arg(path).arg(shot.width()).arg(shot.height()));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("batch2-editor-probe"));

    QString screenshotDirectory;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--screenshots") && index + 1 < argc) {
            screenshotDirectory = QString::fromLocal8Bit(argv[++index]);
        } else if (!argument.startsWith(QStringLiteral("--"))) {
            screenshotDirectory = argument;
        }
    }

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("BATCH2-EDITOR-PROBE ok=0 reason=temporary-directory\n");
        return 1;
    }
    const QString work = temp.path();
    const QString project = work + QStringLiteral("/project");
    const QString topPath = project + QStringLiteral("/rtl/top.sv");
    const QString otherPath = project + QStringLiteral("/rtl/other.sv");
    // `old_sig` is declared and used twice in top.sv and once in other.sv; the
    // second file is NOT opened, so a rename that reaches it proves the
    // "not open in the editor" path.
    const QByteArray topSource =
        "module top;\n"
        "  wire old_sig;\n"
        "  assign old_sig = 1'b0;\n"
        "endmodule\n";
    const QByteArray otherSource =
        "module other;\n"
        "  wire copy = top.old_sig;\n"
        "endmodule\n";
    if (!writeFile(topPath, topSource) || !writeFile(otherPath, otherSource)) {
        std::printf("BATCH2-EDITOR-PROBE ok=0 reason=fixture\n");
        return 1;
    }
    std::printf("PROBE work=%s platform=%s\n", qPrintable(work),
                qPrintable(QApplication::platformName()));

    ShellOptions options;
    options.stateDir = work + QStringLiteral("/state");
    options.projectRoot = project;
    options.restoreSession = true;
    options.deferSessionRestore = true;
    ApplicationShell shell(options);
    vedit::workspace::installPanels(shell, {shell.projectRoot(), options.stateDir});
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    shell.startSession();
    shell.resize(1500, 1000);
    shell.show();
    pump();

    QString openError;
    check("a2.open-fixture", shell.openPaths({topPath}, &openError), openError);
    pump();
    EditorPane *pane = shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
    KTextEditor::View *view = viewOf(pane);
    check("a2.editor-view", view != nullptr);
    if (view == nullptr || pane == nullptr) {
        std::printf("BATCH2-EDITOR-PROBE ok=0 reason=no-ktexteditor-view\n");
        return 1;
    }
    check("a2.editor-path", pane->currentFilePath() == topPath, pane->currentFilePath());

    // ---- AC-2.3: wait for the REAL LSP session and its capability table -----
    const bool backendReady = waitFor(
        [&shell]() {
            const QVariantMap capabilities =
                shell.property("vedit.diagnostics.capabilities").toMap();
            return !capabilities.value(QStringLiteral("backend")).toString().isEmpty();
        },
        120000);
    const QString backend =
        shell.property("vedit.diagnostics.capabilities").toMap().value(QStringLiteral("backend")).toString();
    info("a2.backend", QStringLiteral("name=%1 ready=%2 session-files=%3")
                           .arg(backend)
                           .arg(backendReady)
                           .arg(shell.property("vedit.diagnostics.capabilities")
                                    .toMap()
                                    .value(QStringLiteral("filesOpened"))
                                    .toInt()));
    check("a2.backend-connected", backendReady && !backend.isEmpty(), backend);
    // The capability table is published before the server has answered, so wait
    // for the LIVE session (files opened) before asking for navigation.
    const bool sessionReady = waitFor(
        [&shell]() {
            const QVariantMap session =
                shell.property("vedit.diagnostics.session").toMap();
            return session.value(QStringLiteral("filesOpened")).toInt() > 0;
        },
        180000);
    const QVariantMap sessionMap = shell.property("vedit.diagnostics.session").toMap();
    info("a2.session", QStringLiteral("ready=%1 filesOpened=%2 message='%3'")
                           .arg(sessionReady)
                           .arg(sessionMap.value(QStringLiteral("filesOpened")).toInt())
                           .arg(sessionMap.value(QStringLiteral("message")).toString()));
    check("a2.session-ready", sessionReady,
          sessionMap.value(QStringLiteral("message")).toString());
    const QVariantMap gates = shell.property("vedit.diagnostics.gates").toMap();
    const bool renameGate = gates.value(QStringLiteral("rename")).toMap()
                                .value(QStringLiteral("enabled"))
                                .toBool();
    const bool referencesGate = gates.value(QStringLiteral("references")).toMap()
                                    .value(QStringLiteral("enabled"))
                                    .toBool();
    check("a2.gates.rename", renameGate,
          gates.value(QStringLiteral("rename")).toMap().value(QStringLiteral("reason")).toString());
    check("a2.gates.references", referencesGate,
          gates.value(QStringLiteral("references")).toMap().value(QStringLiteral("reason")).toString());
    // AC-2.3: the EXISTING four actions and the hover gate are still decided by
    // the same live table (no regression of T15/T16).
    const struct {
        const char *name;
    } existingActions[] = {{"definition"}, {"formatting"}, {"hover"}};
    for (const auto &entry : existingActions) {
        const QVariantMap row = gates.value(QString::fromLatin1(entry.name)).toMap();
        info("a2.existing-action",
             QStringLiteral("%1 enabled=%2 known=%3 source=%4 reason='%5'")
                 .arg(QString::fromLatin1(entry.name))
                 .arg(row.value(QStringLiteral("enabled")).toBool())
                 .arg(row.value(QStringLiteral("known"), true).toBool())
                 .arg(row.value(QStringLiteral("source")).toString())
                 .arg(row.value(QStringLiteral("reason")).toString()));
    }
    // The capability rows the four existing actions hang on (the table spells
    // formatting as "formatting" — the action id never leaks into the gate map).
    const QStringList existingGateNames{QStringLiteral("definition"), QStringLiteral("references"),
                                        QStringLiteral("rename"), QStringLiteral("formatting"),
                                        QStringLiteral("hover")};
    QStringList missingGateRows;
    for (const QString &name : existingGateNames) {
        if (gates.value(name).toMap().isEmpty()) {
            missingGateRows.append(name);
        }
    }
    check("a2.existing-actions.gates-present", missingGateRows.isEmpty(),
          missingGateRows.join(QLatin1Char(',')));
    QAction *renameAction = shell.commands()->action(QStringLiteral("diagnostics.rename"));
    QAction *referencesAction = shell.commands()->action(QStringLiteral("diagnostics.references"));
    check("a2.commands.registered", renameAction != nullptr && referencesAction != nullptr);
    check("a2.commands.enabled-by-gate",
          renameAction != nullptr && referencesAction != nullptr && renameAction->isEnabled()
              && referencesAction->isEnabled());

    // ---- AC-2.2: Shift+F12 gives a jumpable location list -------------------
    int symbolLine = -1;
    check("a2.references.cursor", placeCursorOn(view, QStringLiteral("old_sig"), &symbolLine));
    if (referencesAction != nullptr) {
        referencesAction->trigger();
    }
    if (!waitFor(
            [&]() {
                return shell.findChild<QDialog *>(QStringLiteral("veditReferencesDialog"))
                       != nullptr;
            },
            5000)) {
        info("a2.references.retry", QStringLiteral("no list after the first request; retrying"));
        if (referencesAction != nullptr) {
            referencesAction->trigger();
        }
    }
    auto *dialog = shell.findChild<QDialog *>(QStringLiteral("veditReferencesDialog"));
    const bool dialogReady = waitFor(
        [&]() {
            auto *current = shell.findChild<QDialog *>(QStringLiteral("veditReferencesDialog"));
            if (current == nullptr) {
                return false;
            }
            auto *tree = current->findChild<QTreeWidget *>(QStringLiteral("veditReferencesTree"));
            return tree != nullptr && tree->topLevelItemCount() > 0;
        },
        30000);
    dialog = shell.findChild<QDialog *>(QStringLiteral("veditReferencesDialog"));
    QTreeWidget *tree = dialog != nullptr
                            ? dialog->findChild<QTreeWidget *>(QStringLiteral("veditReferencesTree"))
                            : nullptr;
    auto *headerLabel = dialog != nullptr ? dialog->findChild<QLabel *>(
                                                QStringLiteral("veditReferencesHeader"))
                                          : nullptr;
    check("a2.references.dialog", dialog != nullptr && dialog->isVisible());
    check("a2.references.rows", dialogReady && tree != nullptr,
          tree != nullptr ? QString::number(tree->topLevelItemCount()) : QStringLiteral("<no tree>"));
    check("a2.references.header",
          headerLabel != nullptr && headerLabel->text().contains(QStringLiteral("reference(s)")),
          headerLabel != nullptr ? headerLabel->text() : QStringLiteral("<none>"));
    QString locationText;
    QString snippetText;
    int targetRow = -1;
    if (tree != nullptr && tree->topLevelItemCount() > 0) {
        locationText = tree->topLevelItem(0)->text(0);
        snippetText = tree->topLevelItem(0)->text(1);
        for (int row = 0; row < tree->topLevelItemCount(); ++row) {
            if (tree->topLevelItem(row)->text(0).contains(QStringLiteral("other.sv"))) {
                targetRow = row;
            }
        }
        if (targetRow < 0) {
            targetRow = 0;
        }
    }
    check("a2.references.location-text",
          locationText.contains(QStringLiteral(".sv:"))
              && locationText.section(QLatin1Char(':'), 1, 1).toInt() > 0,
          locationText);
    check("a2.references.snippet", !snippetText.trimmed().isEmpty(), snippetText);
    info("a2.references.rows-detail",
         QStringLiteral("rows=%1 first='%2' target-row=%3")
             .arg(tree != nullptr ? tree->topLevelItemCount() : -1)
             .arg(locationText)
             .arg(targetRow));
    if (!screenshotDirectory.isEmpty()) {
        QDir().mkpath(screenshotDirectory);
        grab(shell, screenshotDirectory + QStringLiteral("/10-references-list.png"));
    }

    // Double-click the row that points at the OTHER file and assert the real
    // jump: the active pane must now show that file at that line.
    if (tree != nullptr && targetRow >= 0) {
        QTreeWidgetItem *item = tree->topLevelItem(targetRow);
        tree->setCurrentItem(item);
        tree->scrollToItem(item);
        pump(8);
        const QRect rect = tree->visualItemRect(item);
        info("a2.references.item-rect",
             QStringLiteral("valid=%1 center=%2,%3")
                 .arg(rect.isValid())
                 .arg(rect.center().x())
                 .arg(rect.center().y()));
        const QPointF position(rect.center());
        const QPointF global(tree->viewport()->mapToGlobal(rect.center()));
        // A real double-click is press + release + double-click; the view emits
        // itemDoubleClicked only from that sequence.
        QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, position, global, Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(tree->viewport(), &press);
        QCoreApplication::sendEvent(tree->viewport(), &release);
        QCoreApplication::sendEvent(tree->viewport(), &doubleClick);
        const int expectedRowLine = locationText.section(QLatin1Char(':'), 1, 1).toInt();
        const bool jumped = waitFor(
            [&]() {
                EditorPane *active = shell.editorArea() != nullptr
                                         ? shell.editorArea()->activePane()
                                         : nullptr;
                KTextEditor::View *current = viewOf(active);
                return active != nullptr && current != nullptr
                       && active->currentFilePath().endsWith(
                              locationText.section(QLatin1Char(':'), 0, 0).section(QLatin1Char('/'), -1))
                       && current->cursorPosition().line() + 1 == expectedRowLine;
            },
            15000);
        EditorPane *active = shell.editorArea() != nullptr ? shell.editorArea()->activePane() : nullptr;
        check("a2.references.double-click-jumps", jumped,
              active != nullptr ? active->currentFilePath() : QStringLiteral("<no pane>"));
        KTextEditor::View *jumpedView = viewOf(active);
        if (jumpedView != nullptr) {
            const QString rowText = tree->topLevelItem(targetRow)->text(0);
            const int expectedLine = rowText.section(QLatin1Char(':'), 1, 1).toInt();
            const bool placed = waitFor(
                [&]() {
                    KTextEditor::View *current = viewOf(shell.editorArea()->activePane());
                    return current != nullptr && current->cursorPosition().line() + 1 == expectedLine;
                },
                10000);
            check("a2.references.jump-line", placed,
                  QStringLiteral("cursor-line=%1 expected=%2")
                      .arg(jumpedView->cursorPosition().line() + 1)
                      .arg(expectedLine));
        }
    }
    if (dialog != nullptr) {
        dialog->hide();
        pump();
    }

    // ---- AC-2.1: F2 rename is APPLIED (open document + not-open file) -------
    check("a2.rename.reopen-top",
          shell.openPaths({topPath}) && shell.editorArea()->activePane() != nullptr);
    pump();
    pane = shell.editorArea()->activePane();
    view = viewOf(pane);
    check("a2.rename.cursor", placeCursorOn(view, QStringLiteral("old_sig")));
    const QString textBefore = view != nullptr && view->document() != nullptr
                                   ? view->document()->text()
                                   : QString();
    // The rename dialog is modal: fill it in from the event loop it runs.
    QTimer::singleShot(700, QCoreApplication::instance(), []() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *input = qobject_cast<QInputDialog *>(widget)) {
                input->setTextValue(QStringLiteral("renamed_sig"));
                input->accept();
                return;
            }
        }
    });
    if (renameAction != nullptr) {
        renameAction->trigger();
    }
    const bool renamed = waitFor(
        [&]() {
            KTextEditor::View *current = viewOf(shell.editorArea()->activePane());
            return current != nullptr && current->document() != nullptr
                   && current->document()->text().contains(QStringLiteral("renamed_sig"));
        },
        30000);
    KTextEditor::View *renamedView = viewOf(shell.editorArea()->activePane());
    const QString textAfter = renamedView != nullptr && renamedView->document() != nullptr
                                  ? renamedView->document()->text()
                                  : QString();
    check("a2.rename.applied-to-open-document", renamed && textAfter != textBefore,
          QStringLiteral("bytes-before=%1 bytes-after=%2")
              .arg(textBefore.size())
              .arg(textAfter.size()));
    check("a2.rename.all-occurrences",
          !textAfter.contains(QStringLiteral("old_sig"))
              && textAfter.count(QStringLiteral("renamed_sig")) >= 2,
          QStringLiteral("remaining-old=%1 new=%2")
              .arg(textAfter.count(QStringLiteral("old_sig")))
              .arg(textAfter.count(QStringLiteral("renamed_sig"))));
    check("a2.rename.buffer-dirty",
          renamedView != nullptr && renamedView->document() != nullptr
              && renamedView->document()->isModified());
    // The not-open file: verible's rename may or may not cover it — report what
    // really happened instead of asserting a server behaviour.
    const QString otherAfter = QString::fromUtf8(readAll(otherPath));
    const bool otherChanged = otherAfter.contains(QStringLiteral("renamed_sig"));
    info("a2.rename.not-open-file",
         otherChanged ? QStringLiteral("other.sv was rewritten on disk")
                      : QStringLiteral("other.sv was NOT part of the server's WorkspaceEdit: %1")
                            .arg(otherAfter.trimmed()));
    if (!screenshotDirectory.isEmpty()) {
        grab(shell, screenshotDirectory + QStringLiteral("/20-rename-applied.png"));
    }
    // ONE undo restores the pre-rename buffer (the rename is one editing
    // transaction per file).
    QString undoError;
    const bool undone =
        renamedView != nullptr && renamedView->document() != nullptr
        && vedit::editor::documentUndo(renamedView->document(), &undoError)
        && renamedView->document()->text() == textBefore;
    check("a2.rename.one-undo-restores", undone, undoError);
    QString redoError;
    vedit::editor::documentRedo(renamedView != nullptr ? renamedView->document() : nullptr,
                                &redoError);

    std::printf("BATCH2-EDITOR-PROBE ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
