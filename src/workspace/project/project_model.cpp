#include "project_model.h"

#include "path_display.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringLiteral>

#include <algorithm>

#include <utility>
#include <QCoreApplication>

namespace vedit::workspace::project {

namespace {

constexpr int kPollIntervalMs = 10;
constexpr int kJobTimeoutMs = 30000;
// The Preferences dialog writes `settings.toml` atomically (rename); the watch
// sees a directory change and this debounce coalesces the burst into one
// re-expansion.
constexpr int kSettingsDebounceMs = 150;
// Fallback poll for filesystems without a working directory watch (network
// mounts, containers without inotify): a changed stamp re-expands within a
// second, so an edited `-I`/`-D` still takes effect without a restart.
constexpr int kSettingsPollMs = 1000;

// One project `-D` entry normalised for comparison: `NAME`, `NAME=VALUE` with
// the whitespace around `=` dropped (the spelling the Rust parser accepts).
QString normalizedDefineEntry(const QString &entry)
{
    const QString trimmed = entry.trimmed();
    const int equals = trimmed.indexOf(QLatin1Char('='));
    if (equals < 0) {
        return trimmed;
    }
    return trimmed.left(equals).trimmed() + QLatin1Char('=')
           + trimmed.mid(equals + 1).trimmed();
}

QString defineName(const QString &entry)
{
    return entry.section(QLatin1Char('='), 0, 0).trimmed();
}

void deduplicateProjectDefines(QStringList &defines)
{
    // Older versions appended edits. The last entry for a name is the latest edit.
    QSet<QString> seen;
    for (qsizetype index = defines.size(); index > 0; --index) {
        const QString name = defineName(defines.at(index - 1));
        if (seen.contains(name)) {
            defines.removeAt(index - 1);
        } else {
            seen.insert(name);
        }
    }
}

// The Rust `parse_define` rule (crates/vd-config/src/project.rs): an ASCII
// identifier, optionally followed by `=VALUE`. Kept identical so the panel never
// stores an entry the expansion would refuse.
bool isDefineIdentifier(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    const QChar first = name.at(0);
    if (!(first.unicode() < 128 && first.isLetter()) && first != QLatin1Char('_')) {
        return false;
    }
    for (int index = 1; index < name.size(); ++index) {
        const QChar character = name.at(index);
        if (character.unicode() >= 128
            || !(character.isLetterOrNumber() || character == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

// A path entry that cannot be spelled safely in a filelist line or an argv slot.
bool hasUnsafeEntryCharacters(const QString &entry)
{
    for (const QChar character : entry) {
        if (character.category() == QChar::Other_Control) {
            return true;
        }
        if (character == QLatin1Char('"')) {
            return true;
        }
    }
    return false;
}

} // namespace

ProjectModel::ProjectModel(QString stateDirectory, QObject *parent)
    : QObject(parent)
    , store_(stateDirectory)
    , stateDirectory_(std::move(stateDirectory))
{
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &ProjectModel::pollOnce);

    if (stateDirectory_.isEmpty()) {
        // Same fallback as ProjectStateStore: the platform config directory, so
        // `settings.toml` beside `project-model.json` is still found.
        stateDirectory_ = QFileInfo(store_.path()).absolutePath();
    }
    settingsStamp_ = settingsStamp();

    settingsDebounce_.setSingleShot(true);
    settingsDebounce_.setInterval(kSettingsDebounceMs);
    connect(&settingsDebounce_, &QTimer::timeout, this, &ProjectModel::reloadSettings);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { onStateDirectoryChanged(); });
    watchSettings();

    settingsPollTimer_.setInterval(kSettingsPollMs);
    connect(&settingsPollTimer_, &QTimer::timeout, this, &ProjectModel::pollSettingsStamp);
    settingsPollTimer_.start();
}

ProjectModel::~ProjectModel()
{
    pollTimer_.stop();
    settingsDebounce_.stop();
    settingsPollTimer_.stop();
    if (job_ != 0) {
        service_.cancel(job_);
    }
}

bool ProjectModel::restoreFromDisk(QString *error, bool restoreCachedFiles,
                                   std::optional<QString> activeRoot)
{
    bool ok = false;
    QString loadError;
    ProjectState state = store_.load(&ok, &loadError, restoreCachedFiles);
    if (!ok) {
        if (error != nullptr) {
            *error = loadError;
        }
        return false;
    }
    stopExpansion();
    state_ = std::move(state);
    deduplicateProjectDefines(state_.projectDefines);
    // RM-3: a version-2 project-model.json has one live variable table and no
    // per-root map. Seed the entry for its own root so switching away and back
    // cannot lose what the user typed.
    if (!state_.root.isEmpty() && !state_.variables.isEmpty()
        && !state_.variablesByRoot.contains(state_.root)) {
        state_.variablesByRoot.insert(state_.root, state_.variables);
    }
    // RM-17: an older file has no top; seed the live root's entry from `top`
    // (mirror of the variables seeding above) so switching cannot lose it.
    if (!state_.root.isEmpty() && !state_.top.isEmpty()
        && !state_.topByRoot.contains(state_.root)) {
        state_.topByRoot.insert(state_.root, state_.top);
    }
    relativeFiles_.clear();
    missingVariables_.clear();
    referencedVariables_.clear();
    referencedVariableLayers_.clear();
    skipped_ = 0;
    restored_ = true;
    lastError_.clear();
    stateError_.clear();
    if (activeRoot.has_value()) {
        selectRoot(*activeRoot);
    }
    emit stateChanged();
    return true;
}

bool ProjectModel::selectRoot(const QString &root)
{
    const QString absolute = vedit::paths::normalizedRoot(root);
    if (absolute == state_.root) {
        return false;
    }
    stopExpansion();
    if (!state_.root.isEmpty()) {
        state_.inputsByRoot.insert(state_.root,
                                   {state_.filelists, state_.projectIncludeDirs, state_.projectDefines});
    }
    ProjectState next;
    next.root = absolute;
    next.variables = state_.variablesByRoot.value(absolute);
    next.top = state_.topByRoot.value(absolute);
    const ProjectInputs authored = state_.inputsByRoot.value(absolute);
    next.filelists = authored.filelists;
    next.projectIncludeDirs = authored.includeDirs;
    next.projectDefines = authored.defines;
    deduplicateProjectDefines(next.projectDefines);
    next.filesStale = !authored.filelists.isEmpty();
    next.variablesByRoot = std::move(state_.variablesByRoot);
    next.topByRoot = std::move(state_.topByRoot);
    next.inputsByRoot = std::move(state_.inputsByRoot);
    state_ = std::move(next);
    relativeFiles_.clear();
    missingVariables_.clear();
    referencedVariables_.clear();
    referencedVariableLayers_.clear();
    skipped_ = 0;
    restored_ = false;
    lastError_.clear();
    stateError_.clear();
    return true;
}

void ProjectModel::open(const QString &projectRoot)
{
    const bool changed = selectRoot(projectRoot);
    startExpansion();
    if (changed) {
        emit filelistsChanged();
    }
}

void ProjectModel::openRoot(const QString &projectRoot)
{
    // The file tree announces the folder the user opened; that folder IS the
    // project root (AC-2.1), even inside a larger checkout.
    open(projectRoot);
}

void ProjectModel::refresh()
{
    if (state_.root.isEmpty()) {
        emit refreshFinished(false, QCoreApplication::translate("VEdit", "no project is open"));
        return;
    }
    startExpansion(true);
}

bool ProjectModel::importFilelists(const QStringList &paths, QString *error, int *addedFilelists)
{
    if (addedFilelists != nullptr) {
        *addedFilelists = 0;
    }
    if (state_.root.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("open a folder before importing a filelist");
        }
        return false;
    }
    QStringList accepted = state_.filelists;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            if (error != nullptr) {
                *error = QStringLiteral("%1 is not a readable filelist").arg(path);
            }
            return false; // atomic: nothing is added on a bad entry
        }
        const QString absolute = info.absoluteFilePath();
        if (!accepted.contains(absolute)) {
            accepted.append(absolute);
        }
    }
    if (error != nullptr) {
        error->clear();
    }
    if (accepted == state_.filelists) {
        return true; // already imported: no re-parse needed
    }
    if (addedFilelists != nullptr) {
        *addedFilelists =
            static_cast<int>(accepted.size() - state_.filelists.size());
    }
    state_.filelists = accepted;
    saveState();
    emit filelistsChanged();
    startExpansion();
    return true;
}

