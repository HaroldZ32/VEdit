#pragma once

// VHDL LSP session (batch 20261110c, AC-6.2).
//
// The product's Verilog LSP session lives in the Rust service layer
// (`vd-lsp` -> `vd_ffi`), whose two backends are verible and slang and whose
// `languageId` mapping sends every non-`.sv` file as `verilog`. vhdl_ls is a
// different language server: it needs `languageId: "vhdl"`, its own `vhdl_ls.toml`
// library mapping and its own child process. The Rust session is therefore the
// WRONG seam for VHDL (and the C ABI has no VHDL backend), so this lane owns a
// small, strictly asynchronous JSON-RPC client of its own.
//
// Rules this class keeps (they are the product's subprocess discipline):
//   * no synchronous spawn/wait — `QProcess` is driven by its signals, and every
//     request has a timer, so the GUI thread never blocks on the child;
//   * argv arrays only, no shell;
//   * the child is shut down with `shutdown`/`exit`, then terminated;
//   * the session never spawns verible/slang — a VHDL file must not reach the
//     Verilog backends (AC-6.2).
//
// The server's own behaviour was measured (vhdl_ls 0.88.0, the pinned build):
//   * without a workspace `vhdl_ls.toml` it only warns ("Library mapping is
//     unknown … semantic analysis might be incorrect") and publishes NO
//     diagnostics;
//   * with `[libraries] work.files = [...]` it publishes real diagnostics and
//     answers `textDocument/definition` (cross-file) and `textDocument/hover`.
// The evidence log records both states.

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QTimer;

namespace vedit::workspace {

/// One diagnostic as `textDocument/publishDiagnostics` delivered it.
struct VhdlDiagnostic {
    QString file;    // absolute path (the URI mapped to a local file)
    int line = 0;    // zero-based start line
    int column = 0;  // zero-based start character
    int endLine = 0;
    int endColumn = 0;
    /// LSP severity: 1 error, 2 warning, 3 information, 4 hint.
    int severity = 0;
    QString code;
    QString source;
    QString message;
};

/// True for `.vhd` / `.vhdl` (case-insensitive). The ONE place the lane decides
/// what a VHDL document is, so the routing cannot drift between callers.
bool isVhdlFile(const QString &path);

/// AC-6.3: where the workspace's `vhdl_ls.toml` came from for one session root.
///
/// The lane generates the DEFAULT mapping (every project `.vhd`/`.vhdl` under ONE
/// ordinary library) when the workspace has none, for two MEASURED reasons, and
/// deliberately does NOT claim a third:
///   * COVERAGE — without a mapping the server only knows the documents the editor
///     opened: a correct reference into an unopened file becomes
///     `No primary unit ...` + `No declaration of ...`, while the same sources with
///     a mapping resolve (mapping at the root vs no mapping, one file opened);
///   * WARNING STATE — `<root>/vhdl_ls.toml` is the only channel the server reports
///     as "Loaded workspace root configuration file"; a `VHDL_LS_CONFIG` file still
///     leaves it warning `Library mapping is unknown …` and `Without library mapping
///     semantic analysis might be incorrect`, even though navigation may resolve.
///   * NOT claimed: that navigation only works with the file at the root. It does
///     not — every arrangement (root file, `VHDL_LS_CONFIG` absolute/relative, no
///     mapping at all) resolves when the files are opened. The directory variable
///     was measured and is irrelevant.
/// `work` is the implicit library of the analysed unit and is REJECTED as a config
/// key ("The 'work' library is not a valid library"), so `use work.x` resolves
/// through the library the files are declared in.
struct VhdlConfigReport {
    enum class Source {
        User,       // the workspace already had vhdl_ls.toml — used verbatim
        Generated,  // the lane wrote the default one because it was missing
        Missing,    // no config and it could not be written (reason in `error`)
    };
    Source source = Source::Missing;
    QString path;          // the file the server will read ("" when Missing)
    QString error;         // why the config is unusable (never silently ignored)
    int files = 0;         // files listed by the GENERATED config
    QStringList libraries; // library names the file declares (best effort)
    QString text() const;  // one log line: source=… path=… files=…
};

/// AC-6.3: resolve the workspace config for `root`, generating the default one
/// when the workspace has none. Never overwrites an existing file; a root that
/// cannot be written is reported, not worked around.
VhdlConfigReport prepareVhdlConfig(const QString &root);

/// One target of a definition answer.
struct VhdlLocation {
    QString file;
    int line = 0;
    int column = 0;
};

/// A real `vhdl_ls` session: initialize handshake, didOpen, published
/// diagnostics, hover and definition.
///
/// Lifecycle: `Idle -> Starting -> Ready | Failed`, then `stop()`.
/// `ready()` is the readiness barrier a caller waits for; `readyChanged` is
/// emitted once the server answered `initialize` (never before).
class VhdlLspSession : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Starting,
        Ready,
        Failed,
    };

    explicit VhdlLspSession(QObject *parent = nullptr);
    ~VhdlLspSession() override;

    /// Spawn `program` inside `workspaceRoot` and begin the initialize handshake.
    /// Returns false only when the process cannot be started at all.
    bool start(const QString &program, const QString &workspaceRoot);
    /// `shutdown` + `exit`, then a bounded kill. Idempotent.
    void stop();

    State state() const { return state_; }
    QString stateName() const;
    QString program() const { return program_; }
    QString workspaceRoot() const { return workspaceRoot_; }
    qint64 processId() const;
    QString errorText() const { return error_; }
    /// Every `window/showMessage` / `window/logMessage` text the server sent.
    /// vhdl_ls reports its own limitation here ("Library mapping is unknown …"),
    /// so the absence of a configuration is observable instead of implied.
    QStringList serverMessages() const { return messages_; }
    /// AC-6.3: true when the server REFUSED the workspace config (bad TOML or an
    /// invalid library name). The process keeps running — a rejected config is a
    /// state to show, never a crash.
    bool configRejected() const { return configRejected_; }
    QString configError() const { return configError_; }
    QStringList openedFiles() const { return opened_; }
    int filesOpened() const { return static_cast<int>(opened_.size()); }
    bool ready() const { return state_ == State::Ready; }

    /// The `initialize` result's capabilities, as booleans (measured, not assumed).
    bool hasDefinition() const { return definitionProvider_; }
    bool hasHover() const { return hoverProvider_; }
    bool hasDocumentSymbols() const { return documentSymbolProvider_; }

    /// didOpen one document. The language id is ALWAYS `vhdl`. Idempotent.
    bool openDocument(const QString &path);
    bool isOpen(const QString &path) const { return opened_.contains(path); }

    /// Diagnostics the server published for `path` (empty until the first
    /// publication; vhdl_ls publishes per file after didOpen/didChange).
    QVector<VhdlDiagnostic> diagnostics(const QString &path) const;
    int diagnosticCount() const;

    /// `textDocument/hover` and `textDocument/definition` at a zero-based
    /// position. The answers arrive as `hoverAnswered` / `definitionAnswered`.
    bool requestHover(const QString &path, int line, int column);
    bool requestDefinition(const QString &path, int line, int column);

