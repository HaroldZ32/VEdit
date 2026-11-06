#pragma once

// Process-wide jump registry (acceptance AC-7.7: "新控件统一实现 IJumpTarget，
// 不各写一套").
//
// ONE target per process, installed by the shell/editor lane; every panel asks
// for a jump through `jumpTo()` and never learns which widget performs it. The
// registry is deliberately process-wide rather than per-shell: the jump seam is
// a free function so a lane does not need an ApplicationShell reference, and it
// never owns the target (the shell owns it).
//
// The no-target case is a supported state, not an error: a panel built without
// the product shell (a lane's standalone build) still paints and still reports
// clicks - `jumpTo()` then returns JumpStatus::NoTarget and nothing happens.
//
// Lifetime: the registry stores a raw pointer. When the target is also a QObject
// (the product target is), the registry weak-guards it: destroying the target
// without clearTarget() drops it, and a later jump reports NoTarget instead of
// dereferencing a dead object. A non-QObject target must be cleared by its owner
// before it dies (clearTarget()/clearTargetIf()).
//
// Threading: jumps are dispatched on the GUI thread. The registry's own state
// (target + counters) is mutex-protected, but a target implementation is not
// required to be thread-safe.
#include "jump_target.h"

#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>

namespace vedit::workspace::jump {

// What one jump did.
enum class JumpStatus {
    NoTarget,        // no target registered: the jump is a no-op (no crash)
    InvalidRequest,  // empty path or a line/column below 1: refused before dispatch
    Delivered,       // the registered target reported success
    Rejected,        // the registered target reported failure
};

// Stable lowercase spelling for logs and the self-check's output.
const char *jumpStatusName(JumpStatus status);

// What the registry saw, including the position it was asked for. A NoTarget /
// InvalidRequest result still carries the request, so a caller can tell "the
// click was well formed but nobody listens yet" from "the request was bad".
struct JumpResult {
    JumpStatus status = JumpStatus::NoTarget;
    QString filePath;
    int line = 0;
    int column = 0;
};

class Registry final {
public:
    static Registry &instance();

    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    // Installs the jump target (non-owning; nullptr clears). Replacing a target
    // is allowed: the shell re-registers after a restart of the editor lane.
    void setTarget(IJumpTarget *target);
    void clearTarget();
    // Clears only when the registered target is `expected` (a target removes
    // itself in its destructor without racing a newer registration).
    void clearTargetIf(IJumpTarget *target);

    bool hasTarget() const;
    // The registered target, or nullptr. A destroyed QObject target reports
    // nullptr (and is dropped from the registry at this point).
    IJumpTarget *target() const;

    // Dispatches one jump. Never throws and never blocks on the target beyond
    // the call itself; NoTarget when nothing is registered.
    JumpResult jumpTo(const QString &filePath, int line, int column);

    // Observability for the self-check / evidence and a future status-bar hint.
    quint64 deliveredCount() const;
    quint64 rejectedCount() const;
    quint64 noTargetCount() const;
    quint64 invalidCount() const;
    JumpResult lastResult() const;

    // Test + install support: drop the target and the counters. installJump()
    // only clears the target; this full reset exists for the self-check.
    void reset();

private:
    Registry() = default;

    // Drops a QObject target whose destructor already ran. Caller holds mutex_.
    IJumpTarget *liveTargetLocked() const;

    mutable QMutex mutex_;
    // `mutable`: liveTargetLocked() is const but lazily drops a destroyed
    // QObject target, which is state the next caller must see.
    mutable IJumpTarget *target_ = nullptr;
    mutable QPointer<QObject> targetGuard_;
    mutable bool targetGuarded_ = false;
    JumpResult lastResult_;
    quint64 deliveredCount_ = 0;
    quint64 rejectedCount_ = 0;
    quint64 noTargetCount_ = 0;
    quint64 invalidCount_ = 0;
};

// The seam every panel uses. The free functions are the intended spelling; the
// singleton exists for the tests and for code that needs the counters.
void setTarget(IJumpTarget *target);
void clearTarget();
void clearTargetIf(IJumpTarget *target);
bool hasTarget();
JumpResult jumpTo(const QString &filePath, int line, int column);

} // namespace vedit::workspace::jump
