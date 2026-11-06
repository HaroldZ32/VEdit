#include "module_tree_panel.h"

#include "jump/jump_registry.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace vedit::workspace::module_tree {

namespace {

// The tree row carries everything a double-click needs. The jump goes through
// the process-wide registry, so the panel never keeps a file-open path of its own.
constexpr int kFileRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;
constexpr int kColumnRole = Qt::UserRole + 2;
constexpr int kKindRole = Qt::UserRole + 3;
constexpr int kModuleRole = Qt::UserRole + 4;
constexpr int kPathRole = Qt::UserRole + 5;
constexpr int kNoteRole = Qt::UserRole + 6;

QString oneLine(QString text)
{
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.simplified();
}

QString rowLabel(const ModuleTreeNode &node)
{
    switch (node.kind) {
    case ModuleRowKind::Module:
        return node.moduleName;
    case ModuleRowKind::Cycle:
        return QCoreApplication::translate("VEdit", "cycle: %1").arg(node.moduleName);
    case ModuleRowKind::DepthLimit:
        return QCoreApplication::translate("VEdit", "depth limit (%1) reached")
            .arg(kModuleTreeMaxDepth);
    case ModuleRowKind::Truncated:
        return QCoreApplication::translate("VEdit", "tree truncated (%1 rows)")
            .arg(kModuleTreeMaxRows);
    }
    return QString();
}

int countItems(const QTreeWidgetItem *item)
{
    int total = 1;
    for (int index = 0; index < item->childCount(); ++index) {
        total += countItems(item->child(index));
    }
    return total;
}

QTreeWidgetItem *findModuleRow(QTreeWidgetItem *item, const QString &moduleName)
{
    if (item->data(0, kKindRole).toInt() == static_cast<int>(ModuleRowKind::Module)
        && item->data(0, kModuleRole).toString() == moduleName) {
        return item;
    }
    for (int index = 0; index < item->childCount(); ++index) {
        if (QTreeWidgetItem *found = findModuleRow(item->child(index), moduleName)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

ModuleTreePanel::ModuleTreePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("module-tree-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(4);
    refresh_ = new QToolButton(this);
    refresh_->setObjectName(QStringLiteral("module-tree-refresh"));
    refresh_->setText(QCoreApplication::translate("VEdit", "Refresh"));
    refresh_->setToolTip(
        QCoreApplication::translate("VEdit", "Re-run the module catalog for the whole project"));
    toolbar->addWidget(refresh_);
    // RM-17: the ONE place the top is chosen. Auto = today's behaviour (every
    // uninstantiated module is a root); the list holds the detected candidates;
    // typing a name the list does not know is allowed (slang reports the name
    // back if it cannot elaborate it). Clearing the text returns to Auto.
    auto *topLabel = new QLabel(QCoreApplication::translate("VEdit", "Top:"), this);
    topLabel->setObjectName(QStringLiteral("module-tree-top-label"));
    toolbar->addWidget(topLabel);
    top_ = new QComboBox(this);
    top_->setObjectName(QStringLiteral("module-tree-top"));
    top_->setEditable(true);
    top_->setInsertPolicy(QComboBox::NoInsert);
    top_->addItem(QCoreApplication::translate("VEdit", "Auto"), QString());
    top_->setToolTip(QCoreApplication::translate(
        "VEdit", "Top module for the module tree and the lint scope (empty = Auto)"));
    toolbar->addWidget(top_, 1);
    QObject::connect(top_, &QComboBox::activated, this, [this](int) { emitTopChoice(); });
    if (top_->lineEdit() != nullptr) {
        QObject::connect(top_->lineEdit(), &QLineEdit::editingFinished, this,
                         [this]() { emitTopChoice(); });
    }
    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("module-tree-filter"));
    filter_->setPlaceholderText(QCoreApplication::translate("VEdit", "Filter modules…"));
    filter_->setClearButtonEnabled(true);
    toolbar->addWidget(filter_, 1);
    layout->addLayout(toolbar);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("module-tree"));
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({QCoreApplication::translate("VEdit", "Module"),
                            QCoreApplication::translate("VEdit", "Instantiations"),
                            QCoreApplication::translate("VEdit", "Definition")});
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setRootIsDecorated(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(tree_, 1);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("module-tree-summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("module-tree-status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    QObject::connect(refresh_, &QToolButton::clicked, this, [this]() {
        if (refreshHandler_ != nullptr) {
            refreshHandler_();
        } else {
            setActionStatus(QCoreApplication::translate(
                "VEdit", "nothing to refresh: no catalog client"));
        }
    });
    QObject::connect(filter_, &QLineEdit::textChanged, this,
                     [this](const QString &needle) { applyFilter(needle); });
    QObject::connect(tree_, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem *item, int) { activateItem(item); });
    QObject::connect(tree_, &QTreeWidget::currentItemChanged, this,
                     [this](QTreeWidgetItem *, QTreeWidgetItem *) { notifySelection(); });

    setStatus(QCoreApplication::translate(
        "VEdit", "No module catalog yet: open a project and press Refresh."));
}

void ModuleTreePanel::setCatalog(const ModuleCatalog &catalog)
{
    catalog_ = catalog;
    if (!catalog_.valid || !catalog_.error.isEmpty()) {
        tree_->clear();
        roots_.clear();
        summary_->clear();
        setStatus(catalog_.error.isEmpty()
                      ? QCoreApplication::translate(
                            "VEdit", "No module catalog yet: open a project and press Refresh.")
                      : oneLine(catalog_.error));
        notifySelection();
        return;
    }
    if (catalog_.modules.isEmpty()) {
        tree_->clear();
        roots_.clear();
        summary_->setText(catalog_.summaryLine());
        setStatus(QCoreApplication::translate(
            "VEdit", "No modules found: slang reported no module definitions for this project."));
        notifySelection();
        return;
    }
    if (catalog_.topModuleNames().isEmpty()) {
        tree_->clear();
        roots_.clear();
        summary_->setText(catalog_.summaryLine());
        setStatus(QCoreApplication::translate(
            "VEdit", "No top module: every catalogued module is instantiated by another "
                     "module, so the hierarchy has no root."));
        notifySelection();
        return;
    }
    QString summary = catalog_.summaryLine();
    if (!catalog_.projectRoot.isEmpty()) {
        summary += QCoreApplication::translate("VEdit", "\nroot: %1").arg(catalog_.projectRoot);
    }
    summary_->setText(summary);
    rebuildTree();
    setStatus(QCoreApplication::translate(
        "VEdit", "Module tree ready — double-click a module to open its declaration."));
}

void ModuleTreePanel::setNoProject(const QString &reason)
{
    catalog_ = ModuleCatalog{};
    roots_.clear();
    tree_->clear();
    summary_->clear();
    setStatus(oneLine(reason));
    notifySelection();
}

void ModuleTreePanel::setFailure(const QString &message)
{
    catalog_ = ModuleCatalog{};
    roots_.clear();
    tree_->clear();
    summary_->clear();
    setStatus(oneLine(message));
    notifySelection();
}

void ModuleTreePanel::setStatus(const QString &text)
{
    if (status_ != nullptr) {
        status_->setText(text);
    }
}

void ModuleTreePanel::setRefreshHandler(std::function<void()> handler)
{
    refreshHandler_ = std::move(handler);
}

void ModuleTreePanel::setOnSelectionChanged(std::function<void(const QString &)> callback)
{
    selectionHandler_ = std::move(callback);
}

void ModuleTreePanel::notifySelection()
{
    QTreeWidgetItem *item = tree_ != nullptr ? tree_->currentItem() : nullptr;
    QString module;
    if (item != nullptr
        && item->data(0, kKindRole).toInt() == static_cast<int>(ModuleRowKind::Module)) {
        module = item->data(0, kModuleRole).toString();
    }
    selectedModule_ = module;
    if (selectionHandler_ != nullptr) {
        selectionHandler_(selectedModule_);
    }
}

QString ModuleTreePanel::selectedModuleName() const
{
    return selectedModule_;
}

void ModuleTreePanel::addRow(QTreeWidgetItem *parent, const ModuleTreeNode &node)
{
    auto *item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
    item->setText(0, rowLabel(node));
    item->setText(1, node.instantiationCount > 0 ? QString::number(node.instantiationCount)
                                                 : QString());
    const QString position = describePosition(node.definitionFile, node.definitionLine,
                                              node.definitionColumn);
    item->setText(2, position == QLatin1String("-") ? QString() : position);
    // ONE conversion path (review Nit 7): the row stores the 1-based jump
    // position produced by the same helper the interface panel uses, so a later
    // change to the conversion (clamping, say) cannot be bypassed by this panel.
    const ModuleJump jump =
        jumpFromPosition(node.definitionFile, node.definitionLine, node.definitionColumn);
    item->setData(0, kFileRole, jump.file);
    item->setData(0, kLineRole, jump.line);
    item->setData(0, kColumnRole, jump.column);
    item->setData(0, kKindRole, static_cast<int>(node.kind));
    item->setData(0, kModuleRole, node.moduleName);
    item->setData(0, kPathRole, node.instancePath);
    item->setData(0, kNoteRole, node.note);

    if (node.kind == ModuleRowKind::Module) {
        QString tooltip = node.instancePath.isEmpty() ? node.moduleName : node.instancePath;
        if (!position.isEmpty() && position != QLatin1String("-")) {
            tooltip += QStringLiteral("\n") + position;
        }
        if (node.note.isEmpty()) {
            tooltip += QStringLiteral("\n")
                       + QCoreApplication::translate(
                           "VEdit", "Double-click: open this module's declaration");
        } else {
            tooltip += QStringLiteral("\n") + node.note;
        }
        item->setToolTip(0, tooltip);
        item->setToolTip(2, node.note.isEmpty() ? position : node.note);
    } else {
        item->setToolTip(0, node.note);
        item->setToolTip(2, node.note);
    }

    for (const ModuleTreeNode &child : node.children) {
        addRow(item, child);
    }
    if (parent == nullptr) {
        tree_->addTopLevelItem(item);
    }
}

void ModuleTreePanel::rebuildTree()
{
    const QString previous = selectedModule_;
    tree_->clear();
    roots_ = buildModuleTree(catalog_);
    for (const ModuleTreeNode &node : roots_) {
        addRow(nullptr, node);
    }
    int total = 0;
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        total += countItems(tree_->topLevelItem(index));
    }
    if (total <= 300) {
        tree_->expandAll();
    } else {
        tree_->expandToDepth(0);
    }
    applyFilter(filter_ != nullptr ? filter_->text() : QString());
    if (!previous.isEmpty() && catalog_.find(previous) != nullptr) {
        for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
            if (QTreeWidgetItem *found = findModuleRow(tree_->topLevelItem(index), previous)) {
                tree_->setCurrentItem(found);
                break;
            }
        }
    }
    notifySelection();
}

void ModuleTreePanel::applyFilter(const QString &needle)
{
    if (tree_ == nullptr) {
        return;
    }
    const QString text = needle.trimmed();
    // A row is visible when it matches, or when one of its descendants does: a
    // filter must never hide the parent that contains the matching module.
    const auto show = [&text](QTreeWidgetItem *item, const auto &self) -> bool {
        bool visible = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                       || item->text(1).contains(text, Qt::CaseInsensitive)
                       || item->text(2).contains(text, Qt::CaseInsensitive);
        for (int index = 0; index < item->childCount(); ++index) {
            visible = self(item->child(index), self) || visible;
        }
        item->setHidden(!visible);
        return visible;
    };
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        show(tree_->topLevelItem(index), show);
    }
}

