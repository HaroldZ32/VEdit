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
#if defined(VEDIT_WORKSPACE_HAVE_VCS)
#include "filetree/svn_explorer_integration.h"
#endif
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
// Batch 20261110: the new workspace lanes. Each header/entry point is the same
// convention as the lanes above (<lane>/<lane>_integration.h, install<Lane>), and
// the VEDIT_WORKSPACE_HAVE_<LANE> macro is only defined when the lane's directory
// and its integration file exist — so this block is inert until its task lands.
#if defined(VEDIT_WORKSPACE_HAVE_RENAME)
#include "rename/rename_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_VHDL)
#include "vhdl/vhdl_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SCALA)
#include "scala/scala_integration.h"
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SIGNAL_TRACE)
#include "signal_trace/signal_trace_integration.h"
#endif

namespace vedit::workspace {

void installPanels(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;

    // T63 (batch 20261110c, AC-4.7): the imported environment selection must be
    // in place BEFORE any lane installs — installing a lane creates panels, and
    // a panel's first refresh can already start a tool child (lint / language
    // server / VCS / terminal). The shell constructor only read the session
    // archive, so nothing has spawned yet; this is the last safe moment, and
    // `installSettings()` is deliberately NOT the owner (it runs after every
    // other lane).
#if defined(VEDIT_WORKSPACE_HAVE_SETTINGS)
    applyEnvironmentSelection(options);
#endif

#if defined(VEDIT_WORKSPACE_HAVE_JUMP)
    installJump(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_PALETTE)
    installPalette(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_FILETREE)
#if defined(VEDIT_WORKSPACE_HAVE_VCS)
    installFileTree(shell, options, svnExplorerMenu(shell));
#else
    installFileTree(shell, options);
#endif
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
    // Batch 20261110: the new lanes install last. `rename` consumes the project
    // model (installed above) and the editor; `vhdl` and `scala` are independent
    // of the other lanes and of each other.
#if defined(VEDIT_WORKSPACE_HAVE_RENAME)
    installRename(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_VHDL)
    installVhdl(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SCALA)
    installScala(shell, options);
#endif
#if defined(VEDIT_WORKSPACE_HAVE_SIGNAL_TRACE)
    installSignalTrace(shell, options);
#endif
}

} // namespace vedit::workspace
