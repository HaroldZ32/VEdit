#include "vcs_capabilities.h"

#include <QCoreApplication>

namespace vedit::workspace::vcs {

namespace {

PanelCapability supported(PanelAction action)
{
    return PanelCapability{action, true, QString()};
}

PanelCapability unsupported(PanelAction action, const char *reason)
{
    // The literal passed here is wrapped in QT_TRANSLATE_NOOP("VEdit", ...) at the
    // call site; this is why the capability texts live in the SAME "VEdit"
    // context as every other user-visible string of the lane (and lupdate can
    // extract them).
    return PanelCapability{
        action, false, QCoreApplication::translate("VEdit", reason)};
}

} // namespace

const char *panelActionToken(PanelAction action)
{
    switch (action) {
    case PanelAction::Refresh:
        return "refresh";
    case PanelAction::Add:
        return "add";
    case PanelAction::Unstage:
        return "unstage";
    case PanelAction::Revert:
        return "revert";
    case PanelAction::Commit:
        return "commit";
    case PanelAction::Remove:
        return "remove";
    case PanelAction::Fetch:
        return "fetch";
    case PanelAction::Pull:
        return "pull";
    case PanelAction::Update:
        return "update";
    case PanelAction::Push:
        return "push";
    case PanelAction::Branch:
        return "branch";
    case PanelAction::Tag:
        return "tag";
    case PanelAction::Remote:
        return "remote";
    case PanelAction::Workspace:
        return "workspace";
    case PanelAction::Continue:
        return "continue";
    case PanelAction::Abort:
        return "abort";
    case PanelAction::History:
        return "history";
    }
    return "unknown";
}

QVector<PanelCapability> capabilitiesFor(Backend backend)
{
    switch (backend) {
    case Backend::Git:
        // Git: the whole working-copy and history surface the panel implements.
        // `Remove` and `Update` are Subversion's spellings: Git records a
        // deletion by staging the deleted file and updates with pull/fetch.
        return {
            supported(PanelAction::Refresh),
            supported(PanelAction::Add),
            supported(PanelAction::Unstage),
            supported(PanelAction::Revert),
            supported(PanelAction::Commit),
            unsupported(PanelAction::Remove,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Git records a deletion when the deleted file is "
                                          "staged; there is no separate remove operation.")),
            supported(PanelAction::Fetch),
            supported(PanelAction::Pull),
            unsupported(PanelAction::Update,
                        QT_TRANSLATE_NOOP("VEdit", "Git updates with Pull (or Fetch).")),
            supported(PanelAction::Push),
            supported(PanelAction::Branch),
            supported(PanelAction::Tag),
            supported(PanelAction::Remote),
            supported(PanelAction::Workspace),
            supported(PanelAction::Continue),
            supported(PanelAction::Abort),
            supported(PanelAction::History),
        };
    case Backend::Subversion:
        // Subversion 1.14.5: the working-copy operations of this build
        // (status/add/remove/revert/update/commit) plus the identity read. Every
        // "no" below names the real property of Subversion (or the accepted
        // scope), not an implementation gap dressed up as one.
        return {
            supported(PanelAction::Refresh),
            supported(PanelAction::Add),
            unsupported(PanelAction::Unstage,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Subversion has no staging area: changed files are "
                                          "committed directly from the working copy.")),
            supported(PanelAction::Revert),
            supported(PanelAction::Commit),
            supported(PanelAction::Remove),
            unsupported(PanelAction::Fetch,
                        QT_TRANSLATE_NOOP(
                            "VEdit", "Subversion has no remote-tracking branches to fetch.")),
            unsupported(PanelAction::Pull,
                        QT_TRANSLATE_NOOP(
                            "VEdit", "Subversion updates the working copy in place; use Update.")),
            supported(PanelAction::Update),
            unsupported(PanelAction::Push,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Subversion has no push: a commit publishes directly "
                                          "to the repository the working copy is bound to.")),
            unsupported(PanelAction::Branch,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Branches in Subversion are repository-side copies; "
                                          "branch creation is not part of this build's accepted "
                                          "Subversion surface.")),
            unsupported(PanelAction::Tag,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Tags in Subversion are repository-side copies; tag "
                                          "creation is not part of this build's accepted "
                                          "Subversion surface.")),
            unsupported(PanelAction::Remote,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "A Subversion working copy is bound to one URL; there "
                                          "is no remote set to add or remove.")),
            unsupported(PanelAction::Workspace,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Stashes and linked worktrees are Git concepts; a "
                                          "Subversion working copy is a single directory.")),
            unsupported(PanelAction::Continue,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Subversion has no multi-step merge or rebase to "
                                          "continue; conflicts are resolved per file and then "
                                          "committed.")),
            unsupported(PanelAction::Abort,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "Subversion has no multi-step merge or rebase to abort; "
                                          "revert restores a conflicted or modified file.")),
            unsupported(PanelAction::History,
                        QT_TRANSLATE_NOOP("VEdit",
                                          "The history view reads Git's log; Subversion's log "
                                          "(available in svn 1.14.5) is outside this build's "
                                          "accepted read set.")),
        };
    case Backend::None:
        break;
    }
    QVector<PanelCapability> table;
    for (PanelAction action : {PanelAction::Refresh, PanelAction::Add, PanelAction::Unstage,
                               PanelAction::Revert, PanelAction::Commit, PanelAction::Remove,
                               PanelAction::Fetch, PanelAction::Pull, PanelAction::Update,
                               PanelAction::Push, PanelAction::Branch, PanelAction::Tag,
                               PanelAction::Remote, PanelAction::Workspace,
                               PanelAction::Continue, PanelAction::Abort,
                               PanelAction::History}) {
        table.push_back(PanelAction::Refresh == action
                            ? PanelCapability{action, true, QString()}
                            : unsupported(action,
                                          QT_TRANSLATE_NOOP("VEdit",
                                                            "Open a Git or Subversion working "
                                                            "tree first.")));
    }
    return table;
}

