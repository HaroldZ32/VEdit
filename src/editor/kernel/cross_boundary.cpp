#include "cross_boundary.h"

#include <atomic>

namespace vedit::editor {
namespace {
std::atomic<qint64> g_crossBoundaryCalls{0};
} // namespace

void noteCrossBoundaryCall()
{
    g_crossBoundaryCalls.fetch_add(1, std::memory_order_relaxed);
}

qint64 crossBoundaryCallCount()
{
    return g_crossBoundaryCalls.load(std::memory_order_relaxed);
}

void resetCrossBoundaryCallCount()
{
    g_crossBoundaryCalls.store(0, std::memory_order_relaxed);
}

} // namespace vedit::editor
