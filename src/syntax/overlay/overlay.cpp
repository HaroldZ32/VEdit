#include "overlay.h"

#include "syntax_service.h"

#include <algorithm>
#include <chrono>

namespace vedit::syntax::overlay {

namespace {

using Clock = std::chrono::steady_clock;

long long microsSince(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

long long microsBetween(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
}

bool overlaps(std::uint32_t start, std::uint32_t end, std::uint32_t other_start,
              std::uint32_t other_end)
{
    return start < other_end && other_start < end;
}

} // namespace

OverlayController::OverlayController(std::string language_id, std::string overlay_id)
    : language_id_(std::move(language_id))
    , overlay_id_(std::move(overlay_id))
{
    // The overlay lane is the base highlighter's partner, not its replacement:
    // the implicit base owner must be on for the fallback (A1h) to mean anything.
    OwnerArbiter &arbiter = OwnerArbiter::instance();
    base_enabled_ = arbiter.baseEnabled(language_id_);
    if (!base_enabled_) {
        arbiter.setBaseEnabled(language_id_, true);
        base_enabled_ = true;
    }
}

OverlayController::~OverlayController()
{
    releaseAll();
}

bool OverlayController::ensureProvider(std::string *error)
{
    SyntaxService &service = SyntaxService::instance();
    if (!enabled_) {
        reason_ = "disabled at runtime (A1h): the base highlighter owns every range";
        if (error != nullptr) {
            *error = reason_;
        }
        return false;
    }
    if (!service.treeSitterEnabled()) {
        reason_ = "SyntaxService tree-sitter switch is off (A1h): base highlighter only";
        if (error != nullptr) {
            *error = reason_;
        }
        return false;
    }
    if (provider_ != nullptr) {
        return true;
    }
    if (!service.available()) {
        reason_ = "no active grammar/provider; the base highlighter keeps colouring";
        if (error != nullptr) {
            *error = reason_;
        }
        return false;
    }
    provider_ = service.createProvider();
    if (provider_ == nullptr) {
        reason_ = "createProvider() returned null; the base highlighter keeps colouring";
        if (error != nullptr) {
            *error = reason_;
        }
        return false;
    }
    return true;
}

bool OverlayController::open(std::string_view text, std::string *error)
{
    releaseAll();
    provider_.reset();
    spans_.clear();
    text_.assign(text);
    opened_ = true;
    reason_.clear();

    if (!ensureProvider(error)) {
        return false;
    }
    const ParseTreePtr tree = provider_->parse(text_);
    if (!tree) {
        reason_ = "parse returned a null tree; the base highlighter keeps colouring";
        if (error != nullptr) {
            *error = reason_;
        }
        return false;
    }
    // T92: open() caches the whole document (the public spans() view keeps its
    // full meaning); edits and scrolling switch the cache to the visible window.
    refreshSpans(0, static_cast<std::uint32_t>(text_.size()));
    return true;
}

void OverlayController::close()
{
    releaseAll();
    provider_.reset();
    spans_.clear();
    spans_valid_ = false;
    text_.clear();
    opened_ = false;
    reason_.clear();
}

State OverlayController::state() const
{
    if (!enabled_) {
        return State::Disabled;
    }
    if (provider_ == nullptr) {
        return State::NoProvider;
    }
    return State::Active;
}

std::string OverlayController::stateName() const
{
    switch (state()) {
    case State::Active:
        return "Active";
    case State::NoProvider:
        return "NoProvider";
    case State::Disabled:
        return "Disabled";
    }
    return "Unknown";
}

void OverlayController::setEnabled(bool enabled)
{
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_) {
        // A1h: the switch is an invariant, not a hint. Releasing here (and not
        // only in the next paint) is what makes "no dangling claim" true, even
        // for a frame that was already in flight.
        releaseAll();
        reason_ = "disabled at runtime (A1h): the base highlighter owns every range";
        return;
    }
    reason_.clear();
    if (!opened_) {
        return;
    }
    std::string error;
    if (ensureProvider(&error)) {
        if (!provider_->tree()) {
            provider_->parse(text_);
        }
        // Re-enabling restores the full-document cache the caller can inspect.
        refreshSpans(0, static_cast<std::uint32_t>(text_.size()));
    }
}

void OverlayController::refreshSpans(std::uint32_t start_byte, std::uint32_t end_byte)
{
    spans_.clear();
    spans_valid_ = false;
    if (provider_ == nullptr) {
        return;
    }
    const ParseTreePtr tree = provider_->tree();
    if (!tree) {
        return;
    }
    // T92: query exactly the requested byte range (the overlay only ever claims
    // that range), so the cost tracks the window instead of the document.
    const std::uint32_t size = static_cast<std::uint32_t>(text_.size());
    const std::uint32_t start = std::min(start_byte, size);
    const std::uint32_t end = std::max(start, std::min(end_byte, size));
    spans_ = provider_->highlightSpans(tree, Range{start, end, 0, 0});
    spans_begin_ = start;
    spans_end_ = end;
    spans_valid_ = true;
    // The provider walks the tree in order, so the usual case is already sorted;
    // the check is O(n) and saves the sort on every rebuild.
    const bool sorted = std::is_sorted(spans_.begin(), spans_.end(),
                                       [](const HighlightSpan &left, const HighlightSpan &right) {
                                           return left.range.start_byte < right.range.start_byte;
                                       });
    if (!sorted) {
        std::sort(spans_.begin(), spans_.end(), [](const HighlightSpan &left, const HighlightSpan &right) {
            if (left.range.start_byte != right.range.start_byte) {
                return left.range.start_byte < right.range.start_byte;
            }
            return left.range.end_byte < right.range.end_byte;
        });
    }
}

bool OverlayController::spansCover(std::uint32_t start_byte, std::uint32_t end_byte) const
{
    return spans_valid_ && start_byte >= spans_begin_ && end_byte <= spans_end_;
}

bool OverlayController::applyEditToText(const Edit &edit)
{
    if (edit.start_byte > edit.old_end_byte || edit.old_end_byte > text_.size()) {
        return false;
    }
    text_.replace(edit.start_byte, edit.old_end_byte - edit.start_byte, edit.new_text);
    return true;
}

std::size_t OverlayController::applyEdit(const Edit &edit, std::uint32_t window_start,
                                         std::uint32_t window_end)
{
    const Clock::time_point started = Clock::now();
    last_window_claims_ = 0;
    last_window_bytes_ = 0;
    timing_.injection_us = 0;
    if (!opened_) {
        timing_.rebuild_us = microsSince(started);
        return 0;
    }

    // 1. the controller's own copy of the document (the caller applies the same
    //    edit to its buffer; the provider gets it below).
    if (!applyEditToText(edit)) {
        reason_ = "edit outside the document; nothing was changed";
        timing_.rebuild_us = microsSince(started);
        return 0;
    }
    const Clock::time_point text_done = Clock::now();

    // 2. incremental provider sync. A provider that cannot stay in sync (false)
    //    is re-parsed from the controller's text: never a stale tree, never a
    //    crash when a grammar gives up mid-edit.
    if (provider_ != nullptr) {
        if (!provider_->applyEdit(edit)) {
            provider_->parse(text_);
        }
    }
    const Clock::time_point sync_done = Clock::now();

    // 3. rebuild: the tree changed, so the span cache for the VISIBLE window is
    //    refreshed (T92: not the whole document). The old claims are dropped
    //    first (cheap: one release per overlay id) and step 4 re-claims the
    //    window from the fresh cache.
    refreshSpans(window_start, window_end);
    const Clock::time_point query_done = Clock::now();
    releaseAll();
    timing_.rebuild_us = microsSince(started);
    timing_.text_us = microsBetween(started, text_done);
    timing_.sync_us = microsBetween(text_done, sync_done);
    timing_.query_us = microsBetween(sync_done, query_done);

    // 4. incremental injection: only the visible window is claimed.
    return injectWindow(window_start, window_end);
}

std::size_t OverlayController::injectWindow(std::uint32_t start, std::uint32_t end)
{
    const Clock::time_point started = Clock::now();
    last_window_claims_ = 0;
    last_window_bytes_ = 0;
    if (!enabled_ || provider_ == nullptr || start >= end) {
        timing_.injection_us = microsSince(started);
        return 0;
    }

    // The overlay owns exactly ONE visible window: scrolling replaces the claim
    // set instead of accumulating it (bounded work per frame, and a range that
    // scrolled out of view falls back to the base highlighter immediately).
    // T92: if the new window is outside the cached span range (scrolling after
    // an edit cached only the previous window), query that range now; the query
    // cost stays proportional to the window, not to the document.
    const std::uint32_t size = static_cast<std::uint32_t>(text_.size());
    const std::uint32_t clamped_end = std::max(start, std::min(end, size));
    if (!spansCover(start, clamped_end)) {
        refreshSpans(start, clamped_end);
    }
    OwnerArbiter &arbiter = OwnerArbiter::instance();
    releaseAll();

    std::size_t claimed = 0;
    std::uint64_t bytes = 0;
    for (const HighlightSpan &span : spans_) {
        if (span.range.start_byte >= end) {
            break; // spans_ is sorted by start_byte
        }
        if (!overlaps(span.range.start_byte, span.range.end_byte, start, end)) {
            continue;
        }
        const ClaimOutcome outcome = arbiter.claim(language_id_, overlay_id_, span.range.start_byte,
                                                   span.range.end_byte);
        if (outcome.result == ClaimResult::Granted) {
            claims_.push_back(Claim{span.range.start_byte, span.range.end_byte});
            bytes += span.range.end_byte - span.range.start_byte;
            ++claimed;
        } else if (outcome.result == ClaimResult::Conflict) {
            reason_ = "claim conflict: " + outcome.message;
        }
    }
    last_window_claims_ = claimed;
    last_window_bytes_ = static_cast<std::size_t>(bytes);
    timing_.injection_us = microsSince(started);
    return claimed;
}

void OverlayController::releaseAll()
{
    // releaseAll() is safe on an empty state and on an unknown language id, so
    // this runs unconditionally: a claim orphaned by an exception-free early
    // return can never survive a disable/close.
    OwnerArbiter::instance().releaseAll(language_id_, overlay_id_);
    claims_.clear();
}

std::vector<OwnerSpan> OverlayController::ownerSpans(std::uint32_t start, std::uint32_t end) const
{
    return OwnerArbiter::instance().ownerSpans(language_id_, start, end);
}

bool OverlayController::assertSingleOwner(std::uint32_t start, std::uint32_t end,
                                          std::string *detail) const
{
    return OwnerArbiter::instance().assertSingleOwner(language_id_, start, end, detail);
}

} // namespace vedit::syntax::overlay
