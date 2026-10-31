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

} // namespace vedit::workspace::diagnostics
