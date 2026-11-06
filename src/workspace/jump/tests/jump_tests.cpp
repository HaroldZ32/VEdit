// Self-check of the jump lane (frozen contract jump-contract):
//
//   * the contract is ONE interface (`IJumpTarget::jumpTo(file, line, column)`)
//     plus ONE process-wide registry - no panel needs its own jump plumbing;
//   * the unregistered case is an explicit no-op: the request is counted, the
//     well-formed position is preserved in lastResult(), nothing is dispatched
//     and the process does not crash;
//   * a well-formed request reaches the registered target unchanged (1-based
//     file/line/column, cross-file), and the target's refusal is reported as
//     Rejected instead of being hidden;
//   * a target that is destroyed WITHOUT clearTarget() is weak-guarded, so a
//     late jump reports NoTarget instead of dereferencing a dead object.
//
// The harness prints one CHECK line per assertion; evidence/jump/ keeps the log.
#include "jump/jump_registry.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>

#include <cstdio>
#include <cstring>

using vedit::workspace::jump::JumpResult;
using vedit::workspace::jump::JumpStatus;
using vedit::workspace::jump::Registry;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void checkLong(const char *name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual=%lld expected=%lld\n", name, ok ? "PASS" : "FAIL", actual,
                expected);
    std::fflush(stdout);
}

void checkString(const char *name, const QString &actual, const QString &expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual='%s' expected='%s'\n", name, ok ? "PASS" : "FAIL",
                actual.toUtf8().constData(), expected.toUtf8().constData());
    std::fflush(stdout);
}

// A panel-style target that is not a QObject (the contract allows it).
class PlainTarget final : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return accept;
    }

    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
    bool accept = true;
};

// The product shape: a QObject target owned by the shell.
class ObjectTarget final : public QObject, public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        ++calls;
        lastFile = filePath;
        lastLine = line;
        lastColumn = column;
        return true;
    }

    int calls = 0;
    QString lastFile;
    int lastLine = 0;
    int lastColumn = 0;
};

void testNoTargetNoOp()
{
    Registry &registry = Registry::instance();
    registry.reset();
    check("registry.initially-no-target", !vedit::workspace::jump::hasTarget());

    const JumpResult result = vedit::workspace::jump::jumpTo(QStringLiteral("rtl/top.sv"), 12, 3);
    std::printf("NO-TARGET status=%s file='%s' line=%d column=%d\n",
                vedit::workspace::jump::jumpStatusName(result.status),
                result.filePath.toUtf8().constData(), result.line, result.column);
    check("noop.status", result.status == JumpStatus::NoTarget);
    check("noop.request-preserved",
          result.filePath == QStringLiteral("rtl/top.sv") && result.line == 12
              && result.column == 3);
    checkLong("noop.count", static_cast<long long>(registry.noTargetCount()), 1);
    checkLong("noop.delivered", static_cast<long long>(registry.deliveredCount()), 0);
    check("noop.last-result", registry.lastResult().status == JumpStatus::NoTarget);

    // A second no-op keeps counting; nothing accumulates in the target path.
    (void)vedit::workspace::jump::jumpTo(QStringLiteral("rtl/second.sv"), 1, 1);
    checkLong("noop.count.two", static_cast<long long>(registry.noTargetCount()), 2);
}

void testInvalidRequest()
{
    Registry &registry = Registry::instance();
    registry.reset();
    PlainTarget target;
    vedit::workspace::jump::setTarget(&target);

    const JumpResult emptyPath = vedit::workspace::jump::jumpTo(QString(), 1, 1);
    const JumpResult blankPath = vedit::workspace::jump::jumpTo(QStringLiteral("   "), 1, 1);
    const JumpResult zeroLine = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 0, 1);
    const JumpResult zeroColumn = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 1, 0);
    check("invalid.empty-path", emptyPath.status == JumpStatus::InvalidRequest);
    check("invalid.blank-path", blankPath.status == JumpStatus::InvalidRequest);
    check("invalid.zero-line", zeroLine.status == JumpStatus::InvalidRequest);
    check("invalid.zero-column", zeroColumn.status == JumpStatus::InvalidRequest);
    checkLong("invalid.count", static_cast<long long>(registry.invalidCount()), 4);
    checkLong("invalid.target-not-called", target.calls, 0);
    checkLong("invalid.delivered", static_cast<long long>(registry.deliveredCount()), 0);
}

