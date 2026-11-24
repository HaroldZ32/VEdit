#pragma once

// Settings lane entry point (Slice 3): registers the "settings" panel factory
// with the application shell. Same convention as the other workspace lanes:
// `install<Lane>(ApplicationShell &, const IntegrationOptions &)` and the shell
// keeps learning nothing about lane names.
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installSettings(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

/// T63 (batch 20261110c, AC-4.7): load `<stateDir>/environment.json` — the
/// selection the Preferences → Environment page stores beside `settings.toml` —
/// and hand the merged layer to the one seam every tool child passes
/// (`vd_env_apply_selection` → `sanitize_tool_child_env`).
///
/// It MUST run before any lane can spawn a child (lint / language server / VCS /
/// terminal), which is why `installPanels()` calls it FIRST and not from
/// `installSettings()` (that runs after every other lane). A missing file is the
/// empty selection (no warning); a broken file is the empty selection plus one
/// warning, and a refusal is reported too — this startup step is never silent.
void applyEnvironmentSelection(const IntegrationOptions &options);

} // namespace vedit::workspace
