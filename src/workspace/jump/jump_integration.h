#pragma once

// Jump lane entry point (workspace-lane convention):
//   `install<Lane>(ApplicationShell &, const IntegrationOptions &)`.
//
// Unlike the panel lanes this installs no widget factory: the jump lane is a
// CONTRACT plus a process-wide registry, not a dock. installJump() only resets
// the registry when a new shell starts, so a target left over from a destroyed
// shell can never be reused by the new one. The real target (open the file and
// position the cursor) is registered by the shell/editor lane through
// `vedit::workspace::jump::setTarget()`; this lane contains no file-opening
// code on purpose (frozen contract: 本任务不实现真正的文件打开).
#include "../integration.h"

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

void installJump(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

} // namespace vedit::workspace
