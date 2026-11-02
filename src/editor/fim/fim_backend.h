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
// T106: the seam has two shapes.
//   * `complete()` is the blocking convenience call (the lane selftest uses it);
//     it waits on the job with a 15 s budget and therefore must NOT be called
//     from the GUI thread by the product wiring.
//   * `start()` / `poll()` / `cancel()` are the asynchronous shape the editor
//     binding uses: `start` only creates the Rust job (it returns immediately),
//     and the caller polls from a QTimer. A request token 0 means "no request".
#include <QString>
#include <QtGlobal>

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

/// One poll of an asynchronous request.
struct AsyncReply {
    enum class State {
        Pending, ///< still running; poll again later
        Text,    ///< a completion text was returned
        Disabled,///< not enabled / not configured (no request was made)
        Error,   ///< explicit failure (short, key-free sentence)
        Stale,   ///< the token does not belong to a live request (cancelled/finished)
    };
    State state = State::Pending;
    QString text;
};

/// One completion source. The injected seam is what makes the state machine
/// headless-testable without the Rust service; production uses the adapter below.
class Backend {
public:
    virtual ~Backend() = default;

    /// Blocking convenience call (lane selftest / legacy). Waits up to the
    /// backend's budget; never call it on the GUI thread in the product.
    virtual Reply complete(const QString &prefix, const QString &suffix) = 0;

    /// Starts an asynchronous request and returns its token (0 = rejected).
    /// The default implementation reports "unsupported" so a fake backend that
    /// only implements complete() keeps working (the state machine then runs the
    /// blocking call immediately).
    virtual quint64 start(const QString &prefix, const QString &suffix, QString *error)
    {
        Q_UNUSED(prefix);
        Q_UNUSED(suffix);
        if (error != nullptr) {
            *error = QStringLiteral("this backend has no asynchronous seam");
        }
        return 0;
    }

    /// True when start()/poll() are real asynchronous operations. A fake backend
    /// that only implements complete() returns false and the state machine runs
    /// the blocking call immediately inside requestJob() (test-only behaviour).
    virtual bool supportsAsync() const { return false; }

    /// One poll of an asynchronous request. Must not block.
    virtual AsyncReply poll(quint64 request)
    {
        Q_UNUSED(request);
        return AsyncReply{AsyncReply::State::Stale, QString()};
    }

    /// Releases a pending request (safe after it finished, and safe twice).
    virtual void cancel(quint64 request) { Q_UNUSED(request); }
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