void testDelivery()
{
    Registry &registry = Registry::instance();
    registry.reset();
    PlainTarget target;
    vedit::workspace::jump::setTarget(&target);
    check("delivery.has-target", vedit::workspace::jump::hasTarget());

    const JumpResult result =
        vedit::workspace::jump::jumpTo(QStringLiteral("rtl/deep/mid.sv"), 42, 7);
    std::printf("DELIVERED target-calls=%d file='%s' line=%d column=%d\n", target.calls,
                target.lastFile.toUtf8().constData(), target.lastLine, target.lastColumn);
    check("delivery.status", result.status == JumpStatus::Delivered);
    checkLong("delivery.target-calls", target.calls, 1);
    checkString("delivery.file", target.lastFile, QStringLiteral("rtl/deep/mid.sv"));
    checkLong("delivery.line", target.lastLine, 42);
    checkLong("delivery.column", target.lastColumn, 7);
    checkLong("delivery.count", static_cast<long long>(registry.deliveredCount()), 1);
    check("delivery.last-result", registry.lastResult().status == JumpStatus::Delivered);

    // Refusal is reported as the target's own answer, never swallowed.
    target.accept = false;
    const JumpResult refused = vedit::workspace::jump::jumpTo(QStringLiteral("rtl/top.sv"), 1, 1);
    check("rejected.status", refused.status == JumpStatus::Rejected);
    checkLong("rejected.count", static_cast<long long>(registry.rejectedCount()), 1);
    checkLong("rejected.target-called", target.calls, 2);

    // Replacing the target: the new one receives the next jump, the old one
    // does not (the shell re-registers after an editor restart).
    PlainTarget replacement;
    vedit::workspace::jump::setTarget(&replacement);
    (void)vedit::workspace::jump::jumpTo(QStringLiteral("rtl/other.sv"), 5, 2);
    checkLong("replace.old-not-called", target.calls, 2);
    checkLong("replace.new-called", replacement.calls, 1);
    checkString("replace.new-file", replacement.lastFile, QStringLiteral("rtl/other.sv"));
}

void testClearSemantics()
{
    Registry &registry = Registry::instance();
    registry.reset();
    PlainTarget first;
    PlainTarget second;
    vedit::workspace::jump::setTarget(&second);

    // clearTargetIf() must not remove a target that is not the expected one.
    vedit::workspace::jump::clearTargetIf(&first);
    check("clear-if.other-kept", vedit::workspace::jump::hasTarget());
    vedit::workspace::jump::clearTargetIf(&second);
    check("clear-if.expected-cleared", !vedit::workspace::jump::hasTarget());

    (void)vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 1, 1);
    checkLong("clear.after-clear-no-target", static_cast<long long>(registry.noTargetCount()), 1);

    // nullptr is an explicit clear, not a crash.
    vedit::workspace::jump::setTarget(&first);
    vedit::workspace::jump::setTarget(nullptr);
    check("clear.null-nullptr-safe", !vedit::workspace::jump::hasTarget());
}

void testDestroyedQObjectTarget()
{
    Registry &registry = Registry::instance();
    registry.reset();

    auto *objectTarget = new ObjectTarget;
    vedit::workspace::jump::setTarget(objectTarget);
    const JumpResult delivered =
        vedit::workspace::jump::jumpTo(QStringLiteral("rtl/live.sv"), 3, 1);
    check("guard.delivered-while-alive", delivered.status == JumpStatus::Delivered);

    // The shell can destroy its target without calling clearTarget() (shutdown
    // order). The registry weak-guards it: no dangling dereference, no crash.
    delete objectTarget;
    check("guard.dropped-after-destroy", !vedit::workspace::jump::hasTarget());
    const JumpResult after = vedit::workspace::jump::jumpTo(QStringLiteral("rtl/gone.sv"), 9, 2);
    std::printf("DESTROYED-TARGET status=%s file='%s' (no crash)\n",
                vedit::workspace::jump::jumpStatusName(after.status),
                after.filePath.toUtf8().constData());
    check("guard.after-destroy-no-target", after.status == JumpStatus::NoTarget);
    check("guard.target-getter-null", registry.target() == nullptr);
}

