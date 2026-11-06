#pragma once

// Per-backend capability table for the VCS panel (batch 6b, AC-6.3 / AC-6.5).
//
// The panel has ONE three-column layout and ONE action bar for both backends.
// Which actions a backend can actually perform is NOT a matter of taste: every
// row below names the tool capability behind it, and an unsupported action is
// DISABLED with the REAL reason (never a silent no-op and never an invented
// "not implemented"). A capability that the bundled tool has but the panel does
// not offer (e.g. `svn log`, which exists in Subversion 1.14.5 but is outside
// this build's accepted read set) is listed here with that scope reason as well,
// so the evidence's capability table and the UI cannot disagree.
//
// The bundled Subversion is 1.14.5 (batch 6a). Every "supported" row was
// verified against that client in `evidence/svn-e2e/` (command + output); if a
// future bundled version loses one, the row must change to unsupported with the
// real reason — no silent downgrade (AC-6.5).
#include "vcs_source.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vedit::workspace::vcs {

/// One action slot of the action bar (the pull button serves as `Pull` for Git
/// and as `Update` for Subversion, so both are rows of the table).
enum class PanelAction {
    Refresh,
    Add,
    Unstage,
    Revert,
    Commit,
    Remove,
    Fetch,
    Pull,
    Update,
    Push,
    Branch,
    Tag,
    Remote,
    Workspace,
    Continue,
    Abort,
    History,
};

/// The capability of one action under one backend.
struct PanelCapability {
    PanelAction action = PanelAction::Refresh;
    bool supported = false;
    /// The real reason, translated; empty exactly when `supported` is true.
    QString reason;
};

/// The table for one backend, in a stable order (Refresh first, History last).
QVector<PanelCapability> capabilitiesFor(Backend backend);

/// One row of the table (unknown action => unsupported with a reason).
PanelCapability capabilityFor(Backend backend, PanelAction action);

/// Stable lowercase token used by the probe and by the evidence scripts
/// (`refresh`, `add`, `unstage`, `revert`, `commit`, `remove`, `fetch`, `pull`,
/// `update`, `push`, `branch`, `tag`, `remote`, `workspace`, `continue`,
/// `abort`, `history`).
const char *panelActionToken(PanelAction action);

/// The one-line, translated summary the panel shows above the action bar:
/// which operations ARE available and which are not, so a user never has to
/// discover a disabled button without a reason.
QString capabilitySummary(Backend backend);

/// Machine-readable report for probes and the evidence log, one line:
/// `svn: add=yes unstage=no(...) ...` (the reasons are translated).
QString capabilityReport(Backend backend);

/// `action: reason` for every unsupported action, in table order (translated).
QStringList disabledActionReasons(Backend backend);

} // namespace vedit::workspace::vcs
