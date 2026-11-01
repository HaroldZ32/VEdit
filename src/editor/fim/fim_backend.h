#pragma once

// FIM completion seam of the editor lane (task T27 / AC-S3.2 remainder).
//
// The Qt side NEVER performs HTTP for FIM. The production adapter turns
// `(prefix, suffix)` into the C ABI call pair `vd_fim_complete` / `vd_fim_poll`
// and the request itself is made by the Rust service layer (`crates/vd-fim` via
// `crates/vd-ffi`, ureq). With the product default (FIM disabled, nothing
// configured) the Rust side starts no worker and opens no socket, so the adapter
// returns `Reply::State::Disabled` with the reason and touches no network.
//
// This lane is deliberately NOT wired into `src/editor/kernel` yet: the kernel
// hook (a completion provider on the KTextEditor view) belongs to a separate
// task; `README.md` names the exact hook point.
#include <QString>

#include <memory>

namespace vedit::editor::fim {

struct Reply {
    enum class State {
        Disabled, ///< not enabled / not configured: no request was made
        Text,     ///< a completion text was returned
        Error,    ///< an explicit failure (short, key-free sentence)
    };
    State state = State::Disabled;
    QString text; ///< completion text, or the short status/error sentence
};

/// One completion source. The injected seam is what makes the state machine
/// headless-testable without the Rust service; production uses the adapter below.
class Backend {
public:
    virtual ~Backend() = default;
    virtual Reply complete(const QString &prefix, const QString &suffix) = 0;
};

/// Production adapter over the C ABI (`vd_fim_complete` -> `vd_fim_poll`).
/// Null when this build has no Rust service layer (the lane CMake reports it).
std::unique_ptr<Backend> makeRustBackend();

/// "Rust service (vd_fim_complete -> vd-fim)" or "unavailable (no Rust service)".
QString backendDescription();

/// The FIM prefix/suffix around a zero-based cursor position: `prefix` is the
/// text before the cursor, `suffix` the text after it. Out-of-range lines and
/// columns are clamped, so a stale cursor can never throw.
struct Context {
    QString prefix;
    QString suffix;
};

Context contextAt(const QString &text, int zeroBasedLine, int zeroBasedColumn);

} // namespace vedit::editor::fim
