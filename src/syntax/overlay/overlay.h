#pragma once

// src/syn-overlay (T18) — tree-sitter colouring overlay on top of the
// KSyntaxHighlighting base highlighter.
//
// Scope (see the frozen contract .mpd/cellwork/.../contracts/ts-overlay.md and
// requirements C12/C15/C16, A1g/A1h):
//   * single owner per range: every overlay claim goes through OwnerArbiter, so
//     the base highlighter and tree-sitter can never colour the same bytes
//     (A1g / C16). paint.h turns the arbiter state into the clipping plan the
//     editor paints from;
//   * runtime fallback (A1h / C15): setEnabled(false), a disabled
//     SyntaxService or a missing provider releases every claim and leaves the
//     base highlighter as the only owner — no crash, at any point in time;
//   * incremental overlay (C12): the caller injects a visible byte window
//     instead of the whole document, and one edit re-injects only that window.
#include "owner_arbiter.h"
#include "syntax_provider.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vedit::syntax::overlay {

// Why the overlay is (not) colouring right now. `Active` is the only state in
// which it owns ranges; every other state means "the base highlighter owns
// everything".
enum class State { Disabled, NoProvider, Active };

struct Timing {
    long long rebuild_us = 0;    // applyEdit: text + provider sync + span refresh
    long long injection_us = 0;  // claim/release pass for one visible window
    long long text_us = 0;       // part of rebuild_us: local text edit
    long long sync_us = 0;       // part of rebuild_us: provider applyEdit/re-parse
    long long query_us = 0;      // part of rebuild_us: highlightSpans (the query)
};

class OverlayController final {
public:
    explicit OverlayController(std::string language_id, std::string overlay_id = "tree-sitter");
    ~OverlayController();

    OverlayController(const OverlayController &) = delete;
    OverlayController &operator=(const OverlayController &) = delete;

    // Parses `text` through the active SyntaxService provider and resets every
    // claim. Returns false (and fills `error`) when there is no provider; the
    // controller then stays usable in NoProvider state (base highlighter only).
    bool open(std::string_view text, std::string *error = nullptr);
    void close();

    bool opened() const { return opened_; }
    std::string_view text() const { return text_; }
    State state() const;
    std::string stateName() const;
    std::string lastReason() const { return reason_; }

    // Runtime switch (A1h / C15). Safe at any time: it releases the claims when
    // switching off and (re)creates the provider when switching on. Never throws
    // and never leaves a dangling claim.
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // C12: one incremental edit. The edit is applied to the controller's text
    // and to the provider, the cached spans are refreshed, and only the visible
    // window [window_start, window_end) is re-injected. Returns the number of
    // claims inside the window afterwards.
    std::size_t applyEdit(const Edit &edit, std::uint32_t window_start, std::uint32_t window_end);

    // C12: the overlay owns exactly the spans intersecting [start, end). The
    // previous claim set is replaced (scrolling does not accumulate claims), so
    // the overlay only ever owns what is visible right now.
    std::size_t injectWindow(std::uint32_t start, std::uint32_t end);

    // A1h: drops every claim for this overlay id (the bytes fall back to base).
    void releaseAll();

    // Single-owner view for [start, end) (A1g judgement lives in the arbiter).
    std::vector<OwnerSpan> ownerSpans(std::uint32_t start, std::uint32_t end) const;
    bool assertSingleOwner(std::uint32_t start, std::uint32_t end, std::string *detail = nullptr) const;

    // The span cache for the last queried byte range. Right after open() (and
    // after a re-enable) that range is the whole document; after an edit or a
    // scroll it is the visible window (T92: a rebuild must not re-query the
    // whole tree). Owner claims never depend on the cache size: injectWindow()
    // re-queries when the window moved outside it.
    const std::vector<HighlightSpan> &spans() const { return spans_; }
    std::size_t claimCount() const { return claims_.size(); }
    std::size_t lastWindowClaims() const { return last_window_claims_; }
    std::size_t lastWindowBytes() const { return last_window_bytes_; }
    const Timing &lastTiming() const { return timing_; }

    const std::string &languageId() const { return language_id_; }
    const std::string &overlayId() const { return overlay_id_; }

private:
    struct Claim {
        std::uint32_t start = 0;
        std::uint32_t end = 0;
    };

    bool ensureProvider(std::string *error);
    // T92: query only the byte range the caller needs. `open()`/`setEnabled()`
    // still cache the whole document (so `spans()` keeps its full meaning);
    // every edit refreshes the visible window only, and `injectWindow()` asks
    // for the new window when scrolling left the cached range.
    void refreshSpans(std::uint32_t start_byte, std::uint32_t end_byte);
    bool spansCover(std::uint32_t start_byte, std::uint32_t end_byte) const;
    bool applyEditToText(const Edit &edit);

    std::string language_id_;
    std::string overlay_id_;
    std::unique_ptr<SyntaxProvider> provider_;
    std::string text_;
    std::vector<HighlightSpan> spans_;
    std::uint32_t spans_begin_ = 0;
    std::uint32_t spans_end_ = 0;
    bool spans_valid_ = false;
    std::vector<Claim> claims_;
    bool opened_ = false;
    bool enabled_ = true;
    bool base_enabled_ = true;
    std::string reason_;
    Timing timing_;
    std::size_t last_window_claims_ = 0;
    std::size_t last_window_bytes_ = 0;
};

} // namespace vedit::syntax::overlay
