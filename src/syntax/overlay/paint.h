#pragma once

// Single-owner paint plan (A1g / C16).
//
// The editor paints two colour sources: KSyntaxHighlighting base spans and the
// tree-sitter overlay spans. `buildPaintPlan()` turns them into ONE segment
// list in which every byte carries exactly one engine:
//
//   * an overlay span wins over a base span on the same bytes (the base span is
//     clipped, never painted underneath);
//   * bytes covered by neither engine stay None (the editor draws plain text).
//
// The plan is a partition of the requested range, so "no double colouring" is
// structural; `raw_overlap_bytes` counts the bytes the two raw inputs disagreed
// on, which proves the clipping path was actually exercised by the test data.
#include "syntax_provider.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace vedit::syntax::overlay {

// One span the base highlighter would paint; `style` is an opaque token the
// editor maps to a colour (0 = plain).
struct BaseSpan {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    std::uint32_t style = 1;
};

enum class Engine { None, Base, Overlay };

const char *engineName(Engine engine);

struct PaintSegment {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    Engine engine = Engine::None;
    std::uint32_t style = 0;
};

struct PaintPlan {
    std::vector<PaintSegment> segments;
    std::uint64_t base_bytes = 0;
    std::uint64_t overlay_bytes = 0;
    std::uint64_t none_bytes = 0;
    // bytes covered by BOTH raw inputs, i.e. exactly what the clipping removed
    std::uint64_t raw_overlap_bytes = 0;
    bool contiguous = false;  // segments partition [start,end) with no gap
    bool single_engine = false;  // no segment carries two engines (structural)
};

// Builds the single-owner plan for [start,end) from the two raw inputs.
PaintPlan buildPaintPlan(const std::vector<BaseSpan> &base,
                         const std::vector<HighlightSpan> &overlay, std::uint32_t start,
                         std::uint32_t end);

// True iff `left` and `right` overlap on at least one byte.
bool rangesOverlap(std::uint32_t left_start, std::uint32_t left_end, std::uint32_t right_start,
                   std::uint32_t right_end);

// Short, paste-friendly summary ("segments=412 base=... overlay=... overlap=...").
std::string describePaintPlan(const PaintPlan &plan);

} // namespace vedit::syntax::overlay
