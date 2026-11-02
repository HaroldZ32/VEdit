#pragma once

// Data model of the project panel (workspace lane, Slice 1 / task T6).
//
// `ProjectState` is one resolved project: the root the Rust service expanded,
// the members (files), the include directories and defines it contributes, the
// diagnostics it reported, and when the service answered. It is also the
// persisted form: `<state dir>/project-model.json` survives a restart, so the
// project and its members are still listed before (or even without) the service
// answering again — acceptance AC-S1.3 "重启后工程与成员仍在".
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace vedit::workspace::project {

struct ProjectState {
    QString root;               // empty => no project has been resolved
    QStringList files;          // absolute paths, filelist order, duplicates removed
    QStringList includeDirs;    // absolute paths from `+incdir+` / configuration
    QStringList defines;        // `NAME` or `NAME=VALUE`
    QStringList diagnostics;    // structured warnings rendered by the Rust service
    QString capturedAt;         // ISO-8601 UTC of the last live expansion

    bool isValid() const;
    QJsonObject toJson() const;
    // Tolerant by design: a corrupt or older file must not stop the panel from
    // starting; whatever can be read is used and the rest stays empty.
    static ProjectState fromJson(const QJsonObject &object);
    static QString nowUtc();
};

// Persists exactly one ProjectState under a state directory.
class ProjectStateStore {
public:
    explicit ProjectStateStore(QString directory);

    // `<directory>/project-model.json`; an empty directory resolves to the
    // platform application-config location, so the panel never writes to the
    // current working directory.
    QString path() const;

    // `ok` is true when a readable state file with a non-empty root was loaded.
    ProjectState load(bool *ok, QString *error = nullptr) const;
    // Atomic write (temp file + rename); the parent directory is created.
    bool save(const ProjectState &state, QString *error = nullptr) const;

    static QString fileName();

private:
    QString directory_;
};

} // namespace vedit::workspace::project
