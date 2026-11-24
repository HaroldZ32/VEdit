#include "project_lint_panel.h"
#include "../../common/item_context_menu.h"

#include "app_shell.h"
#include "command_registry.h"
#include "diagnostics_controller.h"

#include "jump/jump_registry.h"

#include "panel_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace vedit::workspace::diagnostics {

namespace {

/// The row text of one issue: `name.sv:12:5 — message [rule]`. The file NAME is
/// enough here because the row lives under its file's group header.
QString issueText(const DiagnosticEntry &entry)
{
    QString text = QStringLiteral("%1:%2 — %3")
                       .arg(entry.line + 1)
                       .arg(entry.column + 1)
                       .arg(entry.message);
    if (!entry.code.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.code);
    } else if (!entry.source.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.source);
    }
    return text;
}

QString phaseText(const LintPassSnapshot &pass)
{
    switch (pass.phase) {
    case LintPhase::Idle:
        return QCoreApplication::translate("VEdit", "not run yet");
    case LintPhase::Queued:
    case LintPhase::Running:
        return QCoreApplication::translate("VEdit", "running…");
    case LintPhase::Cancelled:
        return QCoreApplication::translate("VEdit", "cancelled");
    case LintPhase::Finished:
        return QCoreApplication::translate("VEdit", "finished");
    case LintPhase::Failed:
        return QCoreApplication::translate("VEdit", "failed: %1").arg(pass.message);
    }
    return QString();
}

} // namespace

QString projectFileSetStateText(const QVariantMap &state)
{
    const QString phase = state.value(QStringLiteral("phase")).toString();
    const int files = state.value(QStringLiteral("files")).toInt();
    const int softTimeoutMs = state.value(QStringLiteral("softTimeoutMs")).toInt();
    const QString detail = state.value(QStringLiteral("message")).toString();
    if (phase == QStringLiteral("no-filelist")) {
        return QCoreApplication::translate(
            "VEdit", "No project file set yet — import a .f filelist in the Project panel first");
    }
    if (phase == QStringLiteral("expanding")) {
        return QCoreApplication::translate(
                   "VEdit", "Project file set is being expanded… (%1 file(s) so far)")
            .arg(files);
    }
    if (phase == QStringLiteral("slow")) {
        // L2.1: the soft timeout does NOT discard the job. This is the persistent
        // line the user reads while a 20k-file expansion is still working.
        return QCoreApplication::translate(
                   "VEdit",
                   "Project file set expansion is still running after %1 s — the poller keeps "
                   "waiting for the same job instead of discarding it")
            .arg(softTimeoutMs / 1000);
    }
    if (phase == QStringLiteral("failed")) {
        return QCoreApplication::translate(
                   "VEdit", "Project file set expansion failed: %1 — press Run Project Lint to "
                            "retry the expansion")
            .arg(detail.isEmpty() ? QCoreApplication::translate("VEdit", "unknown error") : detail);
    }
    if (phase == QStringLiteral("ready")) {
        if (files == 0) {
            return QCoreApplication::translate(
                "VEdit",
                "The project file set expanded to 0 source files — check the imported .f "
                "filelist and the project root");
        }
        return QCoreApplication::translate("VEdit", "Project file set: %1 file(s)").arg(files);
    }
    return QString();
}

