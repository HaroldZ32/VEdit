#pragma once

// Project-wide replace (ux-batch-2 B2) — the write side of a project search.
//
// The replacement is NOT a second search. Every hit carries the exact character
// range the Rust matcher reported (`SearchHit::column` / `matchLength`, from
// `vd_search_tree_query_ex`), and this engine only APPLIES those ranges:
// grouped per file, one atomic write per file. Before a range is applied it is
// re-checked against the SAME query (literal or RE2-style regex, whole-word
// wrapped exactly like `vd-search` wraps it); a range whose text no longer
// matches is refused with the file and line, so an out-of-date range can never
// overwrite the characters that now sit at that offset. The check produces no
// results — it is a guard, not a second search.
//
// Threading: exactly like GlobalSearch — the batch runs on a worker thread,
// polls a cancellation tag between files, and posts the result back to the GUI
// thread. The GUI thread never reads a file and never splits a line here.
//
// Encoding: a file is rewritten only when it decodes as valid UTF-8 (the Rust
// matcher decodes lossily, so rewriting a Latin-1 file from its lossy text
// would corrupt bytes). Other files are reported in `skippedFiles`, never
// silently rewritten.
#include "search_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

namespace vedit::workspace::search {

// The matcher's own query, carried to the write side so a range can be checked
// against what the matcher would match TODAY before any character is replaced
// (b2-repair High 2). An EMPTY `pattern` is never a "skip validation" mode: the
// replace path refuses it (b2-repair-r2), so a missing query can no longer
// disable the range guard.
struct ReplaceQuery {
    QString pattern;
    bool caseSensitive = true;
    bool wholeWords = false;
    bool regex = false;
};

inline bool operator==(const ReplaceQuery &left, const ReplaceQuery &right)
{
    return left.pattern == right.pattern && left.caseSensitive == right.caseSensitive &&
           left.wholeWords == right.wholeWords && left.regex == right.regex;
}

inline bool operator!=(const ReplaceQuery &left, const ReplaceQuery &right)
{
    return !(left == right);
}

// Cheap identity of the file state the scan saw (b2-repair-r2 High 2 / item 4).
// The worker compares the current size and modification time before reading a
// file; a change refuses the stale ranges instead of applying them to a
// different document state (a line shift can otherwise put the SAME text at the
// old line/column and pass the text check).
//
// `writtenDuringScan` is the SCAN-WINDOW half (b2-repair-r3): the stamp is
// captured when the scan finishes, so on its own it cannot see a save that
// landed while the walk was reading. The panel records the scan's start time and
// flags every hit file whose modification time is after it; a flagged file is
// refused, because its hits describe the pre-save content and its stamp would
// describe the post-save content (both checks would agree while the coordinates
// are stale). It is cleared only when the panel itself saved the file as part of
// the operation (the save-first step), where the new stamp is the authority.
struct ReplaceFileStamp {
    qint64 size = -1;
    qint64 lastModifiedMs = 0;
    bool writtenDuringScan = false;
};

// The IMMUTABLE operation snapshot of one replacement. It is built from the
// scan that produced `hits` (never from live widgets at write time) and carried
// to the worker thread. `scanId` is the matcher identity: every hit tags the
// scan that produced it, and a hit from another scan is refused.
struct ReplaceSnapshot {
    ReplaceQuery query;
    quint64 scanId = 0;
    QHash<QString, ReplaceFileStamp> files;
};

// Applies `hits` (all in `text`, lines 1-based) to `text` and returns the new
// content. Character columns are Unicode scalar values, which is what the Rust
// matcher reports — NOT UTF-16 code units and NOT bytes. A hit that does not
// fit the current text is an error (the file changed under the search). Every
// range is re-checked against `query` (literal or RE2-style regular expression,
// whole-word wrapped exactly as `vd-search` wraps it) and a mismatch refuses the
// whole call instead of writing the wrong characters. An EMPTY `query.pattern`
// is refused too: there is no "write without verification" path.
struct TextReplaceResult {
    bool ok = false;
    int replaced = 0;
    QString text;
    QString error;
};
TextReplaceResult applyReplaceRanges(const QString &text, const QList<ReplaceHit> &hits,
                                     const QString &replacement);
TextReplaceResult applyReplaceRanges(const QString &text, const QList<ReplaceHit> &hits,
                                     const QString &replacement, const ReplaceQuery &query);

// One 1-based line of a document, decoded EXACTLY as the replace writer decodes
// it (UTF-8, Stateless | ConvertInitialBom, so line 1 keeps its BOM and the
// columns agree) and split exactly as the writer splits it. Returns a NULL
// QString when the line cannot be read (missing file, not valid UTF-8, a file
// the writer would skip, or fewer lines); an existing but empty line is an
// empty, non-null string. The replace path uses it to prove a hit's position
// with the whole line the scan read when the 400-character preview cannot
// (b2-repair-r4); `error`, when given, names the reason.
QString readDocumentLine(const QString &absolutePath, int line, QString *error = nullptr);

// One per-file write. The product default is `vedit::editor::atomicSaveText`
// (write -> fsync -> rename -> directory fsync); tests inject a counting double
// to prove "each file once".
using FileRewriter = std::function<ReplaceWriteResult(const QString &path, const QByteArray &contents)>;

// The default rewriter, or an empty function when this build does not link the
// editor kernel (a standalone lane build): the caller then reports that replace
// is unavailable instead of inventing a second save path.
FileRewriter atomicSaveRewriter();

class ProjectReplacer final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectReplacer(QObject *parent = nullptr);
    ~ProjectReplacer() override;

    ProjectReplacer(const ProjectReplacer &) = delete;
    ProjectReplacer &operator=(const ProjectReplacer &) = delete;

    // Applies every hit. Returns false when a previous run is still alive (the
    // same refuse-to-run-two contract as GlobalSearch::start). `snapshot` is
    // the frozen query + scan identity + per-file stamps of the scan that
    // produced the hits; the worker refuses an empty query, a hit from another
    // scan, or a file that changed after that scan.
    bool start(const QList<ReplaceHit> &hits, const QString &replacement, FileRewriter rewriter,
               const ReplaceSnapshot &snapshot);
    void cancel();
    bool isRunning() const;
    bool ranOnWorkerThread() const;

signals:
    void finished(const vedit::workspace::search::ReplaceOutcome &outcome);

private:
    void run(const QList<ReplaceHit> &hits, const QString &replacement,
             const FileRewriter &rewriter, const ReplaceSnapshot &snapshot, quint64 generation,
             const std::shared_ptr<std::atomic_bool> &cancelTag);

    QPointer<QThread> thread_;
    std::atomic_bool running_{false};
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::atomic_bool ranOnWorkerThread_{false};
    quint64 generation_ = 0;
};

} // namespace vedit::workspace::search
