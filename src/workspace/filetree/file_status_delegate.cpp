#include "file_status_delegate.h"

#include "file_tree_model.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace vedit::workspace {

namespace {

// RM-7b: a disabled row's dot must dim exactly like the rest of the row. The
// same rule the theme palette uses for its Disabled group: move the semantic
// colour 55% of the way toward the surface it is painted on. Keeping the hue
// (red/amber/green/grey) means the status stays readable, only muted.
constexpr int kDisabledDotBlendPercent = 55;

QColor blendToward(const QColor &color, const QColor &surface, int percent)
{
    const qreal weight = qBound(0, percent, 100) / 100.0;
    return QColor(qRound(color.red() * (1.0 - weight) + surface.red() * weight),
                  qRound(color.green() * (1.0 - weight) + surface.green() * weight),
                  qRound(color.blue() * (1.0 - weight) + surface.blue() * weight));
}

} // namespace

FileStatusDotDelegate::FileStatusDotDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QColor FileStatusDotDelegate::dotColor(int status)
{
    // ONE definition: src/common/file_status.h (shared with the Project panel's
    // Files list and with the controller's status vocabulary).
    return vedit::common::fileStatusColor(status);
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
    // RM-7b: honour the row's enabled state for the hand-painted dot. Before
    // this, a disabled row kept a full-strength dot while its icon and text
    // dimmed (the RM-9 states are unchanged: the ring shape below still uses
    // this colour, only dimmed when the row is disabled).
    QColor paintedDot = dotColor(status);
    if ((opt.state & QStyle::State_Enabled) == 0) {
        const QColor surface = (opt.state & QStyle::State_Selected)
                                   ? opt.palette.color(QPalette::Highlight)
                                   : opt.palette.color(QPalette::Base);
        paintedDot = blendToward(paintedDot, surface, kDisabledDotBlendPercent);
    }
    lastDotColor_ = paintedDot;
    ++paintedDots_;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (vedit::common::fileStatusIsNotAnalyzed(status)) {
        // Hollow ring: this file is in the analysed set but has no trustworthy
        // verdict YET (queued / pass running) or was excluded (dirty buffer).
        // Grey and hollow, so it can never be mistaken for the filled green dot -
        // the user's complaint was precisely that "nothing" read as broken.
        QPen pen(lastDotColor_);
        pen.setWidthF(2.0); // opaque enough for the ring to be sampled by the evidence
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(dotRect);
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(lastDotColor_);
        painter->drawEllipse(dotRect);
    }
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
