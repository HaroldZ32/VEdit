#include "integration.h"

#include "app_shell.h"

// The jump lane is a contract + process-wide registry (no dock). It is installed
// first so a new shell never inherits the previous shell's jump target; the real
// target is registered later by the shell/editor lane.
#if defined(VEDIT_WORKSPACE_HAVE_JUMP)
#include "jump/jump_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_PALETTE)
#include "palette/palette_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
#include "filetree/filetree_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SEARCH)
#include "search/search_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_PROJECT)
#include "project/project_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_MODULE_TREE)
#include "module_tree/module_tree_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
#include "diagnostics/diagnostics_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_VCS)
#include "vcs/vcs_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SETTINGS)
#include "settings/settings_integration.h"
#endif

namespace vedit::workspace {

void installPanels(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;

#if defined(VEDIT_WORKSPACE_HAVE_JUMP)
    installJump(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_PALETTE)
    installPalette(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
    installFileTree(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SEARCH)
    installSearch(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_PROJECT)
    installProject(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_MODULE_TREE)
    // After the project lane: the module tree consumes the project model's
    // persisted root/file set as its single source of truth.
    installModuleTree(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
    installDiagnostics(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_VCS)
    installVcs(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SETTINGS)
    installSettings(shell, options);
#endif
}

} // namespace vedit::workspace
