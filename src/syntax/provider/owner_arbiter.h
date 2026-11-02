#pragma once

// C16 colouring-ownership arbiter.
//
// Policy (frozen in POLICY.md): for one language, every byte range has at most
// one colouring owner at any moment. The base highlighter (KSyntaxHighlighting)
// is the implicit owner of all ranges while base highlighting is enabled; an
// overlay (tree-sitter) may claim a range, which transfers ownership of exactly
// that range to the overlay. Two overlays may never own overlapping ranges, and
// an overlay claim must be released before the same bytes change owner again.
//
// The editor and the overlay layer must consult ownerSpans()/ownerAt() before
// painting; assertSingleOwner() is the queryable A1g judgement (and the
// invariant the unit tests hammer).
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vedit::syntax {

enum class OwnerKind { None, Base, Overlay };

struct OwnerSpan {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    OwnerKind kind = OwnerKind::None;
    std::string owner_id; // base owner id for Base, overlay id for Overlay
};

enum class ClaimResult { Granted, Conflict, InvalidRange };

struct ClaimOutcome {
    ClaimResult result = ClaimResult::InvalidRange;
    std::string message;
    OwnerSpan conflicting; // valid when result == Conflict
};

class OwnerArbiter {
public:
    static OwnerArbiter &instance();
    static const char *baseOwnerId();

    // Base (KSyntaxHighlighting) ownership switch per language. Enabled by
    // default for every language the base highlighter knows.
    void setBaseEnabled(std::string_view language_id, bool enabled);
    bool baseEnabled(std::string_view language_id) const;

    // Overlay claim of [start_byte, end_byte). Overlapping claims from another
    // overlay are rejected with the existing owner reported.
    ClaimOutcome claim(std::string_view language_id, std::string_view overlay_id,
                       std::uint32_t start_byte, std::uint32_t end_byte);

    // Releases the given overlay's ownership inside [start,end); bytes fall back
    // to the base owner (or None when base highlighting is disabled).
    bool release(std::string_view language_id, std::string_view overlay_id,
                 std::uint32_t start_byte, std::uint32_t end_byte);
    bool releaseAll(std::string_view language_id, std::string_view overlay_id);

    // Segment view of [start,end): contiguous, non-overlapping, covers exactly
    // the requested interval. Each byte has exactly one owner kind.
    std::vector<OwnerSpan> ownerSpans(std::string_view language_id, std::uint32_t start_byte,
                                      std::uint32_t end_byte) const;
    OwnerSpan ownerAt(std::string_view language_id, std::uint32_t byte_offset) const;

    // A1g judgement: true iff ownerSpans() partitions [start,end) exactly (no
    // gap, no overlap, every span non-empty). `detail` receives a short summary.
    bool assertSingleOwner(std::string_view language_id, std::uint32_t start_byte,
                           std::uint32_t end_byte, std::string *detail = nullptr) const;

    void clear();

private:
    struct Interval {
        std::uint32_t start = 0;
        std::uint32_t end = 0;
        std::string overlay_id;
    };
    const std::vector<Interval> *intervalsFor(std::string_view language_id) const;
    std::vector<Interval> *intervalsFor(std::string_view language_id);

    std::map<std::string, bool> base_enabled_;
    std::map<std::string, std::vector<Interval>> overlays_;
};

} // namespace vedit::syntax
