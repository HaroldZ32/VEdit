#include "jump_registry.h"

#include <QMutexLocker>

namespace vedit::workspace::jump {

const char *jumpStatusName(JumpStatus status)
{
    switch (status) {
    case JumpStatus::NoTarget:
        return "no-target";
    case JumpStatus::InvalidRequest:
        return "invalid-request";
    case JumpStatus::Delivered:
        return "delivered";
    case JumpStatus::Rejected:
        return "rejected";
    }
    return "unknown";
}

Registry &Registry::instance()
{
    // Function-local static: constructed on first use, destroyed at exit. No
    // QApplication is required to register or ask for a target.
    static Registry registry;
    return registry;
}

void Registry::setTarget(IJumpTarget *target)
{
    QMutexLocker locker(&mutex_);
    target_ = target;
    targetGuard_.clear();
    targetGuarded_ = false;
    // The product target is a QObject (the shell owns it). Weak-guarding it
    // means a target that is destroyed at shutdown cannot be jumped to by a
    // late click: the registry notices the null QPointer and reports NoTarget.
    if (auto *object = dynamic_cast<QObject *>(target)) {
        targetGuard_ = object;
        targetGuarded_ = true;
    }
}

void Registry::clearTarget()
{
    QMutexLocker locker(&mutex_);
    target_ = nullptr;
    targetGuard_.clear();
    targetGuarded_ = false;
}

void Registry::clearTargetIf(IJumpTarget *expected)
{
    QMutexLocker locker(&mutex_);
    if (liveTargetLocked() == expected) {
        target_ = nullptr;
        targetGuard_.clear();
        targetGuarded_ = false;
    }
}

bool Registry::hasTarget() const
{
    QMutexLocker locker(&mutex_);
    return liveTargetLocked() != nullptr;
}

IJumpTarget *Registry::target() const
{
    QMutexLocker locker(&mutex_);
    return liveTargetLocked();
}

IJumpTarget *Registry::liveTargetLocked() const
{
    if (targetGuarded_ && targetGuard_.isNull()) {
        // The QObject was destroyed without clearing. Forget it; a jump from
        // here on is the ordinary no-target no-op.
        target_ = nullptr;
        targetGuarded_ = false;
    }
    return target_;
}

JumpResult Registry::jumpTo(const QString &filePath, int line, int column)
{
    JumpResult result;
    result.filePath = filePath;
    result.line = line;
    result.column = column;

    if (filePath.trimmed().isEmpty() || line < 1 || column < 1) {
        result.status = JumpStatus::InvalidRequest;
        // The WHY, so a panel does not have to invent one (RM-4).
        result.reason = filePath.trimmed().isEmpty()
                            ? QStringLiteral("the jump request names no file")
                            : QStringLiteral("the jump request has line=%1 column=%2; positions are "
                                             "1-based and must be at least 1")
                                  .arg(line)
                                  .arg(column);
        QMutexLocker locker(&mutex_);
        ++invalidCount_;
        lastResult_ = result;
        return result;
    }

    IJumpTarget *target = nullptr;
    {
        QMutexLocker locker(&mutex_);
        target = liveTargetLocked();
    }
    if (target == nullptr) {
        result.status = JumpStatus::NoTarget;
        result.reason = QStringLiteral("no jump target is registered (the editor lane is not "
                                       "installed)");
        QMutexLocker locker(&mutex_);
        ++noTargetCount_;
        lastResult_ = result;
        return result;
    }

    // Dispatch outside the lock: a target is allowed to call back into the
    // registry (hasTarget(), clearTargetIf(this), ...) without deadlocking.
    const bool accepted = target->jumpTo(filePath, line, column);

    result.status = accepted ? JumpStatus::Delivered : JumpStatus::Rejected;
    if (!accepted) {
        // The target's own words: this is what turns an unexplained refusal
        // into "cannot open <path>: <why>" for the user (RM-4).
        result.reason = target->lastError();
        if (result.reason.isEmpty()) {
            result.reason = QStringLiteral("the editor target refused the jump without a reason");
        }
    }
    QMutexLocker locker(&mutex_);
    if (accepted) {
        ++deliveredCount_;
    } else {
        ++rejectedCount_;
    }
    lastResult_ = result;
    return result;
}

quint64 Registry::deliveredCount() const
{
    QMutexLocker locker(&mutex_);
    return deliveredCount_;
}

quint64 Registry::rejectedCount() const
{
    QMutexLocker locker(&mutex_);
    return rejectedCount_;
}

quint64 Registry::noTargetCount() const
{
    QMutexLocker locker(&mutex_);
    return noTargetCount_;
}

quint64 Registry::invalidCount() const
{
    QMutexLocker locker(&mutex_);
    return invalidCount_;
}

JumpResult Registry::lastResult() const
{
    QMutexLocker locker(&mutex_);
    return lastResult_;
}

void Registry::reset()
{
    QMutexLocker locker(&mutex_);
    target_ = nullptr;
    targetGuard_.clear();
    targetGuarded_ = false;
    lastResult_ = JumpResult();
    deliveredCount_ = 0;
    rejectedCount_ = 0;
    noTargetCount_ = 0;
    invalidCount_ = 0;
}

void setTarget(IJumpTarget *target)
{
    Registry::instance().setTarget(target);
}

void clearTarget()
{
    Registry::instance().clearTarget();
}

void clearTargetIf(IJumpTarget *target)
{
    Registry::instance().clearTargetIf(target);
}

bool hasTarget()
{
    return Registry::instance().hasTarget();
}

JumpResult jumpTo(const QString &filePath, int line, int column)
{
    return Registry::instance().jumpTo(filePath, line, column);
}

} // namespace vedit::workspace::jump
