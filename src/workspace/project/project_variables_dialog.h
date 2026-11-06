#pragma once

// ADDENDUM A-9: the ONE dialog that collects every unresolved filelist variable
// after an import ("一个框要一堆，挨个设条目" — one dialog, many rows).
//
// It is deliberately dumb: the panel/model own the project variable table and
// the re-expansion. The dialog only maps one row per variable name to a line
// edit, pre-filled with the value already known from the project table, and
// hands back a `NAME=VALUE` list. Rows left empty are NOT written, so the
// variable stays unresolved and the next import asks again.
#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>

class QLineEdit;

namespace vedit::workspace::project {

class ProjectVariablesDialog : public QDialog {
    Q_OBJECT
public:
    ProjectVariablesDialog(const QStringList &names,
                           const QMap<QString, QString> &known,
                           QWidget *parent = nullptr);

    // One row per requested name, in that order.
    int rowCount() const;
    QStringList names() const;
    // Every row's current text, including empty ones.
    QMap<QString, QString> values() const;
    // The project variable table the dialog would write: `NAME=VALUE` for every
    // non-empty row, in row order. Empty rows are omitted (still unresolved).
    QStringList variableTable() const;

    void setValue(const QString &name, const QString &value);

    // "NAME=VALUE" list <-> map, shared with the panel/model tests.
    static QMap<QString, QString> parseTable(const QStringList &entries);
    static QStringList toTable(const QMap<QString, QString> &values);

private:
    QStringList names_;
    QMap<QString, QLineEdit *> edits_;
};

} // namespace vedit::workspace::project
