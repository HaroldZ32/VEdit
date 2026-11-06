#pragma once

// ADDENDUM A-9 + RM-3: the ONE dialog for the project variable table.
//
// A-9: after an import the dialog collects every UNRESOLVED filelist variable in
// one pass ("一个框要一堆，挨个设条目").
// RM-3: the real-machine report was that the dialog only allowed EDITING the
// names the project already knew — "不能手动加、删". It now supports adding a new
// NAME + value and deleting a row, and it always shows the WHOLE table (the
// known names plus the requested ones), so the caller can REPLACE the model's
// table with `variableTable()` instead of appending to it.
//
// Two rules are stated in the dialog's own text and enforced here:
//   * validation is INLINE, never a modal or a silent ignore: a row with an
//     empty / malformed / duplicate name shows its reason next to the row and
//     the dialog does not close until it is fixed (or the row is deleted);
//   * deleting a variable removes its value. A name a filelist still references
//     then becomes unresolved again — the project reports it missing and this
//     dialog asks for it on the next expansion, so a required name can never
//     vanish silently.
//
// The dialog is deliberately dumb about the model: the panel/model own the
// table and the re-expansion; this class only maps rows to `NAME=VALUE`.
#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace vedit::workspace::project {

class ProjectVariablesDialog : public QDialog {
    Q_OBJECT
public:
    ProjectVariablesDialog(const QStringList &names,
                           const QMap<QString, QString> &known,
                           QWidget *parent = nullptr);

    // One row per distinct name: the requested order first, then any known name
    // the caller did not list. Add/delete change this count while the dialog is
    // open.
    int rowCount() const;
    // The row names in order, trimmed; an added row is "" until a name is typed.
    QStringList names() const;
    // Every named row's current text (rows without a name are skipped).
    QMap<QString, QString> values() const;
    // The project variable table this dialog would write: `NAME=VALUE` for every
    // row with a VALID name and a non-empty value, in row order. An emptied row
    // is omitted, which is how "clear the value" deletes an entry.
    QStringList variableTable() const;

    void setValue(const QString &name, const QString &value);

    // RM-3 seams — the Add / Delete buttons call exactly these, and the
    // regression drives them without a human. `addVariable` appends a row and
    // returns whether that row is valid; `removeVariable` deletes the row.
    bool addVariable(const QString &name, const QString &value);
    bool removeVariable(const QString &name);
    // The inline reason for a row ("" when the row is valid).
    QString rowReason(const QString &name) const;
    // Paint every row's reason; false means "do not accept".
    bool validateRows();
    // The feedback line under the table ("Removed X …"), for probes/tests.
    QString note() const;

    // "NAME=VALUE" list <-> map, shared with the panel/model tests.
    static QMap<QString, QString> parseTable(const QStringList &entries);
    static QStringList toTable(const QMap<QString, QString> &values);
    // A NAME that can be substituted into a filelist path: letters, digits and
    // '_', not starting with a digit. This is the ONE rule the dialog and the
    // tests share.
    static bool isValidName(const QString &name);

private:
    struct Row {
        QWidget *widget = nullptr;
        QLineEdit *nameEdit = nullptr;
        QLineEdit *valueEdit = nullptr;
        QPushButton *removeButton = nullptr;
        QLabel *reason = nullptr;
        bool required = false; ///< the project asked for it (currently missing)
    };

    void appendRow(const QString &name, const QString &value, bool required);
    void removeRowAt(int index);
    Row *rowFor(const QString &name);
    const Row *rowFor(const QString &name) const;
    void updateNote(const QString &text);
    QStringList allRowNames() const;

    QVector<Row> rows_;
    QStringList knownNames_;
    QLabel *note_ = nullptr;
    QPushButton *addButton_ = nullptr;
};

} // namespace vedit::workspace::project
