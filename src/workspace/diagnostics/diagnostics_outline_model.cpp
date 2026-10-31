#include "diagnostics_outline_model.h"

namespace vedit::workspace::diagnostics {

OutlineModel::OutlineModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int OutlineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : symbols_.size();
}

int OutlineModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant OutlineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= symbols_.size()) {
        return QVariant();
    }
    const OutlineSymbol &symbol = symbols_.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:
            return symbol.name;
        case DetailColumn:
            return symbol.detail;
        case LineColumn:
            return symbol.line + 1;
        default:
            return QVariant();
        }
    }
    if (role == Qt::ToolTipRole && index.column() == NameColumn) {
        return QStringLiteral("%1 (kind %2) at line %3")
            .arg(symbol.name)
            .arg(symbol.kind)
            .arg(symbol.line + 1);
    }
    return QVariant();
}

QVariant OutlineModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case NameColumn:
        return QStringLiteral("Symbol");
    case DetailColumn:
        return QStringLiteral("Detail");
    case LineColumn:
        return QStringLiteral("Line");
    default:
        return QVariant();
    }
}

void OutlineModel::setSymbols(const QVector<OutlineSymbol> &symbols)
{
    beginResetModel();
    symbols_ = symbols;
    endResetModel();
}

} // namespace vedit::workspace::diagnostics