bool ProjectModel::removeFilelist(const QString &path)
{
    const int before = state_.filelists.size();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    state_.filelists.removeAll(path);
    state_.filelists.removeAll(absolute);
    if (state_.filelists.size() == before) {
        return false;
    }
    saveState();
    emit filelistsChanged();
    startExpansion();
    return true;
}

QStringList ProjectModel::filelists() const
{
    return state_.filelists;
}

QStringList ProjectModel::variables() const
{
    return state_.variables;
}

QStringList ProjectModel::missingVariables() const
{
    return missingVariables_;
}

QStringList ProjectModel::referencedVariables() const
{
    return referencedVariables_;
}

QStringList ProjectModel::referencedVariableLayers() const
{
    return referencedVariableLayers_;
}

QString ProjectModel::referencedVariableLayer(const QString &name) const
{
    // The two lists are parallel by contract (E1); stop at the shorter one so a
    // malformed payload can never read past the end.
    const int count = std::min(referencedVariables_.size(), referencedVariableLayers_.size());
    for (int index = 0; index < count; ++index) {
        if (referencedVariables_.at(index) == name) {
            return referencedVariableLayers_.at(index);
        }
    }
    return {};
}

QVariantMap ProjectModel::expansionInputs() const
{
    QVariantMap inputs;
    inputs.insert(QStringLiteral("root"), projectRoot());
    inputs.insert(QStringLiteral("filelists"), filelists());
    inputs.insert(QStringLiteral("variables"), variables());
    inputs.insert(QStringLiteral("projectIncludeDirs"), projectIncludeDirs());
    inputs.insert(QStringLiteral("projectDefines"), projectDefines());
    inputs.insert(QStringLiteral("settingsPath"), settingsPath());
    inputs.insert(QStringLiteral("expansions"), static_cast<qulonglong>(expansionCount()));
    // batch-20261115 AC-2.1/AC-2.2: the ONE gate the consumers share. True means
    // "this root is a project": the module tree may catalogue it and the
    // diagnostics lane may expand its file set. Publishing it here (instead of
    // letting each lane re-derive the rule) keeps a single answer for one root.
    inputs.insert(QStringLiteral("hasFilelist"), hasFilelist());
    // batch-20261114 E1/E2: the referenced set + the layer each name resolved in.
    // Parallel lists (the ABI's own shape), empty until the first expansion.
    inputs.insert(QStringLiteral("referencedVariables"), referencedVariables_);
    inputs.insert(QStringLiteral("referencedVariableLayers"), referencedVariableLayers_);
    return inputs;
}

