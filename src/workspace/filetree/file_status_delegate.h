#pragma once

// item 10: the file-tree status dot.
//
// The tree has ONE column and no delegate; the dot must sit BETWEEN the icon
// and the file name, so it cannot be a nicer icon and it must not be part of
// the text. This delegate draws the row itself:
//
//   [icon]  ●  name
//
//   red    = the merged diagnostics view (R2) has an error in this file
//   yellow = warnings only
//   green  = clean (the project pass covered it and found nothing, or only
//            information/hint rows)
//   no dot = nothing ever reported this file (an unknown file is not "clean")
//
// The status comes from FileTreeModel::FileStatusRole, i.e. from the SAME
// merged view the editor and the panel read; the delegate never rescans the
// project.

#include <QColor>
#include <QStyledItemDelegate>

class QPainter;

namespace vedit::workspace {

class FileStatusDotDelegate final : public QStyledItemDelegate {
public:
    /// Status values of FileTreeModel::FileStatusRole.
    enum Status {
        Unknown = 0,
        Error = 1,
        Warning = 2,
        Clean = 3,
    };

    explicit FileStatusDotDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// The dot colour of one status (transparent for Unknown). The evidence
    /// reads exactly this function.
    static QColor dotColor(int status);

    /// The geometry of the last painted dot and its colour - the self-test
    /// samples the rendered pixel there (a real "the dot is drawn" proof).
    QRect lastDotRect() const { return lastDotRect_; }
    QColor lastDotColor() const { return lastDotColor_; }
    /// Where the file NAME was drawn (the dot's right edge is left of it: the
    /// ordering icon < dot < name, which item 10 asks for).
    QRect lastTextRect() const { return lastTextRect_; }
    /// Number of dots painted since construction (observability).
    int paintedDots() const { return paintedDots_; }

    static constexpr int DotDiameter = 9;
    static constexpr int DotGap = 4;

private:
    mutable QRect lastDotRect_;
    mutable QRect lastTextRect_;
    mutable QColor lastDotColor_;
    mutable int paintedDots_ = 0;
};

} // namespace vedit::workspace
