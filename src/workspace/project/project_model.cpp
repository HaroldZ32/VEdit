#include "project_model.h"

#include "path_display.h"

#include <QDir>
#include <QFileInfo>
#include <QStringLiteral>

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

bool ProjectModel::restoreFromDisk(QString *error)
{
    bool ok = false;
    QString loadError;
    ProjectState state = store_.load(&ok, &loadError);
    if (!ok) {
        if (error != nullptr) {
            *error = loadError;
        }
        return false;
    }
    state_ = state;
    relativeFiles_.clear();
    missingVariables_.clear();
    skipped_ = 0;
    restored_ = true;
    lastError_.clear();
    stateError_.clear();
    emit stateChanged();
    return true;
}

void ProjectModel::open(const QString &projectRoot)
{
    if (projectRoot.isEmpty()) {
        return;
    }
    // FIX 1: the root is stored in the SAME normalised spelling the shell keeps,
    // so displayFiles()/displayFilelists() compute "inside the root" against the
    // same string the project lane was opened with.
    const QString absolute = vedit::paths::normalizedRoot(projectRoot);
    if (absolute.isEmpty()) {
        return;
    }
    if (absolute != state_.root) {
        state_ = ProjectState{};
        state_.root = absolute;
        relativeFiles_.clear();
        missingVariables_.clear();
        skipped_ = 0;
        restored_ = false;
        emit filelistsChanged();
    }
    startExpansion();
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
    startExpansion();
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
    saveState();
    emit stateChanged();
    if (!state_.root.isEmpty()) {
        startExpansion();
    }
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
    startExpansion();
}

bool ProjectModel::hasProject() const
{
    return state_.isValid();
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
    QStringList arguments;
    arguments.reserve(state_.includeDirs.size() + state_.defines.size());
    for (const QString &dir : state_.includeDirs) {
        arguments.append(QStringLiteral("-I%1").arg(dir));
    }
    for (const QString &define : state_.defines) {
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
    QString error;
    if (!store_.save(state_, &error)) {
        stateError_ = error;
        return false;
    }
    stateError_.clear();
    return true;
}

void ProjectModel::startExpansion()
{
    if (pending_ && job_ != 0) {
        service_.cancel(job_);
        job_ = 0;
    }
    quint64 id = 0;
    if (service_.start(state_.root, state_.filelists, state_.variables, settingsPath(), &id)
        != ProjectService::Start::Started) {
        recordFailure(service_.lastError());
        return;
    }
    job_ = id;
    pending_ = true;
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
    skipped_ = payload.skipped;
    state_.includeDirs = payload.includeDirs;
    state_.defines = payload.defines;
    state_.diagnostics = payload.diagnostics;
    state_.capturedAt = ProjectState::nowUtc();
    restored_ = false;
    lastError_.clear();
    saveState();
}

void ProjectModel::recordFailure(const QString &error)
{
    lastError_ = error.isEmpty() ? QCoreApplication::translate("VEdit", "the project service failed") : error;
    if (job_ != 0) {
        service_.cancel(job_);
        job_ = 0;
    }
    pending_ = false;
    pollTimer_.stop();
    emit stateChanged();
    emit refreshFinished(false, lastError_);
}

} // namespace vedit::workspace::project