bool ModuleTreePanel::deliver(const ModuleJump &jump, const QString &description)
{
    if (!jump.isValid()) {
        ++jumpsSkipped_;
        setActionStatus(description);
        return false;
    }
    ++jumpAttempts_;
    const jump::JumpResult result = jump::jumpTo(jump.file, jump.line, jump.column);
    const QString target = QStringLiteral("%1:%2:%3")
                               .arg(QFileInfo(jump.file).fileName())
                               .arg(jump.line)
                               .arg(jump.column);
    switch (result.status) {
    case jump::JumpStatus::Delivered:
        ++jumpsDelivered_;
        setActionStatus(QCoreApplication::translate("VEdit", "%1 → %2")
                            .arg(description, target));
        return true;
    case jump::JumpStatus::NoTarget:
        setActionStatus(
            QCoreApplication::translate(
                "VEdit",
                "%1: no jump target is registered (the editor lane is not installed), so "
                "nothing was opened")
                .arg(description));
        return false;
    case jump::JumpStatus::Rejected:
        // RM-4: the WHY travels with the refusal (jump::JumpResult::reason), so
        // this is never the bare "the editor rejected the jump to X" that hid
        // every distinct cause behind one sentence. A refusal is still a
        // refusal: nothing is retried and nothing is silently swallowed.
        setActionStatus(QCoreApplication::translate("VEdit",
                                                    "%1: the editor rejected the jump to %2 — %3")
                            .arg(description, target,
                                 result.reason.isEmpty()
                                     ? QCoreApplication::translate("VEdit", "no reason was reported")
                                     : result.reason));
        return false;
    case jump::JumpStatus::InvalidRequest:
        setActionStatus(QCoreApplication::translate("VEdit",
                                                    "%1: the jump request (%2) was refused — %3")
                            .arg(description, target,
                                 result.reason.isEmpty()
                                     ? QCoreApplication::translate("VEdit", "no reason was reported")
                                     : result.reason));
        return false;
    }
    return false;
}

