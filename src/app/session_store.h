#pragma once

// Session persistence for the application shell.
//
// One JSON file per state directory holds everything the shell must restore on
// the next start: the split layout (pane tree + active pane), the inserted
// docks and their QMainWindow dock state, the window geometry, the theme and
// the project root. Malformed/unreadable files are reported and ignored — the
// shell must still start (bad-config robustness is a product requirement; the
// settings lane owns the user-facing diagnostics later).
#include <QJsonObject>
#include <QString>

namespace vedit::app {

class SessionStore {
public:
    explicit SessionStore(QString filePath);

    QString path() const;

    // Reads the session. A missing file returns an empty object with no error.
    QJsonObject load(QString *error) const;

    // Atomically writes the session (creates parent directories).
    bool save(const QJsonObject &object, QString *error) const;

    static QString defaultFilePath();

private:
    QString path_;
};

} // namespace vedit::app
