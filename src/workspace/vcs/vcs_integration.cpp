#include "vcs_integration.h"

#include "app_shell.h"
#include "panel_registry.h"
#include "vcs_history.h"
#include "vcs_panel.h"
#include "vcs_source.h"
#include "vcs_toolchain.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLabel>
#include <QString>
#include <QTabWidget>

#include <memory>

namespace vedit::workspace {

namespace {

// AC-1.1/AC-1.2 (batch 1): the ONE place a VCS panel is pointed at the workspace
// root. The panel's own API has setRoot() + refresh() as two steps (setRoot()
// only stores the string), so both go through here.
//
// The header widget shows backend / branch / revision — the ROOT itself is
// carried in its tooltip and in a probe-readable property, so a user hovering the
// header (and the acceptance probe reading it) can tell which working copy this
// snapshot describes after a folder switch.
void applyRoot(vcs::VcsPanel *panel, const QString &root)
{
    if (panel == nullptr) {
        return;
    }
    panel->setRoot(root.toStdString());
    if (auto *header = panel->findChild<QLabel *>(QStringLiteral("vcs-header"))) {
        header->setToolTip(root);
    }
    panel->setProperty("veditProjectRoot", root);
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("vcs: root='%s'", root.isEmpty() ? "<none>" : qPrintable(root));
    }
    panel->refresh();
}

} // namespace

void installVcs(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    (void)options;
    // Batch 6a (rev3): the lane's init is the observable point for the bundled
    // VCS toolchain — one `VCS-TOOLCHAIN ...` line, printed once per process
    // (the panel factory reuses the cached values for its properties).
    (void)vcs::reportToolchainOnce();
    // The root is read from the shell INSIDE the factory (never captured): the
    // shell resolves the persisted session root before the lanes install, and a
    // dock re-inserted after a runtime switch must start at the NEW root.
    // Batch 4b: the dock carries two tabs — the working tree (batch 3b/4a) and the
    // history view (commit list / detail / file history / blame / comparison).
    // Both are pointed at the same workspace root and both read asynchronously.
    const auto factory = [&shell]() {
        auto *tabs = new QTabWidget();
        tabs->setObjectName(QStringLiteral("vcs-tabs"));
        auto *panel = new vcs::VcsPanel(tabs);
        panel->setSource(std::make_unique<vcs::FfiVcsSource>());
        auto *history = new vcs::VcsHistoryPanel(tabs);
        history->setSource(std::make_unique<vcs::FfiVcsSource>());
        history->setRoot(shell.projectRoot().toStdString());
        applyRoot(panel, shell.projectRoot());
        tabs->addTab(panel, QCoreApplication::translate("VEdit", "Working tree"));
        tabs->addTab(history, QCoreApplication::translate("VEdit", "History"));
        // Batch 6a (rev3): the chosen VCS toolchain is probe-visible. The line
        // itself is printed once by the lane; here it becomes a property so an
        // acceptance probe can assert `source=bundled` and the versions.
        const vcs::ToolchainStatus &toolchain = vcs::reportToolchainOnce();
        for (QWidget *widget : {static_cast<QWidget *>(tabs), static_cast<QWidget *>(panel), static_cast<QWidget *>(history)}) {
            // Batch 7: one shared helper — the lane asserts the SAME properties.
            vcs::applyToolchainProperties(widget, toolchain);
        }
        return tabs;
    };
    shell.panels()->setFactory(QStringLiteral("vcs"), factory);

    // A runtime switch repoints every live panel and re-reads its status/diff
    // asynchronously, so the header can only describe the new working copy.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [&shell](const QString &root) {
                         for (vcs::VcsPanel *panel : shell.findChildren<vcs::VcsPanel *>()) {
                             applyRoot(panel, root);
                         }
                         for (vcs::VcsHistoryPanel *panel : shell.findChildren<vcs::VcsHistoryPanel *>()) {
                             panel->setRoot(root.toStdString());
                             panel->refresh();
                         }
                     });
    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("vcs: installVcs projectRoot='%s'", qPrintable(shell.projectRoot()));
    }
}

} // namespace vedit::workspace
