// Compile-only guard: a REAL Qt header plus the generated C ABI header.
//
// `qobjectdefs.h` defines `signals` as a macro (`#define signals public`), so any
// field or parameter in vd_ffi.h named `signals` (or `slots`, `emit`, …) breaks
// every Qt translation unit that includes it. This TU is compiled with
// `-fsyntax-only`; a collision fails here with a clear message instead of taking
// down the editor lane.
#include <QObject>
#include <QString>

#include "vd_ffi.h"

int main()
{
    VdService *service = nullptr;
    VdHierarchy hierarchy{};
    hierarchy.signal_list.count = 0;
    hierarchy.instances.count = 0;
    VdHierSignal signal{};
    signal.drivers.count = 0;
    signal.loads.count = 0;
    signal.chain.count = 0;
    VdHierInstance instance{};
    instance.line = 0;
    VdDiagnostic diagnostic{};
    diagnostic.line = 0;
    VdLspCapabilities capabilities{};
    capabilities.count = 0;
    (void)service;
    (void)hierarchy;
    (void)signal;
    (void)instance;
    (void)diagnostic;
    (void)capabilities;
    return 0;
}