bool ModuleTreePanel::activateItem(QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return false;
    }
    const ModuleRowKind kind = static_cast<ModuleRowKind>(item->data(0, kKindRole).toInt());
    const QString module = item->data(0, kModuleRole).toString();
    if (kind != ModuleRowKind::Module) {
        ++jumpsSkipped_;
        const QString note = item->data(0, kNoteRole).toString();
        setActionStatus(note.isEmpty()
                            ? QCoreApplication::translate("VEdit",
                                                          "this marker row has no location")
                            : note);
        return false;
    }
    ModuleJump jump;
    jump.file = item->data(0, kFileRole).toString();
    jump.line = item->data(0, kLineRole).toInt();
    jump.column = item->data(0, kColumnRole).toInt();
    if (!jump.isValid()) {
        ++jumpsSkipped_;
        setActionStatus(QCoreApplication::translate(
                            "VEdit",
                            "%1 has no definition position in the module catalog, so nothing "
                            "was opened")
                            .arg(module));
        return false;
    }
    return deliver(jump, QCoreApplication::translate("VEdit", "%1 definition").arg(module));
}

void ModuleTreePanel::setActionStatus(const QString &text)
{
    lastActionStatus_ = text;
    setStatus(text);
}

QTreeWidget *ModuleTreePanel::tree() const
{
    return tree_;
}

