#pragma once

// The project panel's state machine: restores the persisted project, asks the
// Rust service to expand it, and publishes the result to the panel.
//
// Threading: everything here runs on the GUI thread. The expansion itself runs
// on the Rust worker thread (C2); `pollTimer_` polls it every few milliseconds
// and never blocks the event loop.
//
// The model owns the inputs the service needs (AC-2.2–2.5): the root, the
// imported `.f` filelists, and the settings store path whose `tools.include_dirs`
// / `tools.defines` are appended by the worker. A change to `settings.toml` (the
// Preferences dialog saves atomically) is observed with a directory watch and
// re-expands the project, so an edited `-I`/`-D` takes effect without a restart.
//
// Restart behaviour (AC-2.4): `restoreFromDisk()` loads
// `<state dir>/project-model.json` so the project, its imported filelists and
// its members are listed immediately on the next start, even before (or without)
// the service answering, and a refresh replaces them with the live expansion.
#include "project_service.h"
#include "project_state.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace vedit::workspace::project {

class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QString stateDirectory, QObject *parent = nullptr);
    ~ProjectModel() override;

    // Loads the persisted project (root, imported filelists, members). Returns
    // true when one was restored; `error` receives why not (empty for "nothing
    // saved yet").
    bool restoreFromDisk(QString *error = nullptr, bool restoreCachedFiles = true);

    // Opens `projectRoot` (switching projects clears the previous members and
    // imported filelists) and starts an asynchronous expansion.
    void open(const QString &projectRoot);

    // Re-expands the current root; no-op with a failed signal when none is open.
    // Q_INVOKABLE so a consumer that only sees the model as the published
    // `vedit.project.model` QObject (the diagnostics lane's Run retry, G1-a
    // review finding 1) can ask the lane that OWNS the expansion to run it
    // again, without a link edge between the two lanes.
    Q_INVOKABLE void refresh();

    // AC-2.2: "Import Filelist…". Adds one or more `.f` files (absolute paths,
    // import order, duplicates rejected) and re-expands. Atomic: an unreadable
    // path rejects the whole call and nothing is added. `addedFilelists` (when
    // given) receives how many entries were NEWLY added; 0 means the call was a
    // no-op ("already imported"), which the panel reports honestly instead of
    // claiming a re-import.
    bool importFilelists(const QStringList &paths, QString *error = nullptr,
                         int *addedFilelists = nullptr);
    // Removes one imported `.f` (by path as shown or absolute) and re-expands.
    bool removeFilelist(const QString &path);
    QStringList filelists() const;
    // The same filelists with the product's ONE display rule (FIX 1): relative
    // to the project root when inside it, absolute otherwise. `filelists()`
    // stays absolute for the state store and the importer.
    QStringList displayFilelists() const;

    // ADDENDUM A-9: the project variable table (`NAME=VALUE`, persisted). It
    // wins over the process environment when a `.f` expands; changing it
    // re-expands. `missingVariables()` is the unresolved set of the LAST
    // expansion (the one-dialog fills all of them in one pass) and
    // `skippedCount()` counts lines that produced no entry.
    QStringList variables() const;
    void setVariables(const QStringList &variables);
    void setVariable(const QString &name, const QString &value);
    QStringList missingVariables() const;
    // batch-20261114 E1 (consumed by E2): every filelist variable name the LAST
    // expansion REFERENCED and, in the same order, the layer that resolved it
    // (`project` / `imported` / `environment` / `missing`). Live like
    // `missingVariables()`: a root switch or a restore-from-disk clears them until
    // the next expansion answers.
    QStringList referencedVariables() const;
    QStringList referencedVariableLayers() const;
    // One name's layer, or `QString()` when the last expansion did not reference it.
    QString referencedVariableLayer(const QString &name) const;
    // The payload the lane publishes through the `vedit.project.inputs` shell
    // property (RM-2): the expansion INPUTS plus the expansion COUNT plus the
    // referenced set above. One builder for the publisher and its tests, so the
    // property can never drift from the model it describes.
    QVariantMap expansionInputs() const;
    int skippedCount() const;

    // RM-17: the module-tree TOP the user named for this project (a module
    // NAME; empty = Auto). Persisted per root, exactly like `variables`: a
    // switch A -> B -> A restores A's top and B never inherits it.
    QString top() const;
    void setTop(const QString &top);

    bool hasProject() const;
    // batch-20261115 AC-2.1/AC-2.5: a PROJECT is a folder PLUS a filelist.
    // Without one the model neither scans the root nor asks the service to
    // expand: every tool works on the single open file. Only an EXPLICIT import
    // (or a restored imported list) counts; a `.f` sitting in the root is
    // discovered only when the project opts in with
    // `[project] auto_discover = true` (default `false`), and that still
    // requires an existing project (docs/USER_GUIDE.md §2.8).
    bool hasFilelist() const;
    // The one line a panel shows instead of an empty file list (AC-2.1).
    QString noFilelistReason() const;
    bool isPending() const;
    bool restoredFromDisk() const;
    QString projectRoot() const;

    // Absolute paths exactly as the Rust service returned them (what the UI opens).
    QStringList files() const;
    // The same members relative to the project root (absolute only for a file
    // outside it) — the portable spelling for a generated `.f`. Correct jump
    // URIs do NOT depend on this: the controlled 4-way spike (2026-11-02) showed
    // the server's cwd must be the project root, and `projectRoot()` is the one
    // source of truth for that cwd and for `rootUri`.
    QStringList relativeFiles() const;
    // The same paths relative to the project root when they are inside it.
    QStringList displayFiles() const;
    QStringList includeDirs() const;
    QStringList defines() const;

    // AC-1.5: the project panel's OWN editable `-I`/`-D` entries, exactly as the
    // user typed them (persisted in project-model.json and passed to the next
    // expansion, which resolves a relative `-I` against the project root and
    // de-duplicates it). `includeDirs()`/`defines()` above stay the EFFECTIVE
    // result of the last expansion, so the AC-2.4 observable is unchanged.
    QStringList projectIncludeDirs() const;
    QStringList projectDefines() const;
    // Add one entry. Rejects (with a reason naming the problem) an empty value,
    // one containing a control character or a double quote, and a duplicate of
    // an entry the project already has (compared after resolution, so `inc`,
    // `./inc` and the absolute twin are the same entry). A define must be
    // `NAME` or `NAME=VALUE` with an identifier name. On success the entry is
    // persisted and the project re-expands immediately (no restart, no manual
    // refresh).
    bool addIncludeDir(const QString &entry, QString *error = nullptr);
    bool removeIncludeDir(const QString &entry);
    bool addDefine(const QString &entry, QString *error = nullptr);
    bool removeDefine(const QString &entry);
    // The project entries with a relative `-I` resolved against the root: the
    // exact arguments the expansion (and therefore lint/LSP/rename) receives.
    QStringList resolvedIncludeDirs() const;
    // What the panel lists for each kind: the project entries in the user's
    // spelling first, then the effective entries the expansion contributed that
    // no project entry explains (filelist / vedit.toml / tools page).
    QStringList displayIncludeDirs() const;
    QStringList displayDefines() const;
    // Service diagnostics plus any state-store/service failure, phrased for the UI.
    QStringList diagnostics() const;

    // AC-2.5: what the backends receive, expanded into the exact `-I`/`-D`
    // arguments (the same projection `vd_deeplint::command_line` echoes). This
    // is what the settings page shows and what the evidence dumps.
    QString backendArguments() const;
    QString settingsPath() const;

    // One line the panel shows first, e.g.
    // `Project: verilog-project — 11 files · 2 filelists (live)`.
    QString summary() const;
    // RM-2 probe: how many expansions this model has SCHEDULED. One project
    // switch (one root change) must move this by exactly one, however many live
    // widgets/events report the switch.
    quint64 expansionCount() const { return expansionCount_; }
    QString statePath() const;
    QString lastError() const;

