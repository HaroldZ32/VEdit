#pragma once

// Plain data types of the diagnostics lane (Verible LSP + SLANG).
//
// No Qt widget and no FFI type appears here: the controller, the C ABI backends
// and the headless self-test all exchange these values, which is what makes the
// A3c/A3d assertions on the effective file set possible without a display.

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace vedit::workspace::diagnostics {

/// Lifecycle of the joint Verible/SLANG reparse (A3d observable state).
enum class BackendState {
    Idle = 0,      ///< no file set yet
    Reparsing = 1, ///< a reparse is in flight
    Ready = 2,     ///< every backend answered
    Degraded = 3,  ///< one backend answered, another failed (diagnosable)
    Failed = 4,    ///< no backend could answer
};

/// One diagnostic, already positioned for the editor's problem model.
struct DiagnosticEntry {
    QString file;
    int line = 0;      ///< zero-based start line
    int column = 0;    ///< zero-based UTF-16 start column
    int endLine = 0;   ///< zero-based end line
    int endColumn = 0; ///< zero-based UTF-16 end column
    int severity = 0;  ///< 1 error, 2 warning, 3 information, 4 hint, 0 unknown
    QString code;      ///< rule/code when the producer supplied one
    QString source;    ///< "verible" / "slang" / backend name
    QString message;
};

/// One outline entry produced from `textDocument/documentSymbol`.
struct OutlineSymbol {
    QString name;
    QString detail;
    int kind = 0; ///< LSP SymbolKind number
    int line = 0;
    int column = 0;
};

/// One navigation target.
struct LspLocation {
    QString uri;
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
};

/// One edit returned by formatting or rename (the caller applies it).
struct LspTextEdit {
    QString uri;
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;
    QString newText;
};

/// Snapshot of the LSP session, mirroring the C ABI `VdLspStatus`.
struct SessionSnapshot {
    BackendState state = BackendState::Idle;
    int filesExpected = 0; ///< files handed to the session (the effective set)
    int filesOpened = 0;   ///< files the server accepted
    int diagnostics = 0;
    quint64 generation = 0;
    int serverPid = 0;
    int restarts = 0;
    QString message;
    QStringList skipped; ///< "path: reason"
};

/// Kind of one polled response.
enum class ResponseKind {
    None = 0,
    Locations = 1,
    Symbols = 2,
    Edits = 3,
    Diagnostics = 4,
    Hover = 5,
    SignatureHelp = 6,
};

/// One call signature of a `textDocument/signatureHelp` answer (T16).
struct LspSignature {
    QString label;
    QStringList parameters;
};

/// Result of polling one backend request; exactly one payload field is filled.
struct LspPolled {
    bool pending = true;
    bool ready = false;
    bool failed = false;
    QString error;
    ResponseKind kind = ResponseKind::None;
    QVector<LspLocation> locations;
    QVector<OutlineSymbol> symbols;
    QVector<LspTextEdit> edits;
    QVector<DiagnosticEntry> diagnostics;
    /// Hover text, when `kind` is Hover.
    QString hoverText;
    /// Signatures, when `kind` is SignatureHelp.
    QVector<LspSignature> signatures;
    int activeSignature = -1;
    int activeParameter = -1;

    static LspPolled pendingResult() { return LspPolled{}; }
    static LspPolled failure(const QString &message)
    {
        LspPolled result;
        result.pending = false;
        result.failed = true;
        result.error = message;
        return result;
    }
};

/// A3c report: which files reached which backend.
struct FileSetReport {
    QStringList files;             ///< the effective file set as handed in
    bool lspReceivedWholeSet = false; ///< filesOpened == files.size()
    int lspFilesOpened = 0;
    int slangRequested = 0;  ///< SLANG lint attempts started
    int slangCompleted = 0;  ///< SLANG lint attempts that reached a terminal state
    int slangFailures = 0;   ///< SLANG lint attempts that failed (missing binary, timeout)
    bool slangAvailable = false;
    QString lspMessage;
    QString slangMessage;
};

// --- T8: project-wide lint pass + display-level merge (AC-2.6..2.14) --------

/// Phase of a project-wide CLI lint pass (mirrors the C ABI `VdLintProgress`).
enum class LintPhase {
    Idle = 0,      ///< no pass has run yet
    Queued = 1,
    Running = 2,
    Finished = 3,
    Cancelled = 4,
    Failed = 5,
};

/// Observable snapshot of the project pass; the progress widget renders exactly
/// this and the controller polls it from `refresh()`.
struct LintPassSnapshot {
    LintPhase phase = LintPhase::Idle;
    int done = 0;         ///< files already linted
    int total = 0;        ///< files in the pass snapshot
    QString current;      ///< file being linted (empty when idle/terminal)
    QString tool;         ///< "slang" / "verible-verilog-lint" (or fake)
    QString stage;        ///< failure stage: resolve / spawn / run / parse
    QString message;      ///< one line for the progress bar / status

    bool running() const { return phase == LintPhase::Queued || phase == LintPhase::Running; }
    bool terminal() const
    {
        return phase == LintPhase::Finished || phase == LintPhase::Cancelled
            || phase == LintPhase::Failed;
    }
    int percent() const { return total <= 0 ? 0 : (done * 100) / total; }
};

/// Inputs of one project pass. `files` is the project file set (already the
/// union); the controller subtracts the OPEN-FILE snapshot (C8) before start.
struct LintPassRequest {
    QStringList files;
    QStringList includeDirs; ///< -I from the project model
    QStringList defines;     ///< -D from the project model
    int engine = 0;          ///< 0 slang, 1 verible-verilog-lint
    QString slangPath;
    QString veriblePath;
    QString rulesPath;
    int timeoutMs = 0;
    int maxDiagnostics = 0;
};

/// Where a displayed diagnostic came from (AC-2.13).
enum class DiagnosticOrigin {
    Lsp = 0,
    Cli = 1,
    Both = 2,
};

/// One row of the merged display list: the canonical entry (LSP entry wins for
/// a merged row) plus where it came from.
struct DisplayDiagnostic {
    DiagnosticEntry entry;
    DiagnosticOrigin origin = DiagnosticOrigin::Lsp;
    QString key; ///< path + start line + severity + normalized message
};

/// The AC-2.13 merge key: file path, 0-based start line, NORMALIZED severity
/// (both producers' scales map to Error/Warning/Info/Hint/Unknown) and the
/// whitespace-normalized message.
QString diagnosticMergeKey(const DiagnosticEntry &entry, DiagnosticOrigin origin);

/// LEGACY (R2): merges the LSP rows with the CLI rows and marks a row found in
/// both as `Both` - i.e. it STACKS the two layers, which R2 forbids for the
/// product surfaces. It is kept only for the historical orchestration probe
/// (`evidence/w4-lint/lint_orchestration_probe.cpp`) and must not be used by a
/// product path; the product reads `DiagnosticsController`'s layered view
/// (`mergedDiagnostics()` / `displayDiagnostics()`), which never emits `Both`.
QVector<DisplayDiagnostic> mergeDiagnostics(const QVector<DiagnosticEntry> &lsp,
                                            const QVector<DiagnosticEntry> &cli);

/// The skipped-lines sentence used by the panel/status line, e.g.
/// `cancelled — 120/1000 file(s)` (the zh catalogue renders
/// `已取消 — 120/1000 文件`).
QString lintCancelledMessage(int done, int total);

} // namespace vedit::workspace::diagnostics
