#include "owner_arbiter.h"

#include <algorithm>
#include <string>

namespace vedit::syntax {

OwnerArbiter &OwnerArbiter::instance()
{
    static OwnerArbiter arbiter;
    return arbiter;
}

const char *OwnerArbiter::baseOwnerId()
{
    return "ksyntaxhighlighting";
}

void OwnerArbiter::setBaseEnabled(std::string_view language_id, bool enabled)
{
    base_enabled_[std::string(language_id)] = enabled;
}

bool OwnerArbiter::baseEnabled(std::string_view language_id) const
{
    const auto it = base_enabled_.find(std::string(language_id));
    return it == base_enabled_.end() ? true : it->second;
}

const std::vector<OwnerArbiter::Interval> *OwnerArbiter::intervalsFor(std::string_view language_id) const
{
    const auto it = overlays_.find(std::string(language_id));
    return it == overlays_.end() ? nullptr : &it->second;
}

std::vector<OwnerArbiter::Interval> *OwnerArbiter::intervalsFor(std::string_view language_id)
{
    const auto it = overlays_.find(std::string(language_id));
    return it == overlays_.end() ? nullptr : &it->second;
}

ClaimOutcome OwnerArbiter::claim(std::string_view language_id, std::string_view overlay_id,
                                 std::uint32_t start_byte, std::uint32_t end_byte)
{
    ClaimOutcome outcome;
    if (start_byte >= end_byte || overlay_id.empty()) {
        outcome.result = ClaimResult::InvalidRange;
        outcome.message = "empty or inverted range, or empty overlay id";
        return outcome;
    }
    auto &intervals = overlays_[std::string(language_id)];
    for (const Interval &interval : intervals) {
        if (start_byte < interval.end && interval.start < end_byte) {
            outcome.result = ClaimResult::Conflict;
            outcome.conflicting = OwnerSpan{interval.start, interval.end, OwnerKind::Overlay,
                                            interval.overlay_id};
            outcome.message = "range already owned by overlay '" + interval.overlay_id + "'";
            return outcome;
        }
    }
    intervals.push_back(Interval{start_byte, end_byte, std::string(overlay_id)});
    std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) {
        return a.start != b.start ? a.start < b.start : a.end < b.end;
    });
    // Merge adjacent intervals of the same overlay so the segment view stays small.
    std::vector<Interval> merged;
    merged.reserve(intervals.size());
    for (const Interval &interval : intervals) {
        if (!merged.empty() && merged.back().overlay_id == interval.overlay_id &&
            merged.back().end == interval.start) {
            merged.back().end = interval.end;
            continue;
        }
        merged.push_back(interval);
    }
    intervals.swap(merged);
    outcome.result = ClaimResult::Granted;
    outcome.message = "granted";
    return outcome;
}

bool OwnerArbiter::release(std::string_view language_id, std::string_view overlay_id,
                           std::uint32_t start_byte, std::uint32_t end_byte)
{
    auto *intervals = intervalsFor(language_id);
    if (intervals == nullptr || start_byte >= end_byte) {
        return false;
    }
    std::vector<Interval> result;
    bool released = false;
    for (const Interval &interval : *intervals) {
        if (interval.overlay_id != overlay_id || start_byte >= interval.end ||
            interval.start >= end_byte) {
            result.push_back(interval);
            continue;
        }
        released = true;
        if (interval.start < start_byte) {
            result.push_back(Interval{interval.start, start_byte, interval.overlay_id});
        }
        if (end_byte < interval.end) {
            result.push_back(Interval{end_byte, interval.end, interval.overlay_id});
        }
    }
    intervals->swap(result);
    return released;
}

bool OwnerArbiter::releaseAll(std::string_view language_id, std::string_view overlay_id)
{
    auto *intervals = intervalsFor(language_id);
    if (intervals == nullptr) {
        return false;
    }
    const std::size_t before = intervals->size();
    intervals->erase(std::remove_if(intervals->begin(), intervals->end(),
                                    [overlay_id](const Interval &interval) {
                                        return interval.overlay_id == std::string(overlay_id);
                                    }),
                     intervals->end());
    return intervals->size() != before;
}

std::vector<OwnerSpan> OwnerArbiter::ownerSpans(std::string_view language_id,
                                                std::uint32_t start_byte,
                                                std::uint32_t end_byte) const
{
    std::vector<OwnerSpan> spans;
    if (start_byte >= end_byte) {
        return spans;
    }
    const bool base = baseEnabled(language_id);
    const auto makeSpan = [base](std::uint32_t start, std::uint32_t end) {
        OwnerSpan span{start, end, base ? OwnerKind::Base : OwnerKind::None,
                       base ? std::string(baseOwnerId()) : std::string()};
        return span;
    };

    std::uint32_t cursor = start_byte;
    const auto *intervals = intervalsFor(language_id);
    if (intervals != nullptr) {
        for (const Interval &interval : *intervals) {
            const std::uint32_t overlap_start = std::max(start_byte, interval.start);
            const std::uint32_t overlap_end = std::min(end_byte, interval.end);
            if (overlap_start >= overlap_end) {
                continue;
            }
            if (overlap_start > cursor) {
                spans.push_back(makeSpan(cursor, overlap_start));
            }
            spans.push_back(OwnerSpan{overlap_start, overlap_end, OwnerKind::Overlay,
                                      interval.overlay_id});
            cursor = overlap_end;
        }
    }
    if (cursor < end_byte) {
        spans.push_back(makeSpan(cursor, end_byte));
    }
    return spans;
}

OwnerSpan OwnerArbiter::ownerAt(std::string_view language_id, std::uint32_t byte_offset) const
{
    const auto spans = ownerSpans(language_id, byte_offset, byte_offset + 1);
    if (!spans.empty()) {
        return spans.front();
    }
    OwnerSpan none;
    none.start_byte = byte_offset;
    none.end_byte = byte_offset;
    return none;
}

bool OwnerArbiter::assertSingleOwner(std::string_view language_id, std::uint32_t start_byte,
                                     std::uint32_t end_byte, std::string *detail) const
{
    const auto spans = ownerSpans(language_id, start_byte, end_byte);
    bool ok = true;
    std::string reason;
    if (spans.empty()) {
        ok = false;
        reason = "no owner spans";
    } else if (spans.front().start_byte != start_byte) {
        ok = false;
        reason = "leading gap";
    } else if (spans.back().end_byte != end_byte) {
        ok = false;
        reason = "trailing gap";
    } else {
        for (std::size_t i = 0; i < spans.size(); ++i) {
            const OwnerSpan &span = spans[i];
            if (span.start_byte >= span.end_byte) {
                ok = false;
                reason = "empty span";
                break;
            }
            if (i + 1 < spans.size() && spans[i + 1].start_byte != span.end_byte) {
                ok = false;
                reason = "gap or overlap between spans";
                break;
            }
        }
    }
    if (detail != nullptr) {
        *detail = ok ? "single owner across " + std::to_string(spans.size()) + " span(s)"
                     : "violation: " + reason;
    }
    return ok;
}

void OwnerArbiter::clear()
{
    base_enabled_.clear();
    overlays_.clear();
}

} // namespace vedit::syntax
