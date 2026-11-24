#pragma once

// Scala lane entry point (batch 20261110, AC-7.5 / AC-7.10): registers the
// one-click Run command and streams its output into the EXISTING Terminal panel.
// Same convention as every workspace lane: installScala(ApplicationShell &,
// const IntegrationOptions &); the shell never learns the lane's name.
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installScala(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
