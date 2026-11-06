#pragma once

// The Interface (port) panel: a SEPARATE dock that follows the focused module
// (UX batch 2, item 2).
//
// It renders one row per port of the focused module: `Name / Direction / Type /
// Width / Location`. The width is derived from the ABI's verbatim `type` string
// by `portWidthFromType()`: a literal packed range gives its width, no packed
// dimension gives 1, and an unparsable range renders an EMPTY cell with a
// tooltip explaining why (never a guessed number).
//
// Focus rule, exactly as the user ruled:
//   (a) the module selected in the module tree, else
//   (b) the module enclosing the editor caret: the nearest module definition at
//       or above the caret line in the SAME file, using the catalog positions.
//
// Port activation goes through the application-wide `jump::jumpTo()` seam:
//   * double-click -> the port DECLARATION;
//   * Ctrl+double-click -> the INSTANTIATION of this module (a child edge whose
//     `module` equals the focused module); several parents offer a chooser.
// The ABI has no per-connection position, so the Ctrl+double-click target is
// the instance statement, never the connected port - the tooltip says so.
#include "module_catalog.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace vedit::workspace::module_tree {

class InterfacePanel final : public QWidget {
    Q_OBJECT
public:
    explicit InterfacePanel(QWidget *parent = nullptr);

    /// One catalog answer. Re-renders the focused module (or the empty state).
    void setCatalog(const ModuleCatalog &catalog);
    /// "No project open" state: one concise line naming the reason.
    void setNoProject(const QString &reason);
    /// One-line failure from the client; never per-file errors.
    void setFailure(const QString &message);
    /// Focus rule (a): the module selected in the module tree; empty clears it.
    void setTreeSelection(const QString &moduleName);
    /// Focus rule (b): the active editor file and its 0-based caret line
    /// (-1 when there is no caret). Same file only, nearest definition above.
    void setCaretContext(const QString &file, int caretLine0);

    /// Deterministic chooser seam: return the candidate index to jump to, or -1
    /// to cancel. Without a chooser a QMenu lists the instance statements.
    void setInstantiationChooser(std::function<int(const QVector<InstantiationSite> &)> chooser);

    /// Activates one port row as a double-click would (control = Ctrl held).
    bool activatePortRow(int row, bool control);

    QTableWidget *table() const;
    QString focusedModuleName() const;
    const ModuleDefinition *focusedModule() const;
    /// The one-line empty-state reason (empty while a module is focused).
    QString emptyText() const;
    QString statusText() const;
    int jumpAttempts() const;
    int jumpsDelivered() const;
    int jumpsSkipped() const;
    /// Observability for the chooser: how many candidates the last Ctrl+double-
    /// click saw, and which candidates they were.
    int lastChooserCandidates() const;
    QVector<InstantiationSite> lastCandidates() const;

    /// The honest statement the panel shows as its tooltip (also asserted by the
    /// acceptance test): no per-connection position exists in the ABI.
    static QString activationTooltip();

private:
    void refreshFocus();
    void renderPorts();
    void clearTable(const QString &reason);
    void setStatus(const QString &text);
    bool deliver(const ModuleJump &jump, const QString &description);
    bool jumpToInstantiation();

    ModuleCatalog catalog_;
    QString treeSelection_;
    QString caretFile_;
    int caretLine0_ = -1;
    QString focusedName_;
    QString emptyText_;
    QLabel *heading_ = nullptr;
    QLabel *empty_ = nullptr;
    QLabel *status_ = nullptr;
    QTableWidget *table_ = nullptr;
    std::function<int(const QVector<InstantiationSite> &)> chooser_;
    QVector<InstantiationSite> lastCandidates_;
    int lastChooserCandidates_ = 0;
    QString statusText_;
    int jumpAttempts_ = 0;
    int jumpsDelivered_ = 0;
    int jumpsSkipped_ = 0;
};

} // namespace vedit::workspace::module_tree
