#include "interface_panel.h"

#include "jump/jump_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

namespace vedit::workspace::module_tree {

namespace {

constexpr int kPortIndexRole = Qt::UserRole;

QString oneLine(QString text)
{
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.simplified();
}

/// The table's double-click event carries the keyboard modifiers; the
/// QTableWidget::itemDoubleClicked signal does not, and Ctrl+double-click must
/// mean "jump to the instantiation" rather than "jump to the declaration".
class PortTable final : public QTableWidget {
public:
    explicit PortTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

    std::function<void(int row, bool control)> onDoubleClick;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->pos());
        if (index.isValid() && onDoubleClick != nullptr) {
            onDoubleClick(index.row(),
                          event->modifiers().testFlag(Qt::ControlModifier));
            return;
        }
        QTableWidget::mouseDoubleClickEvent(event);
    }
};

} // namespace

QString InterfacePanel::activationTooltip()
{
    return QCoreApplication::translate(
        "VEdit",
        "Double-click a port: jump to its declaration. Ctrl+double-click: jump to this module's "
        "instantiation statement. The module catalog has no per-connection position, so "
        "Ctrl+double-click targets the instance statement, not the connected port.");
}

InterfacePanel::InterfacePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("interface-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    heading_ = new QLabel(this);
    heading_->setObjectName(QStringLiteral("interface-heading"));
    heading_->setWordWrap(true);
    heading_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading_);

    auto *table = new PortTable(this);
    table_ = table;
    table_->setObjectName(QStringLiteral("interface-table"));
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QCoreApplication::translate("VEdit", "Name"),
         QCoreApplication::translate("VEdit", "Direction"),
         QCoreApplication::translate("VEdit", "Type"),
         QCoreApplication::translate("VEdit", "Width"),
         QCoreApplication::translate("VEdit", "Location")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->setToolTip(activationTooltip());
    heading_->setToolTip(activationTooltip());
    table->onDoubleClick = [this](int row, bool control) { activatePortRow(row, control); };
    layout->addWidget(table_, 1);

    empty_ = new QLabel(this);
    empty_->setObjectName(QStringLiteral("interface-empty"));
    empty_->setWordWrap(true);
    layout->addWidget(empty_);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("interface-status"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    setStatus(QCoreApplication::translate(
        "VEdit", "Double-click a port for its declaration; Ctrl+double-click for the "
                 "instantiation statement."));
    clearTable(QCoreApplication::translate(
        "VEdit", "No module catalog yet: open a project and press Refresh."));
}

void InterfacePanel::setCatalog(const ModuleCatalog &catalog)
{
    catalog_ = catalog;
    refreshFocus();
}

void InterfacePanel::setNoProject(const QString &reason)
{
    catalog_ = ModuleCatalog{};
    clearTable(oneLine(reason));
}

void InterfacePanel::setFailure(const QString &message)
{
    catalog_ = ModuleCatalog{};
    clearTable(oneLine(message));
}

void InterfacePanel::setTreeSelection(const QString &moduleName)
{
    treeSelection_ = moduleName;
    refreshFocus();
}

void InterfacePanel::setCaretContext(const QString &file, int caretLine0)
{
    caretFile_ = file;
    caretLine0_ = caretLine0;
    refreshFocus();
}

void InterfacePanel::setInstantiationChooser(
    std::function<int(const QVector<InstantiationSite> &)> chooser)
{
    chooser_ = std::move(chooser);
}

void InterfacePanel::refreshFocus()
{
    if (!catalog_.valid || !catalog_.error.isEmpty()) {
        clearTable(catalog_.error.isEmpty()
                       ? QCoreApplication::translate(
                             "VEdit",
                             "No module catalog yet: open a project and press Refresh.")
                       : oneLine(catalog_.error));
        return;
    }
    if (catalog_.modules.isEmpty()) {
        clearTable(QCoreApplication::translate(
            "VEdit", "No modules found: slang reported no module definitions for this project."));
        return;
    }
    const ModuleDefinition *module = nullptr;
    if (!treeSelection_.isEmpty()) {
        module = catalog_.find(treeSelection_);
    }
    if (module == nullptr && caretLine0_ >= 0 && !caretFile_.isEmpty()) {
        module = moduleAtCaret(catalog_, caretFile_, caretLine0_);
    }
    if (module == nullptr) {
        clearTable(QCoreApplication::translate(
            "VEdit", "No focused module — select a module in the Module Tree, or put the caret "
                     "inside a module definition."));
        return;
    }
    focusedName_ = module->name;
    emptyText_.clear();
    empty_->hide();
    table_->show();
    heading_->setText(QCoreApplication::translate("VEdit", "Module %1 · %2 port(s)")
                          .arg(module->name)
                          .arg(module->ports.size()));
    renderPorts();
}