int ProjectModel::skippedCount() const
{
    return skipped_;
}

QStringList ProjectModel::normalizedVariables(const QStringList &values) const
{
    QStringList names;
    QStringList out;
    for (const QString &raw : values) {
        const QString entry = raw.trimmed();
        if (entry.isEmpty()) {
            continue;
        }
        const int split = entry.indexOf(QLatin1Char('='));
        const QString name = (split < 0 ? entry : entry.left(split)).trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const QString value = split < 0 ? QString() : entry.mid(split + 1);
        const int existing = names.indexOf(name);
        const QString normalized = QStringLiteral("%1=%2").arg(name, value);
        if (existing >= 0) {
            out[existing] = normalized; // last definition of a name wins
            continue;
        }
        names.append(name);
        out.append(normalized);
    }
    return out;
}

void ProjectModel::setVariables(const QStringList &variables)
{
    const QStringList normalized = normalizedVariables(variables);
    if (normalized == state_.variables) {
        return;
    }
    state_.variables = normalized;
    // RM-3: the table is remembered PER ROOT, so a later switch back restores
    // exactly what was typed for this project (and nothing for another one).
    if (!state_.root.isEmpty()) {
        state_.variablesByRoot.insert(state_.root, normalized);
    }
    saveState();
    emit stateChanged();
    if (!state_.root.isEmpty()) {
        startExpansion();
    }
}

QString ProjectModel::top() const
{
    return state_.top;
}