ModuleCatalog ModuleTreePanel::catalog() const
{
    return catalog_;
}

QVector<ModuleTreeNode> ModuleTreePanel::roots() const
{
    return roots_;
}

QString ModuleTreePanel::lastActionStatus() const
{
    return lastActionStatus_;
}

void ModuleTreePanel::setScopeNote(const QString &text)
{
    scopeNote_ = text;
    if (!text.isEmpty()) {
        setStatus(text);
    }
}

void ModuleTreePanel::setTopCandidates(const QStringList &candidates)
{
    if (top_ == nullptr) {
        return;
    }
    const QString effective = effectiveTop();
    settingTop_ = true;
    top_->clear();
    top_->addItem(QCoreApplication::translate("VEdit", "Auto"), QString());
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty()) {
            top_->addItem(candidate, candidate);
        }
    }
    if (!effective.isEmpty()) {
        const int index = top_->findText(effective, Qt::MatchExactly);
        if (index >= 0) {
            top_->setCurrentIndex(index);
        } else {
            top_->setCurrentText(effective);
        }
    } else {
        top_->setCurrentIndex(0);
    }
    settingTop_ = false;
}

void ModuleTreePanel::setEffectiveTop(const QString &top)
{
    if (top_ == nullptr) {
        return;
    }
    settingTop_ = true;
    const QString trimmed = top.trimmed();
    if (trimmed.isEmpty()) {
        top_->setCurrentIndex(0);
    } else {
        const int index = top_->findText(trimmed, Qt::MatchExactly);
        if (index >= 0) {
            top_->setCurrentIndex(index);
        } else {
            top_->setCurrentText(trimmed);
        }
    }
    settingTop_ = false;
}

QString ModuleTreePanel::effectiveTop() const
{
    if (top_ == nullptr) {
        return QString();
    }
    if (top_->currentIndex() == 0 && top_->currentText()
        == QCoreApplication::translate("VEdit", "Auto")) {
        return QString();
    }
    const QString text = top_->currentText().trimmed();
    return text == QCoreApplication::translate("VEdit", "Auto") ? QString() : text;
}

void ModuleTreePanel::setOnTopChanged(std::function<void(const QString &)> handler)
{
    topHandler_ = std::move(handler);
}

void ModuleTreePanel::emitTopChoice()
{
    if (settingTop_ || topHandler_ == nullptr) {
        return;
    }
    topHandler_(effectiveTop());
}

QString ModuleTreePanel::summaryText() const
{
    return summary_ != nullptr ? summary_->text() : QString();
}

QString ModuleTreePanel::statusText() const
{
    return status_ != nullptr ? status_->text() : QString();
}

int ModuleTreePanel::jumpAttempts() const
{
    return jumpAttempts_;
}

int ModuleTreePanel::jumpsDelivered() const
{
    return jumpsDelivered_;
}

int ModuleTreePanel::jumpsSkipped() const
{
    return jumpsSkipped_;
}

} // namespace vedit::workspace::module_tree
