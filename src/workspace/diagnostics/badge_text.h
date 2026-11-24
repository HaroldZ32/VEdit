#pragma once

// AC-N2 / AC-3.2/3.3/3.4: the badge's status line as a PURE function.
//
// The badge data plane (BadgeState, T9) already knows the request, the effective
// selection and why they differ. Rendering it used to be the wiring's job, and
// the wiring printed only the EFFECTIVE backend - which is exactly how a refused
// switch became invisible once the 8 s status-bar message expired (the reported
// defect this badge exists for). This function is the one renderer: it takes the
// DATA and returns the text a QLabel shows, so the exact string can be asserted
// with no widget, no timer and no event loop.
//
// Shape (fixed, in this order):
//
//   <requested>→<effective> · lint=<engine> · basis=<basis> · reason=<code>
//
//   * the arrow appears ONLY when the pair differs; a pair that is really in
//     force collapses to the single effective token (`verible`);
//   * an axis with no effective value renders `unavailable` (never an empty
//     field, and never a backend that cannot run);
//   * `lint=` names the CLI lint engine and follows the same arrow rule, so a
//     refused engine switch is just as visible as a refused backend;
//   * `reason=ok` when BOTH axes are in force. Otherwise the axis'
//     `BadgeReasonCode` token (`tool-missing`, `unknown-value`, `select-failed`,
//     `no-backend`), two distinct failing reasons joined with `+` in the fixed
//     lsp-then-lint order, so the vocabulary is machine-readable and stable for
//     evidence. An axis that did not take effect but carries no reason code
//     renders `not-applied` (never `ok`, which would claim the request is in
//     force); two axes refused for the same reason are one token, because the
//     arrows already show which axes differ;
//   * `basis` is passed through verbatim; an empty basis is `unknown` (never
//     silently upgraded to a stronger claim).
//
// The human sentence of a refusal stays in `BadgeState.reasonMessage` (the
// widget's tooltip, `diagnosticsBackendStatus`): this line is the always-visible
// status, and its tokens are deliberately the stable ones (`reason=ok` means
// "the request is in force", not "the settings are perfect" - a load warning
// still travels separately in `BadgeState.warnings`). The literal `unavailable`
// is not translated for the same reason: the line mixes machine tokens and must
// compare equal across locales in evidence.

#include "diagnostics_integration.h"

#include <QString>

namespace vedit::workspace::diagnostics {

/// The exact text the badge shows for `badge`. Pure: no widget, no timer, no
/// I/O, and no dependency on `BadgeState::summary()` (that one is the older,
/// human-oriented rendering).
QString badgeText(const vedit::workspace::BadgeState &badge);

} // namespace vedit::workspace::diagnostics