ProjectLintPanel::ProjectLintPanel(DiagnosticsController *controller,
                                   vedit::app::ApplicationShell *shell, QWidget *parent)
    : QWidget(parent)
    , controller_(controller)
    , shell_(shell)
{
    setObjectName(QStringLiteral("project-lint-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *toolbar = new QHBoxLayout();
    run_ = new QPushButton(QCoreApplication::translate("VEdit", "Run Project Lint"), this);
    run_->setObjectName(QStringLiteral("project-lint-run"));
    run_->setToolTip(QCoreApplication::translate(
        "VEdit", "Run the whole-project lint (the same command as Project > Lint Project)"));
    toolbar->addWidget(run_);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("project-lint-summary"));
    summary_->setWordWrap(true);
    toolbar->addWidget(summary_, 1);
    layout->addLayout(toolbar);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("project-lint-tree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({QCoreApplication::translate("VEdit", "File / Issue"),
                            QCoreApplication::translate("VEdit", "Count")});
    tree_->setUniformRowHeights(true);
    tree_->setRootIsDecorated(true);
    tree_->setAlternatingRowColors(true);
    tree_->header()->setStretchLastSection(false);
    layout->addWidget(tree_, 1);

    empty_ = new QLabel(this);
    empty_->setObjectName(QStringLiteral("project-lint-empty"));
    empty_->setWordWrap(true);
    layout->addWidget(empty_);

    if (run_ != nullptr) {
        QObject::connect(run_, &QPushButton::clicked, this, [this]() {
            // AC-4.2 user visibility: a restored session may have the dock
            // HIDDEN, and a result nobody can see is no result. Show the dock
            // (the same registry command the View menu uses) before running.
            if (shell_ != nullptr && shell_->commands() != nullptr) {
                // The dock's own registered command (the View menu item); the
                // panel registry name is the fallback for a build that only has
                // the registry.
                if (QAction *show = shell_->commands()->action(
                        QStringLiteral("view.dock.project-lint"))) {
                    show->trigger();
                } else if (shell_->panels() != nullptr) {
                    shell_->panels()->showPanel(QStringLiteral("project-lint"), shell_);
                }
            }
            if (shell_ != nullptr && shell_->commands() != nullptr) {
                if (QAction *action = shell_->commands()->action(QStringLiteral("project.lint"))) {
                    action->trigger();
                    return;
                }
            }
            // No command registry (a lane-only build): the panel says so instead
            // of silently doing nothing.
            if (empty_ != nullptr) {
                empty_->setText(QCoreApplication::translate(
                    "VEdit", "The project lint command is not available in this build"));
                empty_->setVisible(true);
            }
        });
    }
    QObject::connect(tree_, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem *item, int) { jumpToItem(item); });
    new vedit::common::ItemContextMenu(tree_, [this](QMenu &menu, const QPersistentModelIndex &hit) {
        const QModelIndex row = hit.sibling(hit.row(), 0);
        const QString file = row.data(Qt::UserRole).toString();
        QAction *open = menu.addAction(tr("Go to Diagnostic"), this, [this, hit]() {
            if (hit.isValid()) {
                jumpToItem(tree_->itemFromIndex(hit));
            }
        });
        open->setEnabled(!file.isEmpty() && row.data(Qt::UserRole + 1).isValid());
        vedit::common::addCopyAction(menu, tr("Copy File Path"), file);
    });

    timer_ = new QTimer(this);
    timer_->setInterval(300);
    QObject::connect(timer_, &QTimer::timeout, this, [this]() { rebuild(); });
    timer_->start();
    rebuild();
}

QString ProjectLintPanel::summaryText() const
{
    return summary_ != nullptr ? summary_->text() : QString();
}