void ProjectModel::setTop(const QString &top)
{
    const QString trimmed = top.trimmed();
    if (trimmed == state_.top) {
        // AC-7.2 (batch-20261115): choosing the SAME top again is not a no-op.
        // The tree/catalog may not be running that top (a failed run, a root
        // switch that has not finished), so the choice must re-announce itself
        // and let the module-tree holder force its catalog run. Nothing is
        // persisted again: the stored value is already this one.
        emit stateChanged();
        return;
    }
    state_.top = trimmed;
    // RM-17: remembered PER ROOT, exactly like the variable tables above.
    if (!state_.root.isEmpty()) {
        if (trimmed.isEmpty()) {
            state_.topByRoot.remove(state_.root);
        } else {
            state_.topByRoot.insert(state_.root, trimmed);
        }
    }
    saveState();
    emit stateChanged();
}

void ProjectModel::setVariable(const QString &name, const QString &value)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return;
    }
    QStringList updated = state_.variables;
    bool replaced = false;
    for (int index = 0; index < updated.size(); ++index) {
        const int split = updated.at(index).indexOf(QLatin1Char('='));
        const QString existingName =
            (split < 0 ? updated.at(index) : updated.at(index).left(split)).trimmed();
        if (existingName == trimmedName) {
            updated[index] = QStringLiteral("%1=%2").arg(trimmedName, value);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        updated.append(QStringLiteral("%1=%2").arg(trimmedName, value));
    }
    setVariables(updated);
}

void ProjectModel::reloadSettings()
{
    if (state_.root.isEmpty()) {
        settingsStamp_ = settingsStamp();
        return;
    }
    settingsStamp_ = settingsStamp();
    // The settings FILE changed but the inputs are identical: force.
    startExpansion(true);
}

bool ProjectModel::hasProject() const
{
    return state_.isValid();
}

bool ProjectModel::hasFilelist() const
{
    // AC-2.1/AC-2.5 + A61.2: "this folder is a project" means the user
    // EXPLICITLY imported a filelist (the Project panel's `Import Filelist…`
    // action, or the explicit "Import" answer to the question the open seam asks
    // for a `.f`). Merely OPENING a `.f` never imports it, and a `.f` merely
    // sitting in the root is NOT an import. Since the 2026-11-18 file-set
    // decision the Rust expansion only auto-discovers such a file when the root
    // opts in with `[project] auto_discover = true` (default `false`), and even
    // then it adds FILES to an existing project - it never turns a folder into
    // one by itself (docs/USER_GUIDE.md §2.8).
    return !state_.filelists.isEmpty();
}

QString ProjectModel::noFilelistReason() const
{
    // AC-2.1: the empty state names the ONE action that turns the folder into a
    // project, instead of an empty tree with no explanation.
    return QCoreApplication::translate(
        "VEdit",
        "No filelist — import a .f filelist (Project ▸ Import Filelist…) to build the "
        "project; until then every tool works on the open file only");
}

bool ProjectModel::isPending() const
{
    return pending_;
}

bool ProjectModel::restoredFromDisk() const
{
    return restored_;
}

QString ProjectModel::projectRoot() const
{
    return state_.root;
}

QStringList ProjectModel::files() const
{
    return state_.files;
}

QStringList ProjectModel::displayFiles() const
{
    QStringList out;
    out.reserve(state_.files.size());
    for (const QString &file : state_.files) {
        out.append(vedit::paths::displayPath(state_.root, file));
    }
    return out;
}

QStringList ProjectModel::displayFilelists() const
{
    QStringList out;
    out.reserve(state_.filelists.size());
    for (const QString &filelist : state_.filelists) {
        out.append(vedit::paths::displayPath(state_.root, filelist));
    }
    return out;
}

QStringList ProjectModel::relativeFiles() const
{
    // The ABI hands out the authoritative project-relative spelling; after a
    // restart (state loaded from disk, no expansion yet) it is re-derived from
    // the root, which is the same rule.
    if (relativeFiles_.size() == state_.files.size()) {
        return relativeFiles_;
    }
    return displayFiles();
}

QStringList ProjectModel::includeDirs() const
{
    return state_.includeDirs;
}

QStringList ProjectModel::defines() const
{
    return state_.defines;
}

QStringList ProjectModel::projectIncludeDirs() const
{
    return state_.projectIncludeDirs;
}

QStringList ProjectModel::projectDefines() const
{
    return state_.projectDefines;
}