public slots:
    // Receiver of the file tree's `directoryOpened`: opening a folder makes it
    // the project root (AC-2.1).
    void openRoot(const QString &projectRoot);
    // Re-reads the tools-page values by re-expanding (the settings watch calls
    // this automatically after a save; an explicit reload is also allowed).
    void reloadSettings();

signals:
    void stateChanged();
    void filelistsChanged();
    // A-9: the last expansion left variables unresolved; the shell shows ONE
    // dialog with a row per name.
    void missingVariablesDetected(const QStringList &names);
    // Emitted when an expansion attempt settles: `ok` false carries the reason.
    void refreshFinished(bool ok, const QString &error);

private:
    /// `force` re-expands even when the same inputs are already in flight; the
    /// switch paths leave it false so a duplicate report is a no-op (RM-2).
    void startExpansion(bool force = false);
    void pollOnce();
    void adopt(const ExpansionPayload &payload);
    void recordFailure(const QString &error);
    bool saveState();
    QStringList normalizedVariables(const QStringList &values) const;
    QString resolveProjectIncludeDir(const QString &entry) const;
    bool validateProjectEntry(const QString &entry, bool includeDir, QString *error) const;
    QString settingsStamp() const;
    void watchSettings();
    void onStateDirectoryChanged();
    void scheduleSettingsReload();
    void pollSettingsStamp();

    ProjectService service_;
    ProjectStateStore store_;
    QTimer pollTimer_;
    QElapsedTimer deadline_;
    QFileSystemWatcher watcher_;
    QTimer settingsDebounce_;
    QTimer settingsPollTimer_;
    QString stateDirectory_;
    QString settingsStamp_;
    ProjectState state_;
    QStringList relativeFiles_;
    QStringList missingVariables_;
    QStringList referencedVariables_;
    QStringList referencedVariableLayers_;
    int skipped_ = 0;
    QString lastError_;
    QString stateError_;
    quint64 job_ = 0;
    quint64 expansionCount_ = 0;
    // Inputs of the expansion currently in flight (RM-2's duplicate guard).
    QString pendingRoot_;
    QStringList pendingFilelists_;
    QStringList pendingVariables_;
    QStringList pendingIncludeDirs_;
    QStringList pendingDefines_;
    bool pending_ = false;
    bool restored_ = false;
};

} // namespace vedit::workspace::project