void InterfacePanel::clearTable(const QString &reason)
{
    focusedName_.clear();
    emptyText_ = reason;
    if (table_ != nullptr) {
        table_->clearContents();
        table_->setRowCount(0);
        table_->hide();
    }
    if (heading_ != nullptr) {
        heading_->clear();
    }
    if (empty_ != nullptr) {
        empty_->setText(reason);
        empty_->show();
    }
}

void InterfacePanel::renderPorts()
{
    const ModuleDefinition *module = catalog_.find(focusedName_);
    if (module == nullptr || table_ == nullptr) {
        return;
    }
    table_->clearContents();
    table_->setRowCount(module->ports.size());
    for (int row = 0; row < module->ports.size(); ++row) {
        const ModulePort &port = module->ports.at(row);
        auto *nameItem = new QTableWidgetItem(port.name);
        nameItem->setData(kPortIndexRole, row);
        table_->setItem(row, 0, nameItem);
        table_->setItem(row, 1, new QTableWidgetItem(port.direction));
        table_->setItem(row, 2, new QTableWidgetItem(port.type));

        const PortWidth width = portWidthFromType(port.type);
        auto *widthItem = new QTableWidgetItem(width.known ? QString::number(width.width)
                                                           : QString());
        widthItem->setToolTip(
            width.known
                ? QCoreApplication::translate("VEdit", "Packed width derived from `%1`")
                      .arg(port.type)
                : width.problem);
        table_->setItem(row, 3, widthItem);

        const QString position = describePosition(port.file, port.line, port.column);
        auto *locationItem =
            new QTableWidgetItem(position == QLatin1String("-") ? QString() : position);
        locationItem->setToolTip(position);
        table_->setItem(row, 4, locationItem);

        nameItem->setToolTip(
            QCoreApplication::translate(
                "VEdit",
                "%1 · double-click: jump to the port declaration; Ctrl+double-click: jump "
                "to this module's instantiation statement")
                .arg(port.name));
    }
}

bool InterfacePanel::activatePortRow(int row, bool control)
{
    const ModuleDefinition *module = catalog_.find(focusedName_);
    if (module == nullptr || table_ == nullptr || row < 0) {
        return false;
    }
    const QTableWidgetItem *nameItem = table_->item(row, 0);
    if (nameItem == nullptr) {
        return false;
    }
    const int portIndex = nameItem->data(kPortIndexRole).toInt();
    if (portIndex < 0 || portIndex >= module->ports.size()) {
        return false;
    }
    if (control) {
        return jumpToInstantiation();
    }
    const ModulePort &port = module->ports.at(portIndex);
    const ModuleJump jump = jumpFromPort(port);
    if (!jump.isValid()) {
        ++jumpsSkipped_;
        setStatus(QCoreApplication::translate(
                      "VEdit", "port %1 has no declaration position in the catalog, so "
                               "nothing was opened")
                      .arg(port.name));
        return false;
    }
    return deliver(jump, QCoreApplication::translate("VEdit", "port %1 declaration").arg(port.name));
}