PanelCapability capabilityFor(Backend backend, PanelAction action)
{
    for (const PanelCapability &row : capabilitiesFor(backend)) {
        if (row.action == action) {
            return row;
        }
    }
    return PanelCapability{
        action, false,
        QCoreApplication::translate("VEdit", "This action is not part of the VCS panel.")};
}

QString capabilitySummary(Backend backend)
{
    switch (backend) {
    case Backend::Git:
        return QCoreApplication::translate(
            "VEdit", "Git: staging, commits, remotes, branches, tags, stashes, worktrees and "
                     "conflict resolution are available.");
    case Backend::Subversion:
        return QCoreApplication::translate(
            "VEdit", "Subversion: add, commit, revert, remove and update are available. Staging, "
                     "fetches, pushes, branches, tags, remotes, stashes, worktrees and conflict "
                     "continuation are not — hover a disabled action for its reason.");
    case Backend::None:
        break;
    }
    return QCoreApplication::translate(
        "VEdit", "Open a Git or Subversion working tree to enable the actions.");
}

QString capabilityReport(Backend backend)
{
    QStringList parts;
    parts << (backend == Backend::Subversion ? QStringLiteral("svn")
             : backend == Backend::Git        ? QStringLiteral("git")
                                              : QStringLiteral("none"));
    for (const PanelCapability &row : capabilitiesFor(backend)) {
        QString part = QString::fromLatin1(panelActionToken(row.action));
        part += row.supported ? QStringLiteral("=yes") : QStringLiteral("=no");
        if (!row.supported) {
            part += QStringLiteral("(") + row.reason + QStringLiteral(")");
        }
        parts << part;
    }
    return parts.join(QStringLiteral(" "));
}

QStringList disabledActionReasons(Backend backend)
{
    QStringList reasons;
    for (const PanelCapability &row : capabilitiesFor(backend)) {
        if (!row.supported) {
            reasons << QStringLiteral("%1: %2")
                           .arg(QString::fromLatin1(panelActionToken(row.action)), row.reason);
        }
    }
    return reasons;
}

} // namespace vedit::workspace::vcs
