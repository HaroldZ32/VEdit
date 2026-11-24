#include "trace_dialog.h"

#include "../jump/jump_registry.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace vedit::workspace::signal_trace {
namespace {
constexpr int kLocationRole = Qt::UserRole;

QString connectionText(const QString &detail)
{
    static const QRegularExpression port(QStringLiteral(R"(^Port (.+) \((In|Out|InOut)\)$)"));
    const auto match = port.match(detail);
    if (match.hasMatch()) {
        const QString direction = match.captured(2);
        const QString label = direction == QLatin1String("In") ? QCoreApplication::translate("SignalTrace", "In")
            : direction == QLatin1String("Out") ? QCoreApplication::translate("SignalTrace", "Out")
            : QCoreApplication::translate("SignalTrace", "InOut");
        return QCoreApplication::translate("SignalTrace", "Port %1 (%2)").arg(match.captured(1), label);
    }
    static const QHash<QString, const char *> labels{
        {QStringLiteral("assign"), QT_TRANSLATE_NOOP("SignalTrace", "assign")},
        {QStringLiteral("procedural"), QT_TRANSLATE_NOOP("SignalTrace", "procedural")},
        {QStringLiteral("Always"), QT_TRANSLATE_NOOP("SignalTrace", "Always")},
        {QStringLiteral("AlwaysComb"), QT_TRANSLATE_NOOP("SignalTrace", "AlwaysComb")},
        {QStringLiteral("AlwaysFF"), QT_TRANSLATE_NOOP("SignalTrace", "AlwaysFF")},
        {QStringLiteral("AlwaysLatch"), QT_TRANSLATE_NOOP("SignalTrace", "AlwaysLatch")},
        {QStringLiteral("Initial"), QT_TRANSLATE_NOOP("SignalTrace", "Initial")},
        {QStringLiteral("Final"), QT_TRANSLATE_NOOP("SignalTrace", "Final")},
    };
    const char *source = labels.value(detail);
    return source != nullptr ? QCoreApplication::translate("SignalTrace", source) : detail;
}

QTreeWidgetItem *addRow(QTreeWidgetItem *parent, const QJsonObject &node, bool descend)
{
    auto *row = new QTreeWidgetItem(parent);
    const auto source = node.value(QStringLiteral("location")).toObject();
    const Location location = locationOf(source);
    row->setText(0, node.value(QStringLiteral("path")).toString());
    QString detail = connectionText(node.value(QStringLiteral("detail")).toString());
    if (node.value(QStringLiteral("cycle")).toBool()) {
        detail += QCoreApplication::translate("VEdit", " · Cycle");
    }
    row->setText(1, detail);
    if (!location.file.isEmpty()) {
        row->setText(2, QStringLiteral("%1:%2:%3").arg(QFileInfo(location.file).fileName())
                        .arg(location.line + 1).arg(location.column + 1));
        row->setToolTip(2, location.file);
        row->setData(0, kLocationRole, source.toVariantMap());
    }
    if (descend) {
        for (const auto &child : node.value(QStringLiteral("children")).toArray()) {
            addRow(row, child.toObject(), true);
        }
    }
    return row;
}
}

Location locationOf(const QJsonObject &object)
{
    return {object.value(QStringLiteral("file")).toString(),
            object.value(QStringLiteral("line")).toInt(), object.value(QStringLiteral("column")).toInt()};
}

void jumpTo(QWidget *host, const Location &location)
{
    if (location.file.isEmpty()) {
        return;
    }
    const auto result = jump::jumpTo(location.file, location.line + 1, location.column + 1);
    if (result.status != jump::JumpStatus::Delivered) {
        if (auto *window = qobject_cast<QMainWindow *>(host)) {
            window->statusBar()->showMessage(result.reason, 6000);
        }
    }
}

QDialog *showResults(QWidget *host, const QString &title, const QJsonObject &answer, bool full)
{
    auto *dialog = new QDialog(host);
    dialog->setObjectName(QStringLiteral("signal-trace-results"));
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(760, 440);
    auto *layout = new QVBoxLayout(dialog);
    const QString warning = answer.value(QStringLiteral("warning")).toString();
    if (!warning.isEmpty() || answer.value(QStringLiteral("truncated")).toBool()) {
        auto *notice = new QLabel(dialog);
        notice->setWordWrap(true);
        notice->setText(answer.value(QStringLiteral("truncated")).toBool()
                            ? QCoreApplication::translate("VEdit", "Trace limited to 512 rows and 32 levels.") + QLatin1Char(' ') + warning
                            : warning);
        layout->addWidget(notice);
    }
    auto *tree = new QTreeWidget(dialog);
    tree->setHeaderLabels({QCoreApplication::translate("VEdit", "Signal instance"),
                           QCoreApplication::translate("VEdit", "Connection"),
                           QCoreApplication::translate("VEdit", "Source")});
    tree->header()->setStyleSheet(QStringLiteral("QHeaderView::section { color: palette(text); }"));
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->setUniformRowHeights(true);
    layout->addWidget(tree);
    QTreeWidgetItem *first = nullptr;
    for (const auto &match : answer.value(QStringLiteral("roots")).toArray()) {
        const auto root = match.toObject();
        auto *group = addRow(tree->invisibleRootItem(), root, full);
        if (!full) {
            for (const auto &child : root.value(QStringLiteral("children")).toArray()) {
                addRow(group, child.toObject(), false);
            }
        }
        if (first == nullptr && group->childCount() > 0) {
            first = group->child(0);
        }
        group->setExpanded(true);
    }
    if (full) {
        tree->expandAll();
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    auto *open = buttons->addButton(QCoreApplication::translate("VEdit", "Jump to Source"), QDialogButtonBox::ActionRole);
    open->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    const auto location = [](QTreeWidgetItem *item) {
        return item != nullptr
            ? locationOf(QJsonObject::fromVariantMap(item->data(0, kLocationRole).toMap()))
            : Location{};
    };
    const auto activate = [=](QTreeWidgetItem *item) {
        const Location target = location(item);
        if (!target.file.isEmpty()) {
            if (!full) {
                dialog->close();
            }
            jumpTo(host, target);
        }
    };
    QObject::connect(tree, &QTreeWidget::itemActivated, dialog, activate);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog,
                     [=](QTreeWidgetItem *item) { open->setEnabled(!location(item).file.isEmpty()); });
    QObject::connect(open, &QPushButton::clicked, dialog, [=]() { activate(tree->currentItem()); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    open->setEnabled(false);
    tree->setCurrentItem(first);
    tree->setFocus();
    dialog->show();
    return dialog;
}
}