void testStatusNames()
{
    check("status-name.no-target",
          std::strcmp(vedit::workspace::jump::jumpStatusName(JumpStatus::NoTarget), "no-target")
              == 0);
    check("status-name.invalid",
          std::strcmp(vedit::workspace::jump::jumpStatusName(JumpStatus::InvalidRequest),
                      "invalid-request")
              == 0);
    check("status-name.delivered",
          std::strcmp(vedit::workspace::jump::jumpStatusName(JumpStatus::Delivered), "delivered")
              == 0);
    check("status-name.rejected",
          std::strcmp(vedit::workspace::jump::jumpStatusName(JumpStatus::Rejected), "rejected")
              == 0);
}

// A target that explains its refusal, like the product's editor target does.
class ReasonTarget final : public vedit::workspace::jump::IJumpTarget {
public:
    bool jumpTo(const QString &, int, int) override { return false; }
    QString lastError() const override { return reason; }

    QString reason = QStringLiteral("cannot open /tmp/x.sv: not a regular file");
};

/// RM-4: a refusal must say WHY, and that WHY must survive the registry. Before
/// this contract, `Rejected` was all a panel could see, so every distinct cause
/// (missing file, no editor view, a cursor that did not land) looked identical.
void testRefusalReason()
{
    Registry &registry = Registry::instance();
    registry.reset();

    // (1) A target that explains itself: its sentence is the reason.
    ReasonTarget explained;
    vedit::workspace::jump::setTarget(&explained);
    const JumpResult refused = vedit::workspace::jump::jumpTo(QStringLiteral("/tmp/x.sv"), 4, 2);
    std::printf("REASON target='%s'\n", refused.reason.toUtf8().constData());
    check("reason.status", refused.status == JumpStatus::Rejected);
    checkString("reason.target-sentence", refused.reason, explained.reason);
    checkString("reason.last-result", registry.lastResult().reason, explained.reason);

    // (2) A target that cannot explain itself still yields a usable sentence.
    registry.reset();
    PlainTarget silent;
    silent.accept = false;
    vedit::workspace::jump::setTarget(&silent);
    const JumpResult unexplained = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 1, 1);
    check("reason.default-non-empty", !unexplained.reason.isEmpty());
    checkString("reason.default-sentence", unexplained.reason,
                QStringLiteral("the editor target refused the jump without a reason"));

    // (3) NoTarget and InvalidRequest carry their own explanation.
    registry.reset();
    const JumpResult noTarget = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 1, 1);
    check("reason.no-target-status", noTarget.status == JumpStatus::NoTarget);
    check("reason.no-target-non-empty", !noTarget.reason.isEmpty());
    check("reason.no-target-names-the-cause",
          noTarget.reason.contains(QStringLiteral("no jump target")));

    vedit::workspace::jump::setTarget(&silent);
    silent.accept = true;
    const JumpResult badColumn = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 1, 0);
    check("reason.invalid-status", badColumn.status == JumpStatus::InvalidRequest);
    check("reason.invalid-non-empty", !badColumn.reason.isEmpty());
    check("reason.invalid-names-the-position",
          badColumn.reason.contains(QStringLiteral("column=0")));

    // (4) A delivered jump has no reason to report.
    const JumpResult delivered = vedit::workspace::jump::jumpTo(QStringLiteral("a.v"), 2, 3);
    check("reason.delivered-status", delivered.status == JumpStatus::Delivered);
    check("reason.delivered-empty", delivered.reason.isEmpty());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testNoTargetNoOp();
    testInvalidRequest();
    testDelivery();
    testRefusalReason();
    testClearSemantics();
    testDestroyedQObjectTarget();
    testStatusNames();

    Registry::instance().reset();
    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
