#pragma once

// Shared types of the workspace search lane (global search, acceptance A2c;
// project-wide replace, ux-batch-2 B2).
//
// The panel (SearchPanel) is the content of the shell's "search" dock; the
// backend (GlobalSearch) walks the project on a worker thread so the UI never
// blocks. The matcher and the directory walk stay in `vd-search`; this lane
// only consumes the C ABI and never re-matches a line to replace text — every
// hit carries the exact character range the Rust matcher reported.
//
// Cross-file replace used to be a non-goal; ux-batch-2 B2 overrides that. The
// replacement itself is NOT a second search: it applies the ranges of an
// all-matches scan (`SearchOptions::allMatches`) file by file through the
// existing atomic save path (src/editor/kernel/atomic_save.h).
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace vedit::workspace::search {

// One user-facing search. `query` is literal by default; `wholeWords` and
// `regex` select the Rust matcher's other modes (`vd_search_tree_query_ex`).
struct SearchOptions {
    QString rootPath;
    QString query;
    bool caseSensitive = false;

    // Rust-side match options (crates/vd-search): whole word wraps the pattern
    // in \b(?:…)\b, regex compiles it as written. Project-wide regex is
    // RE2-style (no backreferences, no lookaround).
    bool wholeWords = false;
    bool regex = false;

    // Report every occurrence on a line as its own hit. The browse path keeps
    // "one row per line" (false, the historical panel behaviour); the
    // replace-all path needs every occurrence (true) — otherwise a line with
    // two matches would only replace the first one.
    bool allMatches = false;

    // When non-empty, only hits whose absolute path equals this file are
    // reported (the dock's "current file" scope). The walk still runs on the
    // worker thread; Qt does not add a second file walk.
    QString fileFilter;

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
    QString filePath;     // absolute path (the open/jump key, never displayed)
    // FIX 1: what the panel shows, from the ONE display rule: relative to
    // the project root when the hit is inside it, absolute otherwise.
    QString relativePath;
    int line = 0;         // 1-based
    int column = 0;       // 1-based CHARACTER column of the match start
    QString text;         // the matched line (elided to a displayable length)

    // Exact match range reported by the Rust matcher: `endColumn` is the
    // 1-based character column immediately AFTER the match, so
    // `endColumn - column == matchLength`. This is the range the replace path
    // applies; it never re-matches the line in Qt.
    int endColumn = 0;
    int matchLength = 0;

    // Identity of the scan that produced this hit (b2-repair-r2). The panel
    // tags every hit it receives; the replace worker only applies hits whose
    // scanId equals the frozen operation snapshot's id, so a row from an older
    // scan can never be applied through a newer operation.
    quint64 scanId = 0;

    // CONTENT evidence of the line the matcher read (b2-repair-r3):
    // `matchedLineFingerprint()` of the scanner's own preview — TRIMMED and
    // capped at 400 characters exactly as `crates/vd-search` reports it, so it
    // is a whole-line identity only while the line fits inside that cap
    // (`matchedLinePreviewTruncated()` tells the two cases apart, b2-repair-r4).
    // The write side recomputes the same fingerprint from the file's current
    // line and refuses a hit whose line no longer holds the text the matcher
    // read; the panel additionally compares the WHOLE line for a modified
    // buffer whose preview is truncated. Empty means "no per-hit evidence
    // supplied" (hand-built ranges in unit tests); the range check still
    // applies in that case.
    QByteArray lineFingerprint;
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

// Where a result row jumps to (1-based, like the UI shows it). The panel emits
// jumpRequested() and forwards the same position to the ONE application-wide
// jump seam (src/workspace/jump/jump_registry.h, AC-7.7) - the search lane owns
// no jump interface and no file-opening code of its own (A-1/T20).
struct JumpRequest {
    QString filePath;
    int line = 1;
    int column = 1;
};

// Explicit Verilog/SystemVerilog classification by file extension. Deliberately
// independent of KSyntaxHighlighting (the ".v means V language" trap).
bool isVerilogFileName(const QString &fileName);

// Fingerprint of one MATCHED LINE exactly as the Rust matcher reports it
// (`preview` in crates/vd-search/src/lib.rs: trimmed, then capped at
// TREE_SNIPPET_CHARS with a trailing ellipsis). The scanner's preview is the
// content the matcher read; hashing it gives the replace path a small per-hit
// identity that can be recomputed from the file at write time. Applying this
// function to a preview is idempotent, so `matchedLineFingerprint(hit.text)` is
// the same value as `matchedLineFingerprint(rawLine)` when the line is
// unchanged (b2-repair-r3).
QByteArray matchedLineFingerprint(const QString &line);

// True when `preview` is the scanner's TRUNCATED form, i.e. the line holds more
// than TREE_SNIPPET_CHARS (400) trimmed characters. `matchedLineFingerprint()`
// hashes exactly that preview, so for a truncated preview the fingerprint is
// NOT a whole-line identity: two lines that share the first 400 trimmed
// characters hash equal. The replace path uses this to decide when it must
// prove the hit's position with the whole line instead of the fingerprint
// (b2-repair-r4, review P5).
bool matchedLinePreviewTruncated(const QString &preview);

// The text both sides of a WHOLE-LINE identity compare: a leading U+FEFF (the
// UTF-8 BOM, a decoding artefact) is dropped and the line is trimmed — the same
// normalisation `matchedLineFingerprint()` applies, so a line that differs only
// in BOM / line ending / surrounding whitespace is the same line. Everything
// after the 400-character preview cap is compared in full (b2-repair-r4).
QString normalizedLineIdentity(const QString &line);

// --- project-wide replace (ux-batch-2 B2) ----------------------------------

// One exact match to replace, as reported by the all-matches scan.
struct ReplaceHit {
    QString filePath;
    int line = 0;       // 1-based
    int column = 0;     // 1-based character column of the match start
    int matchLength = 0;// characters
    quint64 scanId = 0; // the scan that produced the range (frozen identity)
    // Per-hit content evidence (see SearchHit::lineFingerprint). Carried so the
    // write side can compare the CURRENT line with what the matcher read; empty
    // means the range carries no evidence and only the query range check runs.
    QByteArray lineFingerprint;
};

// What the confirmation dialog is shown before 「全部替换」 writes anything.
struct ReplacePlan {
    int fileCount = 0;
    int hitCount = 0;
    QStringList files; // absolute paths, sorted (display + confirmation text)
};

// Result of one 「替换」 or one 「全部替换」 batch.
struct ReplaceOutcome {
    bool ok = false;
    bool cancelled = false;
    int filesChanged = 0;
    int replacements = 0;
    QStringList writtenFiles;
    // Files the replace refused to touch (not valid UTF-8, unreadable, ...).
    // They are reported, never silently rewritten.
    QStringList skippedFiles;
    // Written files whose open editor buffer was NOT reloaded because it no
    // longer holds the content that was written (the user typed after the
    // save-first step). The buffer is left untouched and these files are named
    // in the status line; a reload never discards newer user text.
    QStringList notReloadedFiles;
    QString error;
};

// One per-file write of the replace path; matches AtomicSaveResult's shape but
// keeps this header independent of the editor kernel. The default
// implementation IS atomicSaveText (write -> fsync -> rename -> dir fsync).
struct ReplaceWriteResult {
    bool ok = false;
    QString error;
};

} // namespace vedit::workspace::search
