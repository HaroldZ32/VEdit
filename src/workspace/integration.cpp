#include "integration.h"

#include "app_shell.h"

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
#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
#include "diagnostics/diagnostics_integration.h"
#endif

namespace vedit::workspace {

void installPanels(vedit::app::ApplicationShell &shell, const IntegrationOptions &options)
{
    (void)shell;
    (void)options;

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
#if defined(VEDIT_WORKSPACE_HAVE_DIAGNOSTICS)
    installDiagnostics(shell, options);
#endif
}

} // namespace vedit::workspace
