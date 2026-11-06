// RM-3 — the project variables dialog must allow ADDING and DELETING, not only
// editing, and the table must survive a restart and an A -> B -> A switch.
//
// The real-machine report: the "环境变量" dialog that pops after opening or
// switching a project only let the user EDIT the names the project already knew
// ("不能手动加、删"). This binary drives the REAL dialog and the REAL
// ProjectPanel/ProjectModel (no modal windows, no fake model):
//
//   * the dialog adds a brand-new NAME + value and deletes a row;
//   * invalid / duplicate / empty names are refused INLINE (a reason next to the
//     row), never by a modal and never silently;
//   * the panel REPLACES the model's table with what the dialog returns, so a
//     deleted variable really disappears (appending made it immortal);
//   * the value is used by the expansion's substitution: the project file set
//     follows `$NEWVAR`;
//   * the table is persisted PER ROOT and survives a restart and A -> B -> A;
//     while B is open nothing from A is visible, and deleting a REQUIRED name
//     makes the project report it missing again (never silent).
//
// Headless: QT_QPA_PLATFORM=offscreen (the CMake test registration sets it).

#include "project_model.h"
#include "project_panel.h"
#include "project_state.h"
#include "project_variables_dialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

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
    std::fflush(stdout);
}

bool waitFor(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

bool writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

/// Wait until `root` has expanded and its inputs are re-derived.
bool settled(vedit::workspace::project::ProjectModel &model, const QString &root,
             int timeoutMs = 30000)
{
    return waitFor(
        [&model, &root]() {
            return model.projectRoot() == root && !model.isPending();
        },
        timeoutMs);
}

/// What the panel does when the dialog is accepted: the dialog's table replaces
/// the project table.
bool applyDialog(vedit::workspace::project::ProjectPanel &panel,
                 vedit::workspace::project::ProjectModel &model,
                 const std::function<void(vedit::workspace::project::ProjectVariablesDialog &)> &edit)
{
    const QMap<QString, QString> known =
        vedit::workspace::project::ProjectVariablesDialog::parseTable(model.variables());
    vedit::workspace::project::ProjectVariablesDialog dialog(panel.variableRowNames(), known);
    edit(dialog);
    if (!dialog.validateRows()) {
        return false;
    }
    return panel.applyVariableTable(dialog.variableTable());
}

bool hasEntry(const QStringList &table, const QString &name)
{
    for (const QString &entry : table) {
        if (entry.startsWith(name + QLatin1Char('='))) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using namespace vedit::workspace::project;

    QTemporaryDir dir;
    if (!dir.isValid()) {
        check("rm3.tempdir", false);
        return 1;
    }
    const QString rootA = dir.filePath(QStringLiteral("project-a"));
    const QString rootB = dir.filePath(QStringLiteral("project-b"));
    // The variable-expanded entries point OUTSIDE the root on purpose: the
    // folder scan cannot find them, so only a correctly substituted `$VAR` can
    // put them in the file set.
    const QString sharedDir = dir.filePath(QStringLiteral("shared"));
    writeFile(rootA + QStringLiteral("/project.f"),
              QStringLiteral("../shared/$NEWVAR.sv\n"));
    writeFile(sharedDir + QStringLiteral("/v1.sv"), QStringLiteral("module v1; endmodule\n"));
    writeFile(sharedDir + QStringLiteral("/v2.sv"), QStringLiteral("module v2; endmodule\n"));
    writeFile(rootB + QStringLiteral("/project.f"), QStringLiteral("../shared/$BVAR.sv\n"));
    writeFile(sharedDir + QStringLiteral("/vb.sv"), QStringLiteral("module vb; endmodule\n"));
    const QString listedA = QFileInfo(sharedDir + QStringLiteral("/v1.sv")).absoluteFilePath();
    const QString listedB = QFileInfo(sharedDir + QStringLiteral("/vb.sv")).absoluteFilePath();

    // =====================================================================
    // 1. The DIALOG: add, delete, and inline validation
    // =====================================================================
    {
        ProjectVariablesDialog dialog({QStringLiteral("MISSING")},
                                      {{QStringLiteral("A"), QStringLiteral("1")}});
        check("rm3.dialog.shows-whole-table",
              dialog.rowCount() == 2 && dialog.names().contains(QStringLiteral("A"))
                  && dialog.names().contains(QStringLiteral("MISSING")),
              QStringLiteral("rows=%1 names=%2")
                  .arg(dialog.rowCount())
                  .arg(dialog.names().join(QLatin1Char(','))));
        check("rm3.dialog.known-value-prefilled",
              dialog.values().value(QStringLiteral("A")) == QStringLiteral("1"));

        check("rm3.dialog.add", dialog.addVariable(QStringLiteral("NEW"), QStringLiteral("x")));
        check("rm3.dialog.add-in-table",
              dialog.rowCount() == 3 && hasEntry(dialog.variableTable(), QStringLiteral("NEW")));

        // A duplicate name is refused with an inline reason, not a modal.
        check("rm3.dialog.duplicate-refused",
              !dialog.addVariable(QStringLiteral("NEW"), QStringLiteral("y"))
                  && !dialog.rowReason(QStringLiteral("NEW")).isEmpty(),
              dialog.rowReason(QStringLiteral("NEW")));

        // Malformed names: whitespace / '=' / '$' / leading digit / empty.
        const QStringList invalid{QStringLiteral("BAD NAME"), QStringLiteral("A=B"),
                                  QStringLiteral("$X"), QStringLiteral("1BAD"), QString()};
        bool allRefused = true;
        QStringList reasons;
        for (const QString &name : invalid) {
            const bool added = dialog.addVariable(name, QStringLiteral("v"));
            const QString reason = dialog.rowReason(name);
            if (added || reason.isEmpty()) {
                allRefused = false;
            }
            reasons.append(reason);
        }
        check("rm3.dialog.invalid-refused", allRefused, reasons.join(QStringLiteral(" | ")));

        // Delete removes the row and says so.
        check("rm3.dialog.delete", dialog.removeVariable(QStringLiteral("A")));
        check("rm3.dialog.delete-removes-entry",
              !hasEntry(dialog.variableTable(), QStringLiteral("A")));
        check("rm3.dialog.delete-note", dialog.note().contains(QStringLiteral("Removed A")),
              dialog.note());

        // Deleting a name the project ASKED for states the consequence inline.
        check("rm3.dialog.delete-required",
              dialog.removeVariable(QStringLiteral("MISSING"))
                  && dialog.note().contains(QStringLiteral("still needs it")),
              dialog.note());
    }

    // =====================================================================
    // 2. The panel replaces the model's table; the expansion uses the value
    // =====================================================================
    const QString stateDir = dir.filePath(QStringLiteral("state"));
    auto model = std::make_unique<ProjectModel>(stateDir);
    model->open(rootA);
    check("rm3.expand.missing-reported",
          waitFor([&]() { return model->missingVariables().contains(QStringLiteral("NEWVAR")); },
                  30000)
              && model->missingVariables() == QStringList{QStringLiteral("NEWVAR")},
          model->missingVariables().join(QLatin1Char(',')));

    ProjectPanel panel(model.get());
    panel.setInteractiveDialogs(false); // headless: the dialog is driven directly

    // ADD: a brand-new name, exactly through the dialog the panel opens.
    check("rm3.panel.add-applied",
          applyDialog(panel, *model, [](ProjectVariablesDialog &dialog) {
              // The empty row the project already asked for: fill it, as a user does.
              dialog.setValue(QStringLiteral("NEWVAR"), QStringLiteral("v1"));
          }));
    check("rm3.substitution-uses-new-value",
          settled(*model, rootA) && model->files().contains(listedA),
          model->files().join(QLatin1Char(',')));
    check("rm3.table-has-added", hasEntry(model->variables(), QStringLiteral("NEWVAR")),
          model->variables().join(QLatin1Char(',')));

    // ADD a second variable, then DELETE it (a different one stays).
    check("rm3.panel.add-second",
          applyDialog(panel, *model, [](ProjectVariablesDialog &dialog) {
              dialog.addVariable(QStringLiteral("EXTRA"), QStringLiteral("e"));
          }));
    check("rm3.panel.delete-applied",
          applyDialog(panel, *model, [](ProjectVariablesDialog &dialog) {
              dialog.removeVariable(QStringLiteral("EXTRA"));
          }));
    check("rm3.panel.delete-removed-from-model",
          !hasEntry(model->variables(), QStringLiteral("EXTRA"))
              && hasEntry(model->variables(), QStringLiteral("NEWVAR")),
          model->variables().join(QLatin1Char(',')));
    check("rm3.panel.shows-table",
          panel.displayedVariables().contains(QStringLiteral("NEWVAR=v1")));

    // =====================================================================
    // 3. RESTART: the table is persisted and comes back for the same root
    // =====================================================================
    const QString statePath = QFileInfo(model->statePath()).absoluteFilePath();
    const QString json = readText(statePath);
    check("rm3.persisted-file-has-per-root-map",
          json.contains(QStringLiteral("variablesByRoot"))
              && json.contains(rootA) && json.contains(QStringLiteral("NEWVAR=v1")),
          statePath);

    model.reset();
    auto restarted = std::make_unique<ProjectModel>(stateDir);
    check("rm3.restart.restore-loads", restarted->restoreFromDisk());
    check("rm3.restart.variables-restored",
          restarted->variables().contains(QStringLiteral("NEWVAR=v1")),
          restarted->variables().join(QLatin1Char(',')));
    restarted->open(rootA);
    check("rm3.restart.re-expands-with-table",
          settled(*restarted, rootA) && restarted->files().contains(listedA));

    // =====================================================================
    // 4. SWITCH A -> B -> A: per-root tables, nothing leaks
    // =====================================================================
    restarted->open(rootB);
    check("rm3.switch.b-settled", settled(*restarted, rootB));
    check("rm3.switch.b-has-no-a-variable",
          !hasEntry(restarted->variables(), QStringLiteral("NEWVAR")),
          restarted->variables().join(QLatin1Char(',')));
    check("rm3.switch.b-reports-own-missing",
          restarted->missingVariables().contains(QStringLiteral("BVAR")),
          restarted->missingVariables().join(QLatin1Char(',')));

    ProjectPanel panelB(restarted.get());
    panelB.setInteractiveDialogs(false);
    check("rm3.switch.b-add-applied",
          applyDialog(panelB, *restarted, [](ProjectVariablesDialog &dialog) {
              dialog.setValue(QStringLiteral("BVAR"), QStringLiteral("vb"));
          }));
    check("rm3.switch.b-substitutes",
          settled(*restarted, rootB) && restarted->files().contains(listedB));

    restarted->open(rootA);
    check("rm3.switch.back-to-a-settled", settled(*restarted, rootA));
    check("rm3.switch.a-table-restored",
          hasEntry(restarted->variables(), QStringLiteral("NEWVAR"))
              && !hasEntry(restarted->variables(), QStringLiteral("BVAR")),
          restarted->variables().join(QLatin1Char(',')));
    check("rm3.switch.a-substitutes-again", restarted->files().contains(listedA),
          restarted->files().join(QLatin1Char(',')));
    const QString jsonAfterSwitch = readText(QFileInfo(restarted->statePath()).absoluteFilePath());
    check("rm3.switch.both-roots-persisted",
          jsonAfterSwitch.contains(rootA) && jsonAfterSwitch.contains(rootB)
              && jsonAfterSwitch.contains(QStringLiteral("NEWVAR=v1"))
              && jsonAfterSwitch.contains(QStringLiteral("BVAR=vb")));

    // =====================================================================
    // 5. REQUIRED NAME: deleting it never vanishes silently
    // =====================================================================
    ProjectPanel panelA(restarted.get());
    panelA.setInteractiveDialogs(false);
    check("rm3.required.delete-applied",
          applyDialog(panelA, *restarted, [](ProjectVariablesDialog &dialog) {
              dialog.removeVariable(QStringLiteral("NEWVAR"));
          }));
    check("rm3.required.delete-removed-value",
          !hasEntry(restarted->variables(), QStringLiteral("NEWVAR")),
          restarted->variables().join(QLatin1Char(',')));
    check("rm3.required.reported-missing-again",
          settled(*restarted, rootA)
              && restarted->missingVariables().contains(QStringLiteral("NEWVAR")),
          restarted->missingVariables().join(QLatin1Char(',')));
    check("rm3.required.panel-no-longer-shows-value",
          !panelA.displayedVariables().contains(QStringLiteral("NEWVAR=v1")));
    check("rm3.required.dialog-asks-again", [&]() {
        const QMap<QString, QString> known =
            ProjectVariablesDialog::parseTable(restarted->variables());
        ProjectVariablesDialog dialog(panelA.variableRowNames(), known);
        return dialog.names().contains(QStringLiteral("NEWVAR"));
    }());

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
