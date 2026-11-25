#pragma once

#include "search_query.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

namespace vedit::search {

inline QToolButton *createHistoryButton(QLineEdit *edit, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    const QString label = QCoreApplication::translate("VEdit", "Recent Searches");
    button->setAccessibleName(label);
    button->setToolTip(label);
    button->setArrowType(Qt::DownArrow);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(button);
    menu->setAccessibleName(label);
    menu->setObjectName(edit->objectName() + QStringLiteral("-history-menu"));
    button->setObjectName(edit->objectName() + QStringLiteral("-history"));
    button->setMenu(menu);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, edit]() {
        menu->clear();
        for (const QString &query : SearchHub::instance().history()) {
            QString text = menu->fontMetrics().elidedText(query, Qt::ElideMiddle, 320);
            text.replace(QLatin1Char('&'), QStringLiteral("&&"));
            QAction *item = menu->addAction(text, edit, [edit, query]() {
                edit->setText(query);
                edit->setFocus(Qt::OtherFocusReason);
                edit->selectAll();
            });
            item->setToolTip(query);
        }
        if (menu->isEmpty()) {
            menu->addAction(QCoreApplication::translate("VEdit", "No Recent Searches"))
                ->setEnabled(false);
        }
    });
    return button;
}

} // namespace vedit::search
