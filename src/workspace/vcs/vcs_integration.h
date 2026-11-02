#pragma once

// App-facing adapter of the read-only VCS panel (T23/T95).
//
// The lane core (vcs_source / vcs_panel) deliberately does not depend on
// src/app: this is the only file that includes the application shell. It
// registers the panel factory for the shell's "vcs" dock; the panel itself only
// exposes read-only queries (info/status/diff), matching the Rust backend's
// allow-list.
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installVcs(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
