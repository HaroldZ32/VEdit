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
#include <QList>
#include <QString>

#include <functional>
#include <utility>

namespace vedit::search {

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

private:
    SearchHub() = default;

    SearchHandoff snapshot_;
    SearchHandoff findBarSnapshot_;
    bool hasFindBarSnapshot_ = false;
    QList<std::pair<int, Observer>> observers_;
    QList<std::pair<const void *, ProjectSearchHandler>> handlers_;
    int nextObserverId_ = 0;
    QString currentFilePath_;
};

} // namespace vedit::search
