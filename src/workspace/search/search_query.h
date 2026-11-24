#pragma once

// The ONE query option model of the two search surfaces (ux-batch-2 B2).
//
// The search panel (`SearchPanel`, the "search" dock) and the in-view Ctrl+F /
// Ctrl+H bar (`vedit::editor::FindReplaceBar`, src/editor/kernel/find_replace.h)
// expose the SAME four options with the SAME meaning:
//
//   * case sensitive      — 'A' does not match 'a'
//   * whole word          — the match must be bounded by non-word characters
//   * regular expression  — the query is a pattern, not literal text
//   * scope               — CurrentFile (only the active document) or
//                           WholeProject (the project tree, in the dock)
//
// This header is deliberately dependency-free (QtCore only) and header-only:
// the editor kernel is a LOWER layer than the workspace lanes (the search lane
// links vedit_editor_core, not the other way round), so it can include this
// model without creating a link dependency or a second implementation. Both
// surfaces store a `QueryOptions` value; `SearchHub` is the process-wide sync
// seam that keeps their query text + options equal in both directions.
//
// Engine note (also in evidence/ux-batch-2/b2/README.md and, once the captain
// consolidates it, docs/USER_GUIDE.md): the PROJECT-wide engine is the Rust
// `regex` crate (RE2-style: no backreferences, no lookaround), while the
// in-file search uses QRegularExpression (PCRE-like). The OPTIONS mean the same
// thing on both sides; the pattern dialect does not.
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <utility>

