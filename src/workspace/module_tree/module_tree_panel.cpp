#include "module_tree_panel.h"
#include "../../common/item_context_menu.h"

#include "jump/jump_registry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <memory>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
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

// R1-c (batch-20261119) rendered the summary/status as ONE elided line so a huge
// warning could not grow them. The user rejected that on the real machine: an
// error that does not fit one line cannot be READ, and a tooltip is not a reading
// surface. R2-a keeps the height bound but removes the information loss: each
// line is a FIXED-HEIGHT read-only scroller whose body carries the whole text -
// newlines included - so the user reads it line by line and drags a bar for the
// rest. The height is a function of the LINE COUNT only (2 lines, 3 when the text
// keeps a newline), plus the horizontal bar's height reserved up front, so a bar
// appearing or disappearing can never move the layout.
constexpr int kScrollableLines = 2;
constexpr int kScrollableLinesMultiline = 3;

int scrollableHeight(const QScrollArea *area, int lines)
{
    return area->fontMetrics().lineSpacing() * lines
        + area->horizontalScrollBar()->sizeHint().height() + 2 * area->frameWidth();
}

void configureScrollableBody(QScrollArea *area, QLabel *body)
{
    area->setFrameShape(QFrame::NoFrame);
    // The body is as wide as its text (thousands of pixels for a real warning), so
    // the scroll range is the body's own size. A resizable widget would be squeezed
    // into the viewport and there would be nothing to drag.
    area->setWidgetResizable(false);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // The R1 failure was a label demanding a huge HEIGHT; the mirror-image trap is
    // a body demanding a huge WIDTH from the dock layout. Neither may happen.
    area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(false);
    // R2-c: selectable AND keyboard-focusable. `TextSelectableByKeyboard` is what
    // gives the label a focus policy at all, i.e. what makes Ctrl+C reach the text
    // control the selection lives in (mouse-only selection would be a dead end).
    body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    body->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    area->setWidget(body);
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
    // AC M1.4: the ONE way to stop a run. A 20k-project elaboration can take
    // minutes; before this the only exit was releasing the handle, which left
    // `slang` running.
    cancel_ = new QToolButton(this);
    cancel_->setObjectName(QStringLiteral("module-tree-cancel"));
    cancel_->setText(QCoreApplication::translate("VEdit", "Cancel"));
    cancel_->setToolTip(
        QCoreApplication::translate("VEdit", "Stop the running module catalog (kills slang)"));
    cancel_->setEnabled(false);
    toolbar->addWidget(cancel_);
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
    // AC M1.3: a top is committed by an EXPLICIT gesture only - choosing an entry
    // or pressing Enter. The old `editingFinished` also fired on focus loss, so
    // clicking anywhere else started a whole-project catalog nobody asked for
    // (and a second one per extra click).
    QObject::connect(top_, &QComboBox::activated, this, [this](int) { emitTopChoice(); });
    if (top_->lineEdit() != nullptr) {
        QObject::connect(top_->lineEdit(), &QLineEdit::returnPressed, this,
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
    // R1-d (batch-20261119): the table keeps SIX visible rows even under a long
    // warning. A minimum on the tree alone is short by its chrome: the viewport
    // is the tree minus the header and the frame, so both are charged here
    // explicitly (plus half a line of style slack).
    const int rowHeight = tree_->fontMetrics().height();
    const int chrome = tree_->header()->sizeHint().height() + 2 * tree_->frameWidth();
    tree_->setMinimumHeight(rowHeight * 6 + chrome + rowHeight / 2);
    layout->addWidget(tree_, 1);

    summary_ = new QScrollArea(this);
    summary_->setObjectName(QStringLiteral("module-tree-summary"));
    auto *summaryBody = new QLabel();
    summaryBody->setObjectName(QStringLiteral("module-tree-summary-text"));
    configureScrollableBody(summary_, summaryBody);
    layout->addWidget(summary_);
    status_ = new QScrollArea(this);
    status_->setObjectName(QStringLiteral("module-tree-status"));
    auto *statusBody = new QLabel();
    statusBody->setObjectName(QStringLiteral("module-tree-status-text"));
    configureScrollableBody(status_, statusBody);
    layout->addWidget(status_);

    QObject::connect(refresh_, &QToolButton::clicked, this, [this]() {
        if (refreshHandler_ != nullptr) {
            refreshHandler_();
        } else {
            setActionStatus(QCoreApplication::translate(
                "VEdit", "nothing to refresh: no catalog client"));
        }
    });
    QObject::connect(cancel_, &QToolButton::clicked, this, [this]() {
        if (preparing_ && !catalogRunning_) {
            setNoProject(QCoreApplication::translate("VEdit", "Module tree preparation cancelled."));
        } else if (cancelHandler_ != nullptr) {
            cancelHandler_();
        }
    });
    QObject::connect(filter_, &QLineEdit::textChanged, this,
                     [this](const QString &needle) { applyFilter(needle); });
    QObject::connect(tree_, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem *item, int) { activateItem(item); });
    QObject::connect(tree_, &QTreeWidget::currentItemChanged, this,
                     [this](QTreeWidgetItem *, QTreeWidgetItem *) { notifySelection(); });
    new vedit::common::ItemContextMenu(tree_, [this](QMenu &menu, const QPersistentModelIndex &hit) {
        const QModelIndex row = hit.sibling(hit.row(), 0);
        const QString file = row.data(kFileRole).toString();
        QAction *open = menu.addAction(tr("Go to Definition"), this, [this, hit]() {
            if (hit.isValid()) {
                activateItem(tree_->itemFromIndex(hit));
            }
        });
        open->setEnabled(!file.isEmpty() && row.data(kLineRole).toInt() > 0 &&
                         row.data(kKindRole).toInt() == static_cast<int>(ModuleRowKind::Module));
        vedit::common::addCopyAction(menu, tr("Copy Instance Path"), row.data(kPathRole).toString());
    });

    setStatus(QCoreApplication::translate(
        "VEdit", "No module catalog yet: open a project and press Refresh."));
}

void ModuleTreePanel::setCatalog(const ModuleCatalog &catalog, bool deferRendering)
{
    ++treeGeneration_;
    pendingRows_.clear();
    setPreparing(false);
    catalog_ = catalog;
    if (!catalog_.valid || !catalog_.error.isEmpty()) {
        tree_->clear();
        roots_.clear();
        setScrollableText(summary_, QString());
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
        setScrollableText(summary_, catalog_.summaryLine());
        setStatus(QCoreApplication::translate(
            "VEdit", "No modules found: slang reported no module definitions for this project."));
        notifySelection();
        return;
    }
    if (catalog_.topModuleNames().isEmpty()) {
        tree_->clear();
        roots_.clear();
        setScrollableText(summary_, catalog_.summaryLine());
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
    if (!topNote_.isEmpty()) {
        summary += QCoreApplication::translate("VEdit", "\n%1").arg(topNote_);
    }
    setScrollableText(summary_, summary);
    if (deferRendering && catalog_.modules.size() > 300) {
        prepareTree();
    } else {
        rebuildTree();
    }
}

void ModuleTreePanel::setNoProject(const QString &reason)
{
    ++treeGeneration_;
    pendingRows_.clear();
    setPreparing(false);
    catalog_ = ModuleCatalog{};
    roots_.clear();
    tree_->clear();
    setScrollableText(summary_, QString());
    setStatus(oneLine(reason));
    notifySelection();
}

void ModuleTreePanel::setFailure(const QString &message)
{
    ++treeGeneration_;
    pendingRows_.clear();
    setPreparing(false);
    // F6 (batch-20261118 review): a FAILED run must be observable AS a failed
    // catalog. `catalog()` is this panel's published answer, so a failure that
    // left `error` empty was indistinguishable from "the run has not answered
    // yet" — consumers can only test `valid || !error.isEmpty()`, and the
    // acceptance test waited out its full 60 s timeout on a catalog that had
    // already failed. The recorded error is always non-empty.
    const QString failure = oneLine(message);
    catalog_ = ModuleCatalog{};
    catalog_.error =
        failure.isEmpty()
            ? QCoreApplication::translate("VEdit", "the module catalog failed without a reason")
            : failure;
    roots_.clear();
    tree_->clear();
    setScrollableText(summary_, QString());
    setStatus(catalog_.error);
    notifySelection();
}

void ModuleTreePanel::setStatus(const QString &text)
{
    setScrollableText(status_, text);
}

void ModuleTreePanel::setScrollableText(QScrollArea *area, const QString &text)
{
    if (area == nullptr) {
        return;
    }
    auto *body = qobject_cast<QLabel *>(area->widget());
    if (body == nullptr) {
        return;
    }
    if (area == summary_) {
        summaryFull_ = text;
    } else if (area == status_) {
        statusFull_ = text;
    }
    // The FULL text stays one hover away (and is what summaryText()/statusText()
    // publish); the body shows that very text, VERBATIM - newlines preserved, no
    // elision - so a multi-thousand character warning is readable line by line and
    // copyable, and only the bar is needed for what does not fit.
    if (area->toolTip() != text) {
        area->setToolTip(text);
    }
    if (body->toolTip() != text) {
        body->setToolTip(text);
    }
    if (body->text() != text) {
        body->setText(text);
    }
    // widgetResizable is OFF, so the scroll range is the body's own size: give it
    // its unconstrained text size. Only the BODY grows; the container's height is
    // derived from the line count below and never from the text's length.
    const QSize wanted = body->sizeHint();
    if (body->size() != wanted) {
        body->resize(wanted);
    }
    const int lines =
        text.contains(QLatin1Char('\n')) ? kScrollableLinesMultiline : kScrollableLines;
    const int height = scrollableHeight(area, lines);
    if (area->height() != height) {
        area->setFixedHeight(height);
    }
}

void ModuleTreePanel::setRefreshHandler(std::function<void()> handler)
{
    refreshHandler_ = std::move(handler);
}

void ModuleTreePanel::setOnCancel(std::function<void()> handler)
{
    cancelHandler_ = std::move(handler);
}

void ModuleTreePanel::setCancelEnabled(bool enabled)
{
    catalogRunning_ = enabled;
    if (cancel_ != nullptr) {
        cancel_->setEnabled(catalogRunning_ || preparing_);
    }
}

void ModuleTreePanel::setPreparing(bool preparing)
{
    preparing_ = preparing;
    setCancelEnabled(catalogRunning_);
}

bool ModuleTreePanel::cancelEnabled() const
{
    return cancel_ != nullptr && cancel_->isEnabled();
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

QTreeWidgetItem *ModuleTreePanel::addRow(QTreeWidgetItem *parent, const ModuleTreeNode &node,
                                         bool descendants)
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

    if (descendants) {
        for (const ModuleTreeNode &child : node.children) {
            addRow(item, child);
        }
    }
    if (parent == nullptr) {
        tree_->addTopLevelItem(item);
    }
    return item;
}

void ModuleTreePanel::rebuildTree()
{
    // AC M1.8: the WHOLE tree is rebuilt on a catalog answer only. A selection or
    // an editor switch must never reach this function; the probe counts calls so
    // the latency test can assert exactly that.
    ++moduleTreeProbe().treeRebuilds;
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
    finishTree(previous);
}

void ModuleTreePanel::prepareTree()
{
    ++moduleTreeProbe().treeRebuilds;
    selectionBeforeBuild_ = selectedModule_;
    tree_->clear();
    roots_.clear();
    setPreparing(true);
    setStatus(QCoreApplication::translate("VEdit", "Preparing the module tree…"));
    const quint64 generation = treeGeneration_;
    auto result = std::make_shared<QVector<ModuleTreeNode>>();
    auto *worker = QThread::create([catalog = catalog_, result]() {
        *result = buildModuleTree(catalog);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, generation, result]() {
        if (generation == treeGeneration_) {
            roots_ = std::move(*result);
            for (auto it = roots_.crbegin(); it != roots_.crend(); ++it) {
                pendingRows_.append({nullptr, &*it});
            }
            appendPreparedRows(generation);
        }
    });
    worker->start();
}

void ModuleTreePanel::appendPreparedRows(quint64 generation)
{
    if (generation != treeGeneration_) {
        return;
    }
    QElapsedTimer batch;
    batch.start();
    do {
        if (pendingRows_.isEmpty()) {
            finishTree(selectionBeforeBuild_);
            return;
        }
        const PendingRow row = pendingRows_.takeLast();
        auto *item = addRow(row.parent, *row.node, false);
        if (row.parent != nullptr && row.parent->parent() == nullptr && row.parent->childCount() == 1) {
            row.parent->setExpanded(true);
        }
        for (auto child = row.node->children.crbegin(); child != row.node->children.crend(); ++child) {
            pendingRows_.append({item, &*child});
        }
    } while (batch.elapsed() < 4);
    QTimer::singleShot(0, this, [this, generation]() { appendPreparedRows(generation); });
}

void ModuleTreePanel::finishTree(const QString &previous)
{
    setPreparing(false);
    if (filter_ != nullptr && !filter_->text().trimmed().isEmpty()) {
        applyFilter(filter_->text());
    }
    if (!previous.isEmpty() && catalog_.find(previous) != nullptr) {
        for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
            if (QTreeWidgetItem *found = findModuleRow(tree_->topLevelItem(index), previous)) {
                tree_->setCurrentItem(found);
                break;
            }
        }
    }
    notifySelection();
    setStatus(QCoreApplication::translate(
        "VEdit", "Module tree ready — double-click a module to open its declaration."));
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

void ModuleTreePanel::setTopNote(const QString &text)
{
    // Stored only: setCatalog() appends it to the SUMMARY line, which is where
    // AC-7.3 wants the fallback explained. The transient status line belongs to
    // the run that is about to start.
    topNote_ = text;
}

void ModuleTreePanel::setTopEnabled(bool enabled)
{
    if (top_ == nullptr) {
        return;
    }
    top_->setEnabled(enabled);
    if (QLabel *label = findChild<QLabel *>(QStringLiteral("module-tree-top-label"))) {
        label->setEnabled(enabled);
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
    // AC M1.3: ONE gesture starts at most ONE catalog. Both signals stay
    // connected - `activated` covers the drop-down choice, `returnPressed` covers
    // a typed name the list does not know - but Qt answers a single Return press
    // on an editable combo by emitting BOTH (its own return handler emits
    // `activated`, then the line edit emits `returnPressed`). The twin arrives in
    // the same event delivery, so the flag is cleared on the next event-loop turn:
    // no wall-clock window, and a second deliberate gesture with the SAME value
    // still commits (which is how a scoped run is retried).
    if (sameGestureCommit_) {
        return;
    }
    sameGestureCommit_ = true;
    QMetaObject::invokeMethod(
        this, [this]() { sameGestureCommit_ = false; }, Qt::QueuedConnection);
    topHandler_(effectiveTop());
}

QString ModuleTreePanel::summaryText() const
{
    return summaryFull_;
}

QString ModuleTreePanel::statusText() const
{
    return statusFull_;
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
