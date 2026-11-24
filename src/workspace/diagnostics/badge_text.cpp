#include "badge_text.h"

#include <QStringList>

namespace vedit::workspace::diagnostics {

namespace {

QString axisText(const vedit::workspace::BadgeAxis &axis)
{
    const QString effective =
        axis.effective.isEmpty() ? QStringLiteral("unavailable") : axis.effective;
    // No request to differ from (or a pair that is really in force): one token.
    if (axis.applied || axis.requested.isEmpty() || axis.requested == axis.effective) {
        return effective;
    }
    return QStringLiteral("%1→%2").arg(axis.requested, effective);
}

/// The reason token of an axis that did NOT take effect. `ok` would claim the
/// request is in force, so a producer that failed to fill a reason yields
/// `not-applied` instead of a self-contradicting line.
QString reasonText(const vedit::workspace::BadgeAxis &axis)
{
    if (axis.reason == vedit::workspace::BadgeReasonCode::Ok) {
        return QStringLiteral("not-applied");
    }
    return vedit::workspace::badgeReasonCodeName(axis.reason);
}

} // namespace

QString badgeText(const vedit::workspace::BadgeState &badge)
{
    QStringList reasons;
    if (!badge.lsp.applied) {
        reasons << reasonText(badge.lsp);
    }
    if (!badge.lint.applied) {
        reasons << reasonText(badge.lint);
    }
    // Two axes refused for the same reason are ONE fact: the arrows already show
    // which axes differ.
    reasons.removeDuplicates();
    const QString reason = reasons.isEmpty()
        ? vedit::workspace::badgeReasonCodeName(vedit::workspace::BadgeReasonCode::Ok)
        : reasons.join(QLatin1Char('+'));
    const QString basis = badge.basis.isEmpty() ? QStringLiteral("unknown") : badge.basis;
    return QStringLiteral("%1 · lint=%2 · basis=%3 · reason=%4")
        .arg(axisText(badge.lsp), axisText(badge.lint), basis, reason);
}

} // namespace vedit::workspace::diagnostics
