#include "file_status_delegate.h"

#include "file_tree_model.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace vedit::workspace {

FileStatusDotDelegate::FileStatusDotDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QColor FileStatusDotDelegate::dotColor(int status)
{
    switch (status) {
    case Error:
        return QColor(0xd3, 0x2f, 0x2f); // red
    case Warning:
        return QColor(0xf9, 0xa8, 0x25); // yellow/amber
    case Clean:
        return QColor(0x2e, 0x7d, 0x32); // green
    default:
        return QColor(Qt::transparent); // unknown: no dot at all
    }
}

QSize FileStatusDotDelegate::sizeHint(const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(FileTreeModel::FileStatusRole).toInt() > 0) {
        size.setWidth(size.width() + DotDiameter + 2 * DotGap);
    }
    return size;
}

void FileStatusDotDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    const int status = index.data(FileTreeModel::FileStatusRole).toInt();
    if (status <= 0) {
        // No verdict for this row (a folder, or a file nothing has linted):
        // the default rendering, with no dot that could be mistaken for clean.
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
    const QIcon icon = opt.icon;
    const QString text = opt.text;

    // 1) background / selection / focus, without icon and text.
    QStyleOptionViewItem background = opt;
    background.icon = QIcon();
    background.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, opt.widget);

    // 2) the geometry the style would use for the decoration and the text.
    QStyleOptionViewItem geometry = opt;
    const QRect iconRect =
        style->subElementRect(QStyle::SE_ItemViewItemDecoration, &geometry, opt.widget);
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &geometry, opt.widget);

    if (!icon.isNull()) {
        const QIcon::Mode mode =
            (opt.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled;
        const QIcon::State state = (opt.state & QStyle::State_Open) ? QIcon::On : QIcon::Off;
        icon.paint(painter, iconRect, Qt::AlignCenter, mode, state);
    }

    // 3) the dot, in the gap between the icon and the name.
    const int centreX = iconRect.right() + DotGap + DotDiameter / 2;
    const int centreY = opt.rect.center().y();
    const QRect dotRect(centreX - DotDiameter / 2, centreY - DotDiameter / 2, DotDiameter,
                        DotDiameter);
    lastDotRect_ = dotRect;
    lastDotColor_ = dotColor(status);
    ++paintedDots_;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(lastDotColor_);
    painter->drawEllipse(dotRect);
    painter->restore();

    // 4) the name, shifted right by the dot so the two never overlap.
    QRect shifted = textRect;
    shifted.setLeft(dotRect.right() + DotGap + 1);
    lastTextRect_ = shifted;
    if (shifted.width() > 0) {
        const QString elided =
            opt.fontMetrics.elidedText(text, opt.textElideMode, shifted.width());
        const QPalette::ColorRole role = (opt.state & QStyle::State_Selected)
            ? QPalette::HighlightedText
            : QPalette::Text;
        style->drawItemText(painter, shifted, opt.displayAlignment | Qt::AlignVCenter, opt.palette,
                            (opt.state & QStyle::State_Enabled) != 0, elided, role);
    }
}

} // namespace vedit::workspace