QString ProjectModel::resolveProjectIncludeDir(const QString &entry) const
{
    const QString trimmed = entry.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QFileInfo info(trimmed);
    if (info.isAbsolute()) {
        return QDir::cleanPath(trimmed);
    }
    return QDir::cleanPath(QDir(state_.root).filePath(trimmed));
}

QStringList ProjectModel::resolvedIncludeDirs() const
{
    QStringList resolved;
    for (const QString &entry : state_.projectIncludeDirs) {
        const QString path = resolveProjectIncludeDir(entry);
        if (!path.isEmpty()) {
            resolved.append(path);
        }
    }
    return resolved;
}

bool ProjectModel::validateProjectEntry(const QString &entry, bool includeDir, QString *error) const
{
    const QString trimmed = entry.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = includeDir
                         ? QCoreApplication::translate("VEdit", "An include dir cannot be empty")
                         : QCoreApplication::translate("VEdit", "A define cannot be empty");
        }
        return false;
    }
    if (hasUnsafeEntryCharacters(trimmed)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                         "VEdit", "\"%1\" contains a control character or a double quote")
                         .arg(trimmed);
        }
        return false;
    }
    if (includeDir) {
        const QString candidate = QDir::cleanPath(
            QFileInfo(trimmed).isAbsolute() ? trimmed : QDir(state_.root).filePath(trimmed));
        for (const QString &existing : state_.projectIncludeDirs) {
            if (resolveProjectIncludeDir(existing) == candidate) {
                if (error != nullptr) {
                    *error = QCoreApplication::translate("VEdit", "\"%1\" is already an include dir")
                                 .arg(trimmed);
                }
                return false;
            }
        }
        return true;
    }
    if (!isDefineIdentifier(defineName(trimmed))) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                         "VEdit", "\"%1\" is not NAME or NAME=VALUE (the name must be an identifier)")
                         .arg(trimmed);
        }
        return false;
    }
    const QString normalized = normalizedDefineEntry(trimmed);
    for (const QString &existing : state_.projectDefines) {
        if (normalizedDefineEntry(existing) == normalized) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "\"%1\" is already a define")
                             .arg(trimmed);
            }
            return false;
        }
    }
    return true;
}

bool ProjectModel::addIncludeDir(const QString &entry, QString *error)
{
    if (!validateProjectEntry(entry, true, error)) {
        return false;
    }
    state_.projectIncludeDirs.append(entry.trimmed());
    saveState();
    startExpansion(true);
    emit stateChanged();
    return true;
}

bool ProjectModel::removeIncludeDir(const QString &entry)
{
    const QString trimmed = entry.trimmed();
    const QString wanted = resolveProjectIncludeDir(trimmed);
    for (int index = 0; index < state_.projectIncludeDirs.size(); ++index) {
        const QString &existing = state_.projectIncludeDirs.at(index);
        if (existing.trimmed() == trimmed || resolveProjectIncludeDir(existing) == wanted) {
            state_.projectIncludeDirs.removeAt(index);
            saveState();
            startExpansion(true);
            emit stateChanged();
            return true;
        }
    }
    return false;
}

bool ProjectModel::addDefine(const QString &entry, QString *error)
{
    if (!validateProjectEntry(entry, false, error)) {
        return false;
    }
    const QString normalized = normalizedDefineEntry(entry);
    const QString name = defineName(normalized);
    const auto existing = std::find_if(state_.projectDefines.begin(), state_.projectDefines.end(),
                                       [&name](const QString &value) { return defineName(value) == name; });
    if (existing == state_.projectDefines.end()) {
        state_.projectDefines.append(normalized);
    } else {
        *existing = normalized;
    }
    saveState();
    startExpansion(true);
    emit stateChanged();
    return true;
}

bool ProjectModel::removeDefine(const QString &entry)
{
    const QString normalized = normalizedDefineEntry(entry);
    for (int index = 0; index < state_.projectDefines.size(); ++index) {
        if (normalizedDefineEntry(state_.projectDefines.at(index)) == normalized) {
            state_.projectDefines.removeAt(index);
            saveState();
            startExpansion(true);
            emit stateChanged();
            return true;
        }
    }
    return false;
}

