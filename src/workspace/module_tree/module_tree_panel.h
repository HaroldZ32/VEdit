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

class QLabel;
class QLineEdit;
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
    void setCatalog(const ModuleCatalog &catalog);
    /// "No project open" state: one concise line naming the reason.
    void setNoProject(const QString &reason);
    /// One-line failure from the client; never per-file errors.
    void setFailure(const QString &message);
    /// The visible status line: progress, failure or the last action's result.
    void setStatus(const QString &text);
    /// The Refresh button's action (the integration restarts the client chain).
    void setRefreshHandler(std::function<void()> handler);
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
    void rebuildTree();
    void applyFilter(const QString &needle);
    void addRow(QTreeWidgetItem *parent, const ModuleTreeNode &node);
    bool deliver(const ModuleJump &jump, const QString &description);
    void setActionStatus(const QString &text);
    void notifySelection();

    ModuleCatalog catalog_;
    QVector<ModuleTreeNode> roots_;
    QTreeWidget *tree_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *status_ = nullptr;
    QLineEdit *filter_ = nullptr;
    QToolButton *refresh_ = nullptr;
    QComboBox *top_ = nullptr;
    std::function<void(const QString &)> topHandler_;
    bool settingTop_ = false;
    QString scopeNote_;
    void emitTopChoice();
    std::function<void()> refreshHandler_;
    std::function<void(const QString &)> selectionHandler_;
    QString selectedModule_;
    QString lastActionStatus_;
    int jumpAttempts_ = 0;
    int jumpsDelivered_ = 0;
    int jumpsSkipped_ = 0;
};

} // namespace vedit::workspace::module_tree