signals:
    void readyChanged(bool ready);
    /// The workspace `vhdl_ls.toml` was refused by the server (one explicit line).
    void configRefused(const QString &reason);
    void diagnosticsPublished(const QString &file, int count);
    void hoverAnswered(const QString &file, int line, int column, const QString &text);
    void definitionAnswered(const QString &file, int line, int column,
                            const QVector<vedit::workspace::VhdlLocation> &locations);
    void failed(const QString &reason);

private:
    enum class PendingKind {
        Hover,
        Definition,
    };
    struct PendingRequest {
        PendingKind kind = PendingKind::Hover;
        QString tag;  // request name used in the log, never sent on the wire
        QString file;
        int line = 0;
        int column = 0;
        QTimer *timer = nullptr;
    };

    void onStarted();
    void onReadyRead();
    void onProcessError(int error);
    void onFinished(int exitCode, int status);
    void feed(const QByteArray &chunk);
    void dispatch(const QJsonObject &message);
    void handleResponse(const QJsonObject &response);
    void handleNotification(const QJsonObject &notification);
    void send(const QJsonObject &message);
    void sendRequest(const QString &method, const QJsonObject &params,
                     const QString &tag, const PendingRequest &pending);
    void sendNotification(const QString &method, const QJsonObject &params);
    void noteServerMessage(const QString &text);
    void markReady();
    void fail(const QString &reason);
    void clearPending();

    QProcess *process_ = nullptr;
    QTimer *initializeTimer_ = nullptr;
    QString program_;
    QString workspaceRoot_;
    QString error_;
    QByteArray buffer_;
    QStringList opened_;
    QStringList messages_;
    bool configRejected_ = false;
    QString configError_;
    QHash<QString, QVector<VhdlDiagnostic>> diagnostics_;
    QHash<qint64, PendingRequest> pending_;
    qint64 nextId_ = 1;
    State state_ = State::Idle;
    bool definitionProvider_ = false;
    bool hoverProvider_ = false;
    bool documentSymbolProvider_ = false;
    bool stopping_ = false;
};

} // namespace vedit::workspace
