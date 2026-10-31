#pragma once

// Cross-boundary call counter (AC-S1.1 / C1: edit transactions must not cross
// the Qt <-> Rust C ABI).
//
// The product increments the counter at every `vd_*` call site (the only place
// the C ABI is reachable from C++ is src/app/main.cpp's `ffi` namespace). The
// edit kernel never touches it and links no `vd_*` symbol at all, so the
// selfcheck can assert a ZERO delta around every editing transaction, with a
// positive control proving the counter is live.
#include <QtGlobal>

namespace vedit::editor {

void noteCrossBoundaryCall();
qint64 crossBoundaryCallCount();
void resetCrossBoundaryCallCount();

} // namespace vedit::editor