QStringList ProjectModel::displayIncludeDirs() const
{
    QStringList display = state_.projectIncludeDirs;
    QStringList covered;
    for (const QString &entry : state_.projectIncludeDirs) {
        const QString path = resolveProjectIncludeDir(entry);
        if (!path.isEmpty()) {
            covered.append(path);
        }
    }
    for (const QString &effective : state_.includeDirs) {
        if (!covered.contains(QDir::cleanPath(effective))) {
            display.append(effective);
        }
    }
    return display;
}

QStringList ProjectModel::displayDefines() const
{
    QStringList display = state_.projectDefines;
    QSet<QString> covered;
    for (const QString &entry : state_.projectDefines) {
        covered.insert(defineName(entry));
    }
    for (const QString &effective : state_.defines) {
        const QString name = defineName(effective);
        if (!covered.contains(name)) {
            covered.insert(name);
            display.append(effective);
        }
    }
    return display;
}

QStringList ProjectModel::diagnostics() const
{
    QStringList out = state_.diagnostics;
    if (!stateError_.isEmpty()) {
        out.append(QStringLiteral("state: %1").arg(stateError_));
    }
    if (!lastError_.isEmpty()) {
        out.append(QStringLiteral("service: %1").arg(lastError_));
    }
    return out;
}

QString ProjectModel::summary() const
{
    if (!hasProject()) {
        return QCoreApplication::translate("VEdit", "No project open");
    }
    const QString name = QFileInfo(state_.root).fileName();
    const int count = state_.files.size();
    QString text = QCoreApplication::translate("VEdit", "Project: %1 — %2 %3")
                       .arg(name.isEmpty() ? state_.root : name)
                       .arg(count)
                       .arg(count == 1
                                ? QCoreApplication::translate("VEdit", "file")
                                : QCoreApplication::translate("VEdit", "files"));
    const int lists = state_.filelists.size();
    if (lists > 0) {
        // i18n (UX batch 2 / A2): the wording is chosen BEFORE translate() and
        // each call gets a plain string literal. lupdate's static extractor only
        // accepts a literal second argument, so the previous
        // `translate("VEdit", lists == 1 ? "…" : "…")` made these six strings
        // unreachable for the catalogue (they fell out of the .qm on every
        // regeneration). The rendered text is unchanged.
        text += (lists == 1 ? QCoreApplication::translate("VEdit", " · %1 filelist")
                            : QCoreApplication::translate("VEdit", " · %1 filelists"))
                    .arg(lists);
    }
    const int variables = state_.variables.size();
    if (variables > 0) {
        text += (variables == 1 ? QCoreApplication::translate("VEdit", " · %1 variable")
                                : QCoreApplication::translate("VEdit", " · %1 variables"))
                    .arg(variables);
    }
    if (skipped_ > 0) {
        text += (skipped_ == 1 ? QCoreApplication::translate("VEdit", " · %1 line skipped")
                               : QCoreApplication::translate("VEdit", " · %1 lines skipped"))
                    .arg(skipped_);
    }
    if (!missingVariables_.isEmpty()) {
        text += QCoreApplication::translate("VEdit", " · %1 unresolved")
                    .arg(missingVariables_.size());
    }
    // AC-7.1 (batch-20261115): the module tree's root follows the user's top, so
    // the summary names it - the one line that says WHY the tree shows a
    // subtree instead of every uninstantiated module.
    if (!state_.top.isEmpty()) {
        text += QCoreApplication::translate("VEdit", " · top %1").arg(state_.top);
    }
    if (!hasFilelist()) {
        // AC-2.1: the folder is open but it is not a project yet; the file set is
        // empty on purpose, so the summary must say so rather than "0 files".
        text += QCoreApplication::translate("VEdit", " · no filelist (tools use the open file)");
    }
    if (!hasFilelist()) {
        // The "no filelist" segment above IS this project's state: no expansion
        // ran (or is running), so none of the live/restored suffixes is true.
        return text;
    }
    if (pending_) {
        text += QCoreApplication::translate("VEdit", " (expanding…)");
    } else if (!lastError_.isEmpty()) {
        text += QCoreApplication::translate("VEdit", " (refresh failed)");
    } else if (restored_) {
        text += QCoreApplication::translate("VEdit", " (restored from last session)");
    } else {
        text += QCoreApplication::translate("VEdit", " (live)");
    }
    return text;
}

