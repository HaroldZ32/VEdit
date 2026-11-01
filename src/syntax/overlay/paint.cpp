#include "paint.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace vedit::syntax::overlay {

const char *engineName(Engine engine)
{
    switch (engine) {
    case Engine::Base:
        return "base";
    case Engine::Overlay:
        return "overlay";
    case Engine::None:
        return "none";
    }
    return "unknown";
}

bool rangesOverlap(std::uint32_t left_start, std::uint32_t left_end, std::uint32_t right_start,
                   std::uint32_t right_end)
{
    return left_start < right_end && right_start < left_end;
}

namespace {

struct CutSpan {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t style = 0;
};

} // namespace

PaintPlan buildPaintPlan(const std::vector<BaseSpan> &base, const std::vector<HighlightSpan> &overlay,
                         std::uint32_t start, std::uint32_t end)
{
    PaintPlan plan;
    if (start >= end) {
        return plan;
    }

    // Clip both inputs to the requested range and remember every boundary: the
    // plan is the partition induced by them, so no segment can straddle an
    // engine change.
    std::vector<CutSpan> base_clipped;
    for (const BaseSpan &span : base) {
        if (!rangesOverlap(span.start_byte, span.end_byte, start, end)) {
            continue;
        }
        base_clipped.push_back(CutSpan{std::max(span.start_byte, start),
                                       std::min(span.end_byte, end), span.style});
    }
    std::vector<CutSpan> overlay_clipped;
    for (const HighlightSpan &span : overlay) {
        if (!rangesOverlap(span.range.start_byte, span.range.end_byte, start, end)) {
            continue;
        }
        overlay_clipped.push_back(CutSpan{std::max(span.range.start_byte, start),
                                          std::min(span.range.end_byte, end), 0});
    }
    std::sort(base_clipped.begin(), base_clipped.end(),
              [](const CutSpan &left, const CutSpan &right) { return left.start < right.start; });
    std::sort(overlay_clipped.begin(), overlay_clipped.end(),
              [](const CutSpan &left, const CutSpan &right) { return left.start < right.start; });

    std::vector<std::uint32_t> cuts;
    cuts.reserve(base_clipped.size() * 2 + overlay_clipped.size() * 2 + 2);
    cuts.push_back(start);
    cuts.push_back(end);
    for (const CutSpan &span : base_clipped) {
        cuts.push_back(span.start);
        cuts.push_back(span.end);
    }
    for (const CutSpan &span : overlay_clipped) {
        cuts.push_back(span.start);
        cuts.push_back(span.end);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    // Two pointers keep this linear in the number of spans: a curve that matters
    // for a 3k-line file painted every frame.
    std::size_t base_index = 0;
    std::size_t overlay_index = 0;
    for (std::size_t index = 0; index + 1 < cuts.size(); ++index) {
        const std::uint32_t from = cuts[index];
        const std::uint32_t to = cuts[index + 1];
        if (from >= to) {
            continue;
        }
        while (base_index < base_clipped.size() && base_clipped[base_index].end <= from) {
            ++base_index;
        }
        while (overlay_index < overlay_clipped.size() && overlay_clipped[overlay_index].end <= from) {
            ++overlay_index;
        }
        const bool in_base = base_index < base_clipped.size()
            && base_clipped[base_index].start <= from && base_clipped[base_index].end > from;
        const bool in_overlay = overlay_index < overlay_clipped.size()
            && overlay_clipped[overlay_index].start <= from && overlay_clipped[overlay_index].end > from;

        PaintSegment segment;
        segment.start_byte = from;
        segment.end_byte = to;
        if (in_overlay) {
            // C16/A1g: the overlay wins; the base span is clipped, never painted
            // underneath. Every byte therefore has exactly one engine.
            segment.engine = Engine::Overlay;
            segment.style = 0;
            plan.overlay_bytes += to - from;
        } else if (in_base) {
            segment.engine = Engine::Base;
            segment.style = base_clipped[base_index].style;
            plan.base_bytes += to - from;
        } else {
            segment.engine = Engine::None;
            plan.none_bytes += to - from;
        }
        if (in_base && in_overlay) {
            plan.raw_overlap_bytes += to - from;
        }
        plan.segments.push_back(segment);
    }

    plan.contiguous = plan.segments.empty() ? false : plan.segments.front().start_byte == start
        && plan.segments.back().end_byte == end;
    plan.single_engine = true;
    for (const PaintSegment &segment : plan.segments) {
        if (segment.start_byte >= segment.end_byte) {
            plan.single_engine = false;
        }
    }
    return plan;
}

std::string describePaintPlan(const PaintPlan &plan)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "segments=%zu base=%llu overlay=%llu none=%llu raw-overlap=%llu contiguous=%d",
                  plan.segments.size(), static_cast<unsigned long long>(plan.base_bytes),
                  static_cast<unsigned long long>(plan.overlay_bytes),
                  static_cast<unsigned long long>(plan.none_bytes),
                  static_cast<unsigned long long>(plan.raw_overlap_bytes),
                  plan.contiguous ? 1 : 0);
    return std::string(buffer);
}

} // namespace vedit::syntax::overlay
