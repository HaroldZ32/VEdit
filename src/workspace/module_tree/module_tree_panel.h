#pragma once

// The module-tree panel after UX batch 2 (item 1).
//
// It paints a PURE MODULE HIERARCHY: the `is_top` modules as roots and, under
// each module, the modules its body instantiates. There are no instance paths,
// nets, drivers, loads or chains anywhere in this panel - the old elaborated
// hierarchy was replaced, not filtered. A module instantiated by two parents
// appears under both parents (the model, not the panel, guarantees it).
//
// The panel owns no data collection: it renders the `ModuleCatalog` a
// CatalogClient hands it, so it can be exercised headlessly without slang. Its
// only way to open an editor is the application-wide `jump::jumpTo()` contract:
// a double-click on a module row opens that module's DEFINITION file and lands
// on the declaration line.
#include "module_catalog.h"
#include "module_hierarchy.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QLineEdit;
class QScrollArea;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class QComboBox;

namespace vedit::workspace::module_tree {

class ModuleTreePanel final : public QWidget {
    Q_OBJECT
public:
    explicit ModuleTreePanel(QWidget *parent = nullptr);

    /// Renders one catalog answer (an empty catalog => visible empty state).
    void setCatalog(const ModuleCatalog &catalog, bool deferRendering = false);
    /// "No project open" state: one concise line naming the reason.
    void setNoProject(const QString &reason);
    /// One-line failure from the client; never per-file errors.
    void setFailure(const QString &message);
    /// The visible status line: progress, failure or the last action's result.
    void setStatus(const QString &text);
    /// The Refresh button's action (the integration restarts the client chain).
    void setRefreshHandler(std::function<void()> handler);
    /// AC M1.4: the Cancel button's action — it stops the RUNNING catalog (the
    /// service kills the `slang` child); it never starts one.
    void setOnCancel(std::function<void()> handler);
    /// Whether a run is in flight (the Cancel button's enabled state).
    void setCancelEnabled(bool enabled);
    bool cancelEnabled() const;
    /// RM-17: the ONE place the top is set — Auto, a detected candidate, or free
    /// text. `candidates` are the modules the catalog reports as uninstantiated.
    void setTopCandidates(const QStringList &candidates);
    /// Shows the EFFECTIVE top ("" = Auto) without emitting a change.
    void setEffectiveTop(const QString &top);
    QString effectiveTop() const;
    void setOnTopChanged(std::function<void(const QString &top)> handler);
    /// RM-17: the explicit exclusion line ("N file(s) outside the top X are not
    /// analysed"); empty removes it.
    void setScopeNote(const QString &text);
    /// AC-7.3 (batch-20261115): the fallback sentence ("top 'X' could not be
    /// elaborated — fell back to Auto"), appended to the summary line.
    void setTopNote(const QString &text);
    /// AC-2.1: without a filelist the folder is not a project, so the top
    /// control is disabled (and the empty state says why).
    void setTopEnabled(bool enabled);
    /// Fires with the selected module name, or an empty string when the
    /// selection is cleared / a marker row is selected. The interface panel
    /// follows this focus rule (a) through the integration.
    void setOnSelectionChanged(std::function<void(const QString &moduleName)> callback);

    /// Activates one row exactly like a double-click. A marker or a module with
    /// no definition performs NO jump and says why.
    bool activateItem(QTreeWidgetItem *item);

    QTreeWidget *tree() const;
    ModuleCatalog catalog() const;
    QVector<ModuleTreeNode> roots() const;
    QString selectedModuleName() const;
    QString summaryText() const;
    QString statusText() const;
    QString lastActionStatus() const;
    int jumpAttempts() const;
    int jumpsDelivered() const;
    int jumpsSkipped() const;

private:
    /// The ONE write path for the summary/status containers: keeps `full`
    /// verbatim (multiline included) for `summaryText()`/`statusText()` and the
    /// tooltip, and paints exactly that full text into the scrollable body - the
    /// newlines stay, so the user reads it line by line and drags the bar for the
    /// rest. The container's height follows the LINE COUNT only, never the text.
    void setScrollableText(QScrollArea *area, const QString &text);
    void rebuildTree();
    void prepareTree();
    void appendPreparedRows(quint64 generation);
    void finishTree(const QString &previous);
    void setPreparing(bool preparing);
    void applyFilter(const QString &needle);
    QTreeWidgetItem *addRow(QTreeWidgetItem *parent, const ModuleTreeNode &node, bool descendants = true);
    bool deliver(const ModuleJump &jump, const QString &description);
    void setActionStatus(const QString &text);
    void notifySelection();

    ModuleCatalog catalog_;
    QVector<ModuleTreeNode> roots_;
    quint64 treeGeneration_ = 0;
    QString selectionBeforeBuild_;
    struct PendingRow {
        QTreeWidgetItem *parent;
        const ModuleTreeNode *node;
    };
    QVector<PendingRow> pendingRows_;
    bool preparing_ = false;
    bool catalogRunning_ = false;
    QTreeWidget *tree_ = nullptr;
    /// R2-a (batch-20261119): a FIXED-HEIGHT scroller, not a content-sized label.
    /// R1-c's one elided line was rejected on the real machine ("the error cannot
    /// be read"); the body carries the full text, newlines kept, and the bars
    /// reach whatever does not fit. `setFixedHeight` (2 lines, 3 when the text has
    /// newlines, + the horizontal bar's height) keeps a bar appearing from moving
    /// the layout under the user's cursor.
    QScrollArea *summary_ = nullptr;
    QScrollArea *status_ = nullptr;
    /// R1-c/R2-c: the FULL summary/status text (never a rendering of it), which is
    /// what `summaryText()`/`statusText()` publish and the tooltip carries.
    QString summaryFull_;
    QString statusFull_;
    QLineEdit *filter_ = nullptr;
    QToolButton *refresh_ = nullptr;
    QToolButton *cancel_ = nullptr;
    QComboBox *top_ = nullptr;
    std::function<void(const QString &)> topHandler_;
    bool settingTop_ = false;
    QString scopeNote_;
    /// AC-7.3: the last invalid-top fallback, shown on the summary line.
    QString topNote_;
    void emitTopChoice();
    /// AC M1.3: one Enter press on the editable combo emits BOTH `activated` and
    /// `returnPressed`; only the first of the two may start a run. The flag is
    /// cleared on the next event-loop turn, so it suppresses the twin signal of
    /// the SAME gesture without any wall-clock window (a second, deliberate
    /// gesture — even with the same value — always commits).
    bool sameGestureCommit_ = false;
    std::function<void()> refreshHandler_;
    std::function<void()> cancelHandler_;
    std::function<void(const QString &)> selectionHandler_;
    QString selectedModule_;
    QString lastActionStatus_;
    int jumpAttempts_ = 0;
    int jumpsDelivered_ = 0;
    int jumpsSkipped_ = 0;
};

} // namespace vedit::workspace::module_tree
