#include "search_integration.h"

#include "app_shell.h"
#include "editor_area.h"
#include "jump/jump_registry.h"
#include "panel_registry.h"
#include "search_jump_target.h"
#include "search_panel.h"
#include "search_query.h"

#include <QDebug>
#include <QMainWindow>
#include <QString>

namespace vedit::workspace {

void installSearch(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    if (shell.panels() == nullptr) {
        return;
    }
    (void)options;

    // A-1/T20: the product's ONE jump target lives behind the application-wide
    // registry (src/workspace/jump). Every panel - search results, module tree,
    // diagnostics rows, the file tree, the palette, the breadcrumb bar - reaches
    // it through jump::jumpTo(); none of them installs a target of its own. The
    // target is a child of the shell, so it outlives the panels and dies with
    // the shell; installing it here (the shell has no registration point of its
    // own in this task's boundary) makes the seam live for the whole product.
    auto *jumpTarget = shell.findChild<search::EditorJumpTarget *>();
    if (jumpTarget == nullptr) {
        jumpTarget = new search::EditorJumpTarget(&shell);
    }
    vedit::workspace::jump::setTarget(jumpTarget);

    // AC-1.1/AC-1.2 (batch 1): the search root is the shell's ONE workspace root,
    // read live inside the factory (the shell resolves the persisted session root
    // before the lanes install, and a dock re-inserted after a runtime switch must
    // start at the new root).
    const auto factory = [&shell]() {
        auto *panel = search::createSearchPanel(shell.projectRoot());
        // High 5: the dock's CurrentFile scope follows the ACTIVE editor
        // document. The provider resolves the active pane's file at search and
        // replace time, so tab/pane switches need no extra signal plumbing and
        // a stale hub path from the find bar or a result click is never used.
        panel->setCurrentFileProvider([&shell]() {
            auto *area = shell.editorArea();
            auto *pane = area != nullptr ? area->activePane() : nullptr;
            return pane != nullptr ? pane->currentFilePath() : QString();
        });
        return panel;
    };
    shell.panels()->setFactory(QStringLiteral("search"), factory);

    // A runtime switch rebinds every live search panel: setRootPath() cancels the
    // in-flight scan, clears the previous root's hits and repaints the root label
    // + empty state, so a query after the switch can only reach the NEW root.
    QObject::connect(&shell, &vedit::app::ApplicationShell::projectRootChanged, &shell,
                     [&shell](const QString &root) {
                         for (search::SearchPanel *panel :
                              shell.findChildren<search::SearchPanel *>()) {
                             if (panel->rootPath() != root) {
                                 panel->setRootPath(root);
                             }
                         }
                     });
    // ux-batch-2 B2: the Ctrl+F bar's "whole project" scope hands its query +
    // options to this dock. A live SearchPanel registers itself as the handler;
    // this fallback makes the dock appear (through the shell's own panel seam)
    // when the user has not opened it yet, so the hand-off is never swallowed.
    vedit::search::SearchHub::instance().setProjectSearchHandler(
        &shell, [&shell](const vedit::search::SearchHandoff &handoff) {
            auto apply = [&handoff](search::SearchPanel *panel) {
                if (panel == nullptr) {
                    return false;
                }
                panel->applyHandoff(handoff);
                return true;
            };
            if (auto *panel = shell.findChild<search::SearchPanel *>()) {
                return apply(panel);
            }
            if (shell.panels() != nullptr) {
                shell.panels()->showPanel(QStringLiteral("search"), &shell);
            }
            return apply(shell.findChild<search::SearchPanel *>());
        });

    if (qEnvironmentVariableIsSet("VEDIT_DEBUG")) {
        qInfo("search: installSearch projectRoot='%s' jumpTarget=registry(editor)",
              qPrintable(shell.projectRoot()));
    }
}

} // namespace vedit::workspace
