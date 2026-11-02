#pragma once

// Outline table model fed by `textDocument/documentSymbol` responses.
// Kept independent of the panel widget so the headless self-test asserts the
// outline entries without a display.

#include "diagnostics_types.h"

#include <QAbstractTableModel>
#include <QVector>

namespace vedit::workspace::diagnostics {

class OutlineModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        NameColumn = 0,
        DetailColumn = 1,
        LineColumn = 2,
        ColumnCount = 3,
    };

    explicit OutlineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setSymbols(const QVector<OutlineSymbol> &symbols);
    const QVector<OutlineSymbol> &symbols() const { return symbols_; }

private:
    QVector<OutlineSymbol> symbols_;
};

} // namespace vedit::workspace::diagnostics
