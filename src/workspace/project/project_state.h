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
// AC-1.5 (batch 20261110): `projectIncludeDirs` / `projectDefines` are the user's
// editable project entries (version 5). A file written by an earlier build simply
// has no such keys and loads with EMPTY entries — the tolerant-load rule.
//
// RM-3: `variablesByRoot` is the per-root memory of the project variable table.
// The live `variables` field belongs to `root`; the map keeps what the user typed
// for OTHER roots, so a switch A -> B -> A brings A's values back instead of
// losing them with the live state (the ProjectModel wipe). It is additive in the
// same file (`version` 3); a version-2 file simply loads with an empty map and
// the model seeds the entry for its own root.
//
// G1-e (batch-20261119): version 6 changed what `files` MEANS. Up to version 5 it
// was whatever the expansion produced at the time - on a build with
// `[project] auto_discover = true` that was a folder SCAN, not the declared
// filelist set - so an old file's `files` may describe a set this build must not
// adopt. `fromJson` therefore reads `version` and, below 6, keeps everything that
// is still authored input (filelists / top / includes / defines / variables) while
// dropping `files` and raising `filesStale`: the catalog then waits for ONE live
// expansion (the project model's own `isPending()`/stamp machinery) instead of
// silently trusting a file set no current declaration reproduces.
//
// G1-e (batch-20261119, review): the version gate above catches a file written
// by an OLDER build, but a version-6 file whose `.f` was edited while the app was
// closed still described a file set no current input reproduces, and "root
// matches + files non-empty" adopted it. `inputsFingerprint` closes that gap: it
// is the stable hash of the authored inputs that produced `files` (the filelists
// in order AND their contents, the root `vedit.toml`'s `[project] auto_discover`,
// the project's -I/-D entries, the effective -I/-D and the variable tables).
// `ProjectStateStore::load` recomputes it from the inputs on disk NOW and drops
// `files` (raising `filesStale`) on any difference, including a file that has no
// fingerprint at all. The version gate and this check are an AND: a file must
// pass both.
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace vedit::workspace::project {

struct ProjectInputs {
    QStringList filelists;
    QStringList includeDirs;
    QStringList defines;
};

struct ProjectState {
    /// The on-disk format this build writes and the OLDEST version whose `files`
    /// it still trusts. Version 6 redefined `files` as "the declared filelist
    /// set"; a version-5 file may hold an `auto_discover` folder scan instead.
    static constexpr int kCurrentVersion = 6;

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
    /// G1-e: `files` came from a file written before version 6 and was therefore
    /// DROPPED on load. Nothing may adopt it as the effective set; the catalog
    /// must wait for the next live expansion. Never written to disk: a save
    /// writes version 6 with the files the model actually has.
    bool filesStale = false;
    // AC-1.5: the project panel's OWN `-I`/`-D` entries, in the spelling the user
    // typed (a relative one is resolved against `root`, never rewritten here).
    // They are an INPUT of the next expansion, so they are persisted separately
    // from the EFFECTIVE result below — repurposing `includeDirs`/`defines` for
    // them would silently change what AC-2.4 restores and what the backends get.
    QStringList projectIncludeDirs;
    QStringList projectDefines;
    QMap<QString, ProjectInputs> inputsByRoot;
    QStringList includeDirs;    // EFFECTIVE `-I` paths of the last expansion
    QStringList defines;        // EFFECTIVE `-D` defines of the last expansion
    QStringList diagnostics;    // `.f`/scan/service problems, phrased for the UI
    QString capturedAt;         // ISO-8601 UTC of the last live expansion
    /// G1-e (batch-20261119, review): the `fnv1a64:` hash of the authored inputs
    /// that were in force when `files` was produced. `toJson` persists it;
    /// `ProjectStateStore::load` compares it against a fresh computation and
    /// refuses to adopt `files` when the two differ. Captured by the model at
    /// the moment an expansion answer is adopted, so a save that happens before
    /// the next expansion still records what `files` belongs to. Empty in a file
    /// written by an older build - which is stale by definition.
    QString inputsFingerprint;
    /// The fingerprint of THIS state's inputs as they are on disk now. Stable
    /// across processes (FNV-1a, no salt); `filelists` contributes both the
    /// ordered paths and the bytes of each file, so editing a `.f` in an editor
    /// changes it.
    QString computeInputsFingerprint() const;

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
    ProjectState load(bool *ok, QString *error = nullptr, bool restoreCachedFiles = true) const;
    // Atomic write (temp file + rename); the parent directory is created.
    bool save(const ProjectState &state, QString *error = nullptr) const;

    static QString fileName();

private:
    QString directory_;
};

} // namespace vedit::workspace::project