namespace vedit::search {

// How many recent queries both surfaces keep (batch 20261110d). Bounded on
// purpose: the history is a convenience, not a log.
constexpr int kSearchHistoryLimit = 20;

// The ONE on-disk form of the search history: a small JSON array of strings,
// newest first, next to the settings store. These helpers are header-only and
// QtCore-only for the same reason the hub is (the editor kernel is a LOWER layer
// and may include this header without a link edge); they never decide WHEN to
// touch the disk — the integration does, once at startup and once at quit, so a
// search itself performs no file IO.
inline QString searchHistoryPath(const QString &stateDir)
{
    if (stateDir.isEmpty()) {
        return QString();
    }
    return stateDir + QStringLiteral("/search-history.json");
}

// Load the bounded history. A missing, unreadable or malformed file is an EMPTY
// history, never an error the caller has to handle: the history is optional and
// a broken file must not stop the editor from starting.
inline QStringList loadSearchHistoryFile(const QString &stateDir)
{
    const QString path = searchHistoryPath(stateDir);
    if (path.isEmpty()) {
        return QStringList();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringList();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return QStringList();
    }
    QStringList entries;
    for (const QJsonValue &value : document.array()) {
        if (!value.isString()) {
            continue;
        }
        const QString query = value.toString().trimmed();
        if (query.isEmpty() || entries.contains(query)) {
            continue;
        }
        entries.append(query);
        if (entries.size() >= kSearchHistoryLimit) {
            break;
        }
    }
    return entries;
}

// Write the history atomically (QSaveFile: a partial file can never be read back
// as a shorter history). Returns false when the directory or the file cannot be
// written; the caller keeps running with the in-memory list.
inline bool saveSearchHistoryFile(const QString &stateDir, const QStringList &history)
{
    const QString path = searchHistoryPath(stateDir);
    if (path.isEmpty()) {
        return false;
    }
    QJsonArray array;
    int kept = 0;
    for (const QString &entry : history) {
        const QString query = entry.trimmed();
        if (query.isEmpty()) {
            continue;
        }
        array.append(query);
        if (++kept >= kSearchHistoryLimit) {
            break;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(QJsonDocument(array).toJson(QJsonDocument::Compact)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

enum class SearchScope {
    CurrentFile = 0,
    WholeProject = 1,
};

// Stable key of a scope (used by evidence/tests; the UI is localised).
inline QString searchScopeKey(SearchScope scope)
{
    return scope == SearchScope::WholeProject ? QStringLiteral("whole-project")
                                              : QStringLiteral("current-file");
}

// The four options both surfaces expose. The defaults are the editor's stricter
// reading (case sensitive, literal, current document); a surface may start its
// widgets in a different state, but the moment the user touches any option the
// value published to the other surface is this exact model.
struct QueryOptions {
    bool caseSensitive = true;
    bool wholeWords = false;
    bool regex = false;
    SearchScope scope = SearchScope::CurrentFile;
};

inline bool operator==(const QueryOptions &left, const QueryOptions &right)
{
    return left.caseSensitive == right.caseSensitive && left.wholeWords == right.wholeWords &&
           left.regex == right.regex && left.scope == right.scope;
}

inline bool operator!=(const QueryOptions &left, const QueryOptions &right)
{
    return !(left == right);
}

// One synchronised search request: what the user typed plus the four options.
struct SearchHandoff {
    QString text;
    QueryOptions options;
};

inline bool operator==(const SearchHandoff &left, const SearchHandoff &right)
{
    return left.text == right.text && left.options == right.options;
}

// Process-wide seam between the two surfaces (GUI thread only).
//
// * `publish()` broadcasts a snapshot to every subscriber except the producer,
//   so a surface never reacts to its own change (no feedback loop).
// * `requestProjectSearch()` publishes AND invokes the dock's project-search
//   handler (the bar's whole-project scope). When no dock is alive the call
//   reports false and the bar says so instead of pretending to search.
// * `currentFilePath` is what the dock's CurrentFile scope searches; the bar
//   publishes its own document path when it is opened.
class SearchHub
{
public:
    static SearchHub &instance()
    {
        static SearchHub hub;
        return hub;
    }

    using Observer = std::function<void(const SearchHandoff &, const void *source)>;
    using ProjectSearchHandler = std::function<bool(const SearchHandoff &)>;

    const SearchHandoff &snapshot() const { return snapshot_; }

    // The last snapshot the Ctrl+F bar published, if any. A newly created dock
    // seeds from THIS (never from another dock's last search): opening the dock
    // shows the bar's live query without pushing any defaults over it
    // (b2-repair Medium 8: subscribe-with-replay, never publish-first).
    void publishFromFindBar(const SearchHandoff &snapshot, const void *source)
    {
        findBarSnapshot_ = snapshot;
        hasFindBarSnapshot_ = true;
        publish(snapshot, source);
    }
    bool hasFindBarSnapshot() const { return hasFindBarSnapshot_; }
    const SearchHandoff &findBarSnapshot() const { return findBarSnapshot_; }

    void publish(const SearchHandoff &snapshot, const void *source)
    {
        if (snapshot == snapshot_) {
            return;
        }
        snapshot_ = snapshot;
        // Copy: an observer may (un)subscribe while being notified.
        const QList<std::pair<int, Observer>> observers = observers_;
        for (const auto &entry : observers) {
            if (entry.second) {
                entry.second(snapshot_, source);
            }
        }
    }

    int subscribe(Observer observer)
    {
        const int id = ++nextObserverId_;
        observers_.append(std::make_pair(id, std::move(observer)));
        return id;
    }

    void unsubscribe(int id)
    {
        for (int index = 0; index < observers_.size(); ++index) {
            if (observers_.at(index).first == id) {
                observers_.removeAt(index);
                return;
            }
        }
    }

    // A handler is owned by `owner` (the QObject pointer) so a destroyed surface
    // removes only its own handler; the fallback registered by installSearch()
    // stays available. The most recently registered live handler wins.
    void setProjectSearchHandler(const void *owner, ProjectSearchHandler handler)
    {
        removeProjectSearchHandler(owner);
        if (handler) {
            handlers_.append(std::make_pair(owner, std::move(handler)));
        }
    }

    void removeProjectSearchHandler(const void *owner)
    {
        for (int index = handlers_.size() - 1; index >= 0; --index) {
            if (handlers_.at(index).first == owner) {
                handlers_.removeAt(index);
                return;
            }
        }
    }

    bool hasProjectSearchHandler() const { return !handlers_.isEmpty(); }

    bool requestProjectSearch(const SearchHandoff &snapshot)
    {
        publish(snapshot, nullptr);
        // Copy before calling: a handler may create the dock, whose panel
        // registers ITSELF as a handler — appending to the list we would
        // otherwise be iterating (and reallocating the running std::function).
        const QList<std::pair<const void *, ProjectSearchHandler>> handlers = handlers_;
        for (int index = handlers.size() - 1; index >= 0; --index) {
            if (handlers.at(index).second && handlers.at(index).second(snapshot)) {
                return true;
            }
        }
        return false;
    }

    void setCurrentFilePath(const QString &path) { currentFilePath_ = path; }
    QString currentFilePath() const { return currentFilePath_; }

    // --- bounded search history (batch 20261110d) --------------------------
    //
    // The hub OWNS the list so both surfaces (the Ctrl+F bar and the dock) feed
    // one history; persistence is INJECTED (`setHistoryStore`) instead of being
    // a link dependency. `noteQuery` is called when a search is actually RUN
    // (Enter / Replace / project hand-off / the dock's scan), never per
    // keystroke. Newest first, duplicates move to the front, bounded by
    // `kSearchHistoryLimit`. GUI thread only, like the rest of the hub.
    using HistoryLoader = std::function<QStringList()>;
    using HistorySaver = std::function<void(const QStringList &)>;

    // `loader` is read once, now (startup); `saver` is called on every change
    // and may be null when the caller persists at another moment (the product
    // saves once at quit, so a search does no file IO).
    void setHistoryStore(HistoryLoader loader, HistorySaver saver)
    {
        historyLoader_ = std::move(loader);
        historySaver_ = std::move(saver);
        if (historyLoader_) {
            history_ = boundedHistory(historyLoader_());
        }
    }

    const QStringList &history() const { return history_; }

    void noteQuery(const QString &query)
    {
        const QString trimmed = query.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        QStringList updated = boundedHistory(history_);
        updated.removeAll(trimmed);
        updated.prepend(trimmed);
        while (updated.size() > kSearchHistoryLimit) {
            updated.removeLast();
        }
        if (updated == history_) {
            return;
        }
        history_ = updated;
        if (historySaver_) {
            historySaver_(history_);
        }
    }

    void clearHistory()
    {
        if (history_.isEmpty()) {
            return;
        }
        history_.clear();
        if (historySaver_) {
            historySaver_(history_);
        }
    }

    // Trim, drop blanks, dedupe (first occurrence wins) and cap — the ONE
    // normalisation both the file loader and the store adopt.
    static QStringList boundedHistory(const QStringList &input)
    {
        QStringList bounded;
        bounded.reserve(std::min<int>(input.size(), kSearchHistoryLimit));
        for (const QString &entry : input) {
            const QString query = entry.trimmed();
            if (query.isEmpty() || bounded.contains(query)) {
                continue;
            }
            bounded.append(query);
            if (bounded.size() >= kSearchHistoryLimit) {
                break;
            }
        }
        return bounded;
    }

private:
    SearchHub() = default;

    SearchHandoff snapshot_;
    SearchHandoff findBarSnapshot_;
    bool hasFindBarSnapshot_ = false;
    QList<std::pair<int, Observer>> observers_;
    QList<std::pair<const void *, ProjectSearchHandler>> handlers_;
    int nextObserverId_ = 0;
    QString currentFilePath_;
    QStringList history_;
    HistoryLoader historyLoader_;
    HistorySaver historySaver_;
};

} // namespace vedit::search