QString ProjectModel::backendArguments() const
{
    // AC-1.5 + AC-2.5: the project's own typed entries come first (an `-I` is
    // resolved against the root, exactly as the expansion passes it), then the
    // effective entries the filelist/project file/tools page contributed that no
    // project entry already covers. This is the projection the child argv gets
    // once the re-expansion settles, and it is already correct for a freshly
    // typed entry.
    QStringList dirs = resolvedIncludeDirs();
    for (const QString &effective : state_.includeDirs) {
        const QString cleaned = QDir::cleanPath(effective);
        if (!dirs.contains(cleaned)) {
            dirs.append(cleaned);
        }
    }
    QStringList defines;
    for (const QString &entry : state_.projectDefines) {
        defines.append(normalizedDefineEntry(entry));
    }
    for (const QString &effective : state_.defines) {
        if (!defines.contains(normalizedDefineEntry(effective))) {
            defines.append(effective);
        }
    }
    QStringList arguments;
    arguments.reserve(dirs.size() + defines.size());
    for (const QString &dir : dirs) {
        arguments.append(QStringLiteral("-I%1").arg(dir));
    }
    for (const QString &define : defines) {
        arguments.append(QStringLiteral("-D%1").arg(define));
    }
    return arguments.join(QLatin1Char(' '));
}

QString ProjectModel::settingsPath() const
{
    if (stateDirectory_.isEmpty()) {
        return QString();
    }
    return QDir(stateDirectory_).filePath(QStringLiteral("settings.toml"));
}

QString ProjectModel::statePath() const
{
    return store_.path();
}

QString ProjectModel::lastError() const
{
    return lastError_;
}

QString ProjectModel::settingsStamp() const
{
    const QFileInfo info(settingsPath());
    if (!info.exists()) {
        return QStringLiteral("missing");
    }
    return QStringLiteral("%1:%2").arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
}

void ProjectModel::watchSettings()
{
    if (stateDirectory_.isEmpty()) {
        return;
    }
    // The directory is watched, not the file: the store saves atomically (rename
    // over the old file), which drops a file watch.
    QDir().mkpath(stateDirectory_);
    if (!watcher_.directories().contains(stateDirectory_)) {
        watcher_.addPath(stateDirectory_);
    }
}

void ProjectModel::onStateDirectoryChanged()
{
    watchSettings();
    scheduleSettingsReload();
}

void ProjectModel::pollSettingsStamp()
{
    scheduleSettingsReload();
}

void ProjectModel::scheduleSettingsReload()
{
    const QString stamp = settingsStamp();
    if (stamp == settingsStamp_) {
        return; // our own project-model.json write must not loop
    }
    settingsStamp_ = stamp;
    settingsDebounce_.start();
}

bool ProjectModel::saveState()
{
    if (state_.root.isEmpty()) {
        return true;
    }
    QString error;
    if (!store_.save(state_, &error)) {
        stateError_ = error;
        return false;
    }
    stateError_.clear();
    return true;
}

void ProjectModel::stopExpansion()
{
    if (job_ != 0) {
        service_.cancel(job_);
        job_ = 0;
    }
    pending_ = false;
    pendingRoot_.clear();
    pendingFilelists_.clear();
    pendingVariables_.clear();
    pendingIncludeDirs_.clear();
    pendingDefines_.clear();
    pollTimer_.stop();
}

