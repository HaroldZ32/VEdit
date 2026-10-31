#pragma once

// Shared types of the workspace search lane (global search, acceptance A2c).
//
// The panel (SearchPanel) is the content of the shell's "search" dock; the
// backend (GlobalSearch) walks the project on a worker thread so the UI never
// blocks. Cross-file replace is an explicit non-goal (requirements rev 2,
// non-goal 5) and is not implemented here.
#include <QList>
#include <QString>
#include <QStringList>

namespace vedit::workspace::search {

// One user-facing search. `query` is matched literally (no regex / glob): the
// requirement is text search, and a literal match keeps the result contract
// simple and testable.
struct SearchOptions {
    QString rootPath;
    QString query;
    bool caseSensitive = false;

    // Restrict to Verilog / SystemVerilog source files. The classification is
    // an explicit extension table (isVerilogFileName) and NOT
    // KSyntaxHighlighting: in this image definitionForFileName("x.v") resolves
    // to the "V" (vlang) definition, see evidence/m0-build/REPORT.md §4.9.
    bool verilogOnly = false;

    // Directory names pruned from the walk (basename match). Empty means
    // defaultExcludedDirNames().
    QStringList excludedDirNames;

    int maxResults = 2000;
    // Files larger than this are skipped and counted (never read into memory).
    qint64 maxFileBytes = 8LL * 1024 * 1024;
    // Wall-clock budget of one scan; when it is exhausted the scan stops and
    // the summary says so (timedOut), instead of freezing the UI.
    qint64 maxDurationMs = 15000;
};

QStringList defaultExcludedDirNames();

struct SearchHit {
    QString filePath;     // absolute path
    QString relativePath; // relative to the search root, for display
    int line = 0;         // 1-based
    int column = 0;       // 1-based, first match on the line
    QString text;         // the matched line (elided to a displayable length)
};

enum class SearchState { Idle, Running, Done, Cancelled, Failed };

QString searchStateName(SearchState state);

// What one scan did, including the skips: the acceptance asks for observable
// behaviour on large / binary / unreadable input, so these counters are part of
// the result rather than a hidden implementation detail.
struct SearchSummary {
    SearchState state = SearchState::Idle;
    int filesScanned = 0;
    int filesMatched = 0;
    int hitCount = 0;
    int skippedBinary = 0;
    int skippedTooLarge = 0;
    int skippedUnreadable = 0; // unreadable files and directories
    bool truncated = false;    // maxResults reached
    bool timedOut = false;     // maxDurationMs reached
    qint64 elapsedMs = 0;
    QString error; // set when state == Failed
};

// Where a result row jumps to. The panel emits jumpRequested() and, when a
// target is installed, also forwards to it.
struct JumpRequest {
    QString filePath;
    int line = 1;
    int column = 1;
};

// Editor seam: the product target is installed by installSearch()
// (search_jump_target.h, EditorJumpTarget), so the click -> open file ->
// position cursor path runs inside vedit-app; the edit-core lane replaces that
// implementation once its real document exists, without changing this contract.
// A panel built without a shell (the lane's standalone build) keeps the
// signal-only behaviour and is driven by a harness-supplied target.
class IJumpTarget {
public:
    virtual ~IJumpTarget() = default;
    virtual void jumpTo(const JumpRequest &request) = 0;
};

// Explicit Verilog/SystemVerilog classification by file extension. Deliberately
// independent of KSyntaxHighlighting (the ".v means V language" trap).
bool isVerilogFileName(const QString &fileName);

} // namespace vedit::workspace::search