void ProjectLintPanel::rebuild()
{
    if (controller_ == nullptr) {
        return;
    }
    const LintPassSnapshot pass = controller_->projectPass();
    const QVector<DiagnosticEntry> rows = controller_->upperLayerDiagnostics();
    // batch-20261116 L2.2: the file-set poller's state and the last Run's answer.
    // Both are read through the shell-property seam (the poller is created by the
    // same lane, so no link edge to a new class is needed) and both are part of
    // the rebuild signature: an empty dock must re-render when the reason moves
    // (loading -> ready / slow / failed), not only when a pass finishes.
    const QVariantMap filesState = shell_ != nullptr
        ? shell_->property("vedit.diagnostics.projectFilesState").toMap()
        : QVariantMap{};
    const QVariantMap runState = shell_ != nullptr
        ? shell_->property("vedit.diagnostics.projectLintRun").toMap()
        : QVariantMap{};
    const QString filesReason = projectFileSetStateText(filesState);

    // One signature per (serial, rows, file-set state) so an idle 300 ms tick
    // never rebuilds the tree - a project-sized list is expensive to clear and
    // refill - while a reason change still repaints.
    const quint64 serial = controller_->projectPassSerial();
    QString signature =
        QStringList{QString::number(serial),
                    QString::number(static_cast<int>(pass.phase)),
                    QString::number(controller_->projectFiles().size()),
                    filesState.value(QStringLiteral("phase")).toString(),
                    filesState.value(QStringLiteral("message")).toString(),
                    runState.value(QStringLiteral("retry")).toString()}
            .join(QLatin1Char('|'));
    for (const DiagnosticEntry &entry : rows) {
        signature += QStringLiteral("%1:%2:%3:%4;")
                         .arg(entry.file)
                         .arg(entry.line)
                         .arg(entry.severity)
                         .arg(qHash(entry.message));
    }
    if (summary_ != nullptr) {
        int errors = 0;
        int warnings = 0;
        for (const DiagnosticEntry &entry : rows) {
            if (entry.severity == 1) {
                ++errors;
            } else if (entry.severity == 2) {
                ++warnings;
            }
        }
        const QString phase = pass.running() && pass.total > 0
            ? QCoreApplication::translate("VEdit", "running %1/%2").arg(pass.done).arg(pass.total)
            : phaseText(pass);
        summary_->setText(
            QCoreApplication::translate("VEdit", "Project lint: %1 · %2 file(s) · %3 error(s) · "
                                                 "%4 warning(s) · %5")
                .arg(phase)
                .arg(controller_->projectFiles().size())
                .arg(errors)
                .arg(warnings)
                .arg(controller_->lastPassWasAutomatic()
                         ? QCoreApplication::translate("VEdit", "automatic")
                         : QCoreApplication::translate("VEdit", "manual")));
    }
    if (signature == lastSignature_) {
        return;
    }
    lastSignature_ = signature;
    lastSerial_ = serial;

    if (tree_ != nullptr) {
        tree_->clear();
    }
    issueRowCount_ = 0;
    fileRowCount_ = 0;

    // Group per file so a project-sized result stays readable; groups sorted by
    // path, issues by position (deterministic, unlike the backend's order).
    QHash<QString, QVector<DiagnosticEntry>> byFile;
    for (const DiagnosticEntry &entry : rows) {
        byFile[entry.file].append(entry);
    }
    QStringList files = byFile.keys();
    std::sort(files.begin(), files.end());
    for (const QString &file : files) {
        QVector<DiagnosticEntry> issues = byFile.value(file);
        std::sort(issues.begin(), issues.end(),
                  [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
                      if (left.line != right.line) {
                          return left.line < right.line;
                      }
                      return left.column < right.column;
                  });
        if (tree_ == nullptr) {
            continue;
        }
        auto *group = new QTreeWidgetItem(tree_);
        group->setText(0, QFileInfo(file).fileName());
        group->setText(1, QString::number(issues.size()));
        group->setToolTip(0, file);
        group->setData(0, Qt::UserRole, file);
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
        for (const DiagnosticEntry &entry : issues) {
            auto *row = new QTreeWidgetItem(group);
            row->setText(0, issueText(entry));
            row->setToolTip(0, QStringLiteral("%1:%2:%3 — %4")
                                   .arg(entry.file)
                                   .arg(entry.line + 1)
                                   .arg(entry.column + 1)
                                   .arg(entry.message));
            row->setData(0, Qt::UserRole, entry.file);
            row->setData(0, Qt::UserRole + 1, entry.line);
            row->setData(0, Qt::UserRole + 2, entry.column);
            row->setData(0, Qt::UserRole + 3, entry.severity);
            ++issueRowCount_;
        }
        group->setExpanded(true);
        ++fileRowCount_;
    }
    if (tree_ != nullptr) {
        tree_->setVisible(issueRowCount_ > 0);
    }
    if (empty_ != nullptr) {
        if (issueRowCount_ > 0) {
            empty_->clear();
            empty_->setVisible(false);
        } else {
            // AC-4.5/AC-4.2 + batch-20261116 L2.2: the empty state names the
            // reason AND the action - never a blank card, and never a "nothing
            // here" that outlives the reason. With no file set the reason is the
            // POLLER's own state (loading / slow / failed / no filelist), read
            // from the same map the poller publishes; the label stays visible
            // until a result or a different reason replaces it (it is not tied to
            // the 8 s status-bar flash).
            QString reason;
            if (pass.phase == LintPhase::Failed) {
                reason = QCoreApplication::translate("VEdit", "The last project lint failed: %1")
                             .arg(pass.message);
            } else if (controller_->projectFiles().isEmpty()) {
                reason = filesReason.isEmpty()
                    ? QCoreApplication::translate(
                          "VEdit",
                          "No project file set yet — import a .f filelist in the Project "
                          "panel first")
                    : filesReason;
                if (runState.value(QStringLiteral("retry")).toString()
                    == QStringLiteral("started")) {
                    reason += QLatin1Char(' ')
                        + QCoreApplication::translate(
                              "VEdit", "Run restarted the expansion.");
                }
            } else {
                reason = QCoreApplication::translate(
                    "VEdit", "No whole-project diagnostics — press Run Project Lint");
            }
            empty_->setText(reason);
            empty_->setVisible(true);
        }
    }
}

void ProjectLintPanel::jumpToItem(QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    const QVariant file = item->data(0, Qt::UserRole);
    const QVariant line = item->data(0, Qt::UserRole + 1);
    if (!file.isValid() || !line.isValid()) {
        return; // a file group header has no position
    }
    // The jump contract is 1-based; the controller stores 0-based positions.
    vedit::workspace::jump::jumpTo(file.toString(), line.toInt() + 1, item->data(0, Qt::UserRole + 2).toInt() + 1);
}

} // namespace vedit::workspace::diagnostics