void ProjectModel::startExpansion(bool force)
{
    // A root that no longer exists is a VISIBLE failure, never an empty tree -
    // and it stays reported now that the filelist gate below would otherwise
    // short-circuit with "nothing to expand" (the restored session and a moved
    // project both land here).
    if (!state_.root.isEmpty() && !QFileInfo(state_.root).isDir()) {
        recordFailure(QCoreApplication::translate("VEdit", "the project root does not exist: %1")
                          .arg(state_.root));
        return;
    }
    // AC-2.1/AC-2.5 (batch-20261115): without a filelist the folder is NOT a
    // project. Nothing is scanned, no service job is started and the file set
    // stays empty - the panel shows `noFilelistReason()` and every tool works on
    // the open file. Importing the first `.f` turns the same root into a project
    // (this gate then passes) without re-opening it.
    if (!hasFilelist()) {
        stopExpansion();
        lastError_.clear();
        relativeFiles_.clear();
        missingVariables_.clear();
        referencedVariables_.clear();
        referencedVariableLayers_.clear();
        skipped_ = 0;
        state_.files.clear();
        state_.includeDirs.clear();
        state_.defines.clear();
        state_.diagnostics.clear();
        emit stateChanged();
        emit refreshFinished(true, QString());
        return;
    }
    // RM-2: ONE expansion per project switch. One root change is announced by
    // several live surfaces (the file tree's `directoryOpened`, the shell's
    // `projectRootChanged`, the `.f` importer / a restore), and each report
    // used to cancel and restart the same job. A duplicate report with the
    // SAME inputs while one is in flight is now a no-op; `force` is reserved
    // for an explicit Refresh or a settings-file change, where the inputs are
    // unchanged but the answer must be recomputed.
    if (!force && pending_ && job_ != 0 && pendingRoot_ == state_.root
        && pendingFilelists_ == state_.filelists && pendingVariables_ == state_.variables
        && pendingIncludeDirs_ == state_.projectIncludeDirs
        && pendingDefines_ == state_.projectDefines) {
        return;
    }
    stopExpansion();
    quint64 id = 0;
    if (service_.start(state_.root, state_.filelists, state_.variables,
                       state_.projectIncludeDirs, state_.projectDefines, settingsPath(), &id)
        != ProjectService::Start::Started) {
        recordFailure(service_.lastError());
        return;
    }
    job_ = id;
    pending_ = true;
    pendingRoot_ = state_.root;
    pendingFilelists_ = state_.filelists;
    pendingVariables_ = state_.variables;
    pendingIncludeDirs_ = state_.projectIncludeDirs;
    pendingDefines_ = state_.projectDefines;
    ++expansionCount_;
    lastError_.clear();
    deadline_.start();
    pollTimer_.start();
    emit stateChanged();
}

void ProjectModel::pollOnce()
{
    if (!pending_ || job_ == 0) {
        pollTimer_.stop();
        return;
    }
    if (deadline_.elapsed() > kJobTimeoutMs) {
        const quint64 stuck = job_;
        job_ = 0;
        service_.cancel(stuck);
        recordFailure(QCoreApplication::translate("VEdit", "the project service did not answer in %1 ms").arg(kJobTimeoutMs));
        return;
    }

    ExpansionPayload payload;
    switch (service_.poll(job_, &payload)) {
    case ProjectService::Poll::Pending:
        return;
    case ProjectService::Poll::Ready: {
        job_ = 0;
        pending_ = false;
        pollTimer_.stop();
        adopt(payload);
        emit stateChanged();
        emit refreshFinished(true, QString());
        if (!missingVariables_.isEmpty()) {
            // A-9: one signal, one dialog, a row per variable.
            emit missingVariablesDetected(missingVariables_);
        }
        return;
    }
    case ProjectService::Poll::Failed: {
        recordFailure(service_.lastError());
        return;
    }
    case ProjectService::Poll::Misuse: {
        recordFailure(QCoreApplication::translate("VEdit", "the project job was rejected by the service"));
        return;
    }
    }
}

void ProjectModel::adopt(const ExpansionPayload &payload)
{
    state_.files = payload.files;
    relativeFiles_ = payload.relativeFiles;
    missingVariables_ = payload.missingVariables;
    referencedVariables_ = payload.referencedVariables;
    referencedVariableLayers_ = payload.referencedVariableLayers;
    skipped_ = payload.skipped;
    state_.includeDirs = payload.includeDirs;
    state_.defines = payload.defines;
    state_.diagnostics = payload.diagnostics;
    state_.capturedAt = ProjectState::nowUtc();
    // G1-e (review): record WHICH authored inputs produced this file set. Saving
    // the fingerprint here (not recomputing it on every save) is what makes the
    // check meaningful: an import/top/variable change saves the OLD fingerprint
    // until the next answer replaces it, so the next load sees that the declared
    // inputs moved on and refuses the file set that answered the previous ones.
    state_.inputsFingerprint = state_.computeInputsFingerprint();
    restored_ = false;
    lastError_.clear();
    saveState();
}

void ProjectModel::recordFailure(const QString &error)
{
    lastError_ = error.isEmpty() ? QCoreApplication::translate("VEdit", "the project service failed") : error;
    stopExpansion();
    emit stateChanged();
    emit refreshFinished(false, lastError_);
}

} // namespace vedit::workspace::project