bool InterfacePanel::jumpToInstantiation()
{
    const QVector<InstantiationSite> candidates = instantiationsOf(catalog_, focusedName_);
    lastCandidates_ = candidates;
    lastChooserCandidates_ = candidates.size();
    if (candidates.isEmpty()) {
        ++jumpsSkipped_;
        setStatus(QCoreApplication::translate(
                      "VEdit",
                      "no instantiation of %1 was catalogued: it is a design top, so there is "
                      "no instance statement to jump to")
                      .arg(focusedName_));
        return false;
    }
    int index = 0;
    if (candidates.size() > 1) {
        if (chooser_ != nullptr) {
            index = chooser_(candidates);
        } else {
            QMenu menu(this);
            menu.setObjectName(QStringLiteral("interface-instantiation-chooser"));
            for (int candidate = 0; candidate < candidates.size(); ++candidate) {
                const InstantiationSite &site = candidates.at(candidate);
                QAction *action = menu.addAction(
                    QCoreApplication::translate("VEdit", "%1 · %2 · %3")
                        .arg(site.parentModule, site.instance.path,
                             describePosition(site.instance.file, site.instance.line,
                                              site.instance.column)));
                action->setData(candidate);
            }
            QAction *chosen = menu.exec(QCursor::pos());
            if (chosen == nullptr) {
                ++jumpsSkipped_;
                setStatus(QCoreApplication::translate("VEdit", "instantiation chooser cancelled"));
                return false;
            }
            index = chosen->data().toInt();
        }
        if (index < 0 || index >= candidates.size()) {
            ++jumpsSkipped_;
            setStatus(QCoreApplication::translate("VEdit", "instantiation chooser cancelled"));
            return false;
        }
    }
    const InstantiationSite &site = candidates.at(index);
    const ModuleJump jump = jumpFromInstance(site.instance);
    if (!jump.isValid()) {
        ++jumpsSkipped_;
        setStatus(QCoreApplication::translate(
                      "VEdit", "the instantiation in %1 has no source position in the catalog")
                      .arg(site.parentModule));
        return false;
    }
    return deliver(jump, QCoreApplication::translate("VEdit", "instantiation of %1 in %2")
                            .arg(focusedName_, site.parentModule));
}

bool InterfacePanel::deliver(const ModuleJump &jump, const QString &description)
{
    if (!jump.isValid()) {
        // Backstop for a caller that reached deliver() without a usable position:
        // it must say WHY (what the catalog reported), exactly like the refusal
        // paths below - a status line with only the description told the user and
        // the team nothing (RM-4).
        ++jumpsSkipped_;
        setStatus(QCoreApplication::translate(
                      "VEdit",
                      "%1: the catalog reports no usable position (file=%2, line=%3, column=%4), "
                      "so nothing was opened")
                      .arg(description,
                           jump.file.isEmpty()
                               ? QCoreApplication::translate("VEdit", "<none>")
                               : jump.file)
                      .arg(jump.line)
                      .arg(jump.column));
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
        setStatus(QCoreApplication::translate("VEdit", "%1 → %2").arg(description, target));
        return true;
    case jump::JumpStatus::NoTarget:
        setStatus(QCoreApplication::translate(
                      "VEdit",
                      "%1: no jump target is registered (the editor lane is not installed), "
                      "so nothing was opened")
                      .arg(description));
        return false;
    case jump::JumpStatus::Rejected:
        // RM-4: the WHY travels with the refusal (jump::JumpResult::reason), so
        // this is never the bare "the editor rejected the jump to X" that hid
        // every distinct cause behind one sentence. A refusal is still a
        // refusal: nothing is retried and nothing is silently swallowed.
        setStatus(QCoreApplication::translate("VEdit",
                                              "%1: the editor rejected the jump to %2 — %3")
                      .arg(description, target,
                           result.reason.isEmpty()
                               ? QCoreApplication::translate("VEdit", "no reason was reported")
                               : result.reason));
        return false;
    case jump::JumpStatus::InvalidRequest:
        setStatus(QCoreApplication::translate("VEdit", "%1: the jump request (%2) was refused — %3")
                      .arg(description, target,
                           result.reason.isEmpty()
                               ? QCoreApplication::translate("VEdit", "no reason was reported")
                               : result.reason));
        return false;
    }
    return false;
}

void InterfacePanel::setStatus(const QString &text)
{
    statusText_ = text;
    if (status_ != nullptr) {
        status_->setText(text);
    }
}

QTableWidget *InterfacePanel::table() const
{
    return table_;
}

QString InterfacePanel::focusedModuleName() const
{
    return focusedName_;
}

const ModuleDefinition *InterfacePanel::focusedModule() const
{
    return catalog_.find(focusedName_);
}

QString InterfacePanel::emptyText() const
{
    return emptyText_;
}

QString InterfacePanel::statusText() const
{
    return statusText_;
}

int InterfacePanel::jumpAttempts() const
{
    return jumpAttempts_;
}

int InterfacePanel::jumpsDelivered() const
{
    return jumpsDelivered_;
}

int InterfacePanel::jumpsSkipped() const
{
    return jumpsSkipped_;
}

int InterfacePanel::lastChooserCandidates() const
{
    return lastChooserCandidates_;
}

QVector<InstantiationSite> InterfacePanel::lastCandidates() const
{
    return lastCandidates_;
}

} // namespace vedit::workspace::module_tree
