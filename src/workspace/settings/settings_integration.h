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

} // namespace vedit::workspace
