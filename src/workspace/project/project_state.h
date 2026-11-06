#pragma once

// Data model of the project panel (workspace lane, task T6).
//
// `ProjectState` is one resolved project: the root the Rust service expanded,
// the imported `.f` filelists, the members (files), the include directories and
// defines it contributes, the diagnostics it reported, and when the service
// answered. It is also the persisted form: `<state dir>/project-model.json`
// survives a restart, so the project, its imported filelists and its members are
// still listed before (or even without) the service answering again —
// acceptance AC-2.4 "重启后根目录、导入的 .f 列表、-I/-D、展开结果与关前一致".
//
// RM-3: `variablesByRoot` is the per-root memory of the project variable table.
// The live `variables` field belongs to `root`; the map keeps what the user typed
// for OTHER roots, so a switch A -> B -> A brings A's values back instead of
// losing them with the live state (the ProjectModel wipe). It is additive in the
// same file (`version` 3); a version-2 file simply loads with an empty map and
// the model seeds the entry for its own root.
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace vedit::workspace::project {

struct ProjectState {
    QString root;               // empty => no project has been resolved
    QStringList filelists;      // imported .f files, absolute, import order
    QStringList variables;      // ADDENDUM A-9: `NAME=VALUE`, project table (wins over env)
    // RM-3: the table typed for each root, keyed by the ROOT the user opened.
    // `variables` is the entry of `root` (kept in sync by the model); the other
    // entries are what a switch back must restore.
    QMap<QString, QStringList> variablesByRoot;
    // RM-17: the module-tree TOP the user named for this project (module NAME;
    // empty = Auto = every design root). `top` is the entry of `root`; the map
    // remembers the other roots so A -> B -> A restores A's top.
    QString top;
    QMap<QString, QString> topByRoot;
    QStringList files;          // absolute paths, filelist order, duplicates removed
    QStringList includeDirs;    // absolute `-I` paths (project + tools page)
    QStringList defines;        // `-D` defines, `NAME` or `NAME=VALUE`
    QStringList diagnostics;    // `.f`/scan/service problems, phrased for the UI
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
