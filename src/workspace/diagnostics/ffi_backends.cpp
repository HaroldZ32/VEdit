// C ABI implementation of the diagnostics seams (Verible LSP + SLANG).
//
// Only this file touches the generated `vd_ffi.h`; the controller and the
// self-tests stay Qt-only. Every string crossing the ABI is copied by the Rust
// side (the caller keeps ownership of the `VdStringList` it builds), and every
// Rust-owned result is released with its `vd_*_free` function.

#include "diagnostics_integration.h"

#include "lsp_backend.h"
#include "slang_backend.h"

// UX batch 2 / A4: the ONE seam the Preferences Lint page reaches through. The
// header is header-only at this boundary (buildLintJobOptions is inline), so the
// diagnostics lane gains no link dependency on the settings library.
#if __has_include("../settings/lint_options.h")
#include "../settings/lint_options.h"
#define VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS 1
#endif

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <QVector>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)

#include <vd_ffi.h>
#include <QCoreApplication>

namespace vedit::workspace::diagnostics {

namespace {

BackendState mapPhase(int phase)
{
    switch (phase) {
    case 0:
        return BackendState::Reparsing; // starting
    case 1:
        return BackendState::Ready;
    case 2:
        return BackendState::Failed;
    default:
        return BackendState::Idle; // stopped
    }
}

/// The lint rule of a diagnostic whose producer sends it inside the MESSAGE.
///
/// `verible-verilog-ls` publishes **no `code` at all**: measured against the
/// pinned v0.0-4298 server, a whole published diagnostic is
/// `{"message", "range", "severity", "source"}` and the rule is spelled
/// `"Remove trailing spaces. [Style: trailing-spaces][no-trailing-spaces] (fix
/// available)"`. The rule id is the LAST bracketed token (Verible's `[Style: x]`
/// class prefix is stripped).
///
/// THE AUTHORITATIVE IMPLEMENTATION IS THE RUST `vd_lsp::diagnostic_rule()`
/// (crates/vd-lsp/src/diagnostics.rs): it prefers the protocol `code` and
/// otherwise parses the trailing bracketed tokens exactly this way. This is a
/// deliberate MIRROR of it, because `normalise_diagnostics()` in vd-lsp is the
/// one place that should call it and this lane cannot reach that code; the
/// canonical fix is to have the normaliser use the helper and delete this copy
/// (follow-up card - see evidence/batch-20261110c/diag-data/REPORT.md). The
/// mirror is pinned by a cross-check against the Rust helper on the SAME real
/// server messages (same evidence file).
///
/// One deliberate difference from the Rust helper: a PURELY NUMERIC token is not
/// a rule id - a `[12]` inside a message is a value, and a waiver written with
/// `--rule=12` would be a fabricated suppression. The scan therefore remembers
/// the last token that is bracket-delimited, space-free and carries at least one
/// letter.
QString ruleFromMessage(const QString &message)
{
    QString candidate;
    int cursor = 0;
    while (cursor < message.size()) {
        const int open = message.indexOf(QLatin1Char('['), cursor);
        if (open < 0) {
            break;
        }
        const int close = message.indexOf(QLatin1Char(']'), open + 1);
        if (close < 0) {
            break;
        }
        QString token = message.mid(open + 1, close - open - 1).trimmed();
        const QString stylePrefix = QStringLiteral("Style: ");
        if (token.startsWith(stylePrefix)) {
            token = token.mid(stylePrefix.size()).trimmed();
        }
        const bool hasLetter = std::any_of(token.cbegin(), token.cend(), [](QChar character) {
            return character.isLetter();
        });
        if (!token.isEmpty() && !token.contains(QLatin1Char(' ')) && hasLetter) {
            candidate = token; // the last well-formed token wins
        }
        cursor = close + 1;
    }
    return candidate;
}

/// Map one C ABI diagnostic. `defaultSource` is the SESSION BACKEND's name and
/// labels a row the server sent without a source (batch-20261117 F4): slang-
/// server's rows carried `[verible]` in the Outline because the empty ABI field
/// fell through to a hard-coded verible default, which misreports provenance.
/// A source the server DID send is kept verbatim - verible rows stay verible -
/// and message/range/severity/code are untouched.
DiagnosticEntry mapDiagnostic(const VdLspDiagnostic &raw, const QString &file,
                              const QString &defaultSource)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = static_cast<int>(raw.range.start_line);
    entry.column = static_cast<int>(raw.range.start_character);
    entry.endLine = static_cast<int>(raw.range.end_line);
    entry.endColumn = static_cast<int>(raw.range.end_character);
    entry.severity = raw.severity;
    const QString serverSource =
        raw.source != nullptr ? QString::fromUtf8(raw.source) : QString();
    entry.source = serverSource.isEmpty() ? defaultSource : serverSource;
    entry.message = raw.message != nullptr ? QString::fromUtf8(raw.message) : QString();
    entry.code = raw.code != nullptr ? QString::fromUtf8(raw.code) : QString();
    if (entry.code.isEmpty()) {
        // AC-5.8: a row without a rule id cannot be waived per diagnostic
        // (waiver_actions.cpp refuses it), so a verible row would be
        // un-waivable from the panel. The rule the server spelled into the
        // message IS the identifier verible's own `waive --rule=` takes.
        entry.code = ruleFromMessage(entry.message);
    }
    return entry;
}

LspLocation mapLocation(const VdLspLocation &raw)
{
    LspLocation location;
    location.uri = raw.uri != nullptr ? QString::fromUtf8(raw.uri) : QString();
    location.startLine = static_cast<int>(raw.range.start_line);
    location.startCharacter = static_cast<int>(raw.range.start_character);
    location.endLine = static_cast<int>(raw.range.end_line);
    location.endCharacter = static_cast<int>(raw.range.end_character);
    return location;
}

LspTextEdit mapEdit(const VdLspTextEdit &raw)
{
    LspTextEdit edit;
    edit.uri = raw.uri != nullptr ? QString::fromUtf8(raw.uri) : QString();
    edit.startLine = static_cast<int>(raw.range.start_line);
    edit.startCharacter = static_cast<int>(raw.range.start_character);
    edit.endLine = static_cast<int>(raw.range.end_line);
    edit.endCharacter = static_cast<int>(raw.range.end_character);
    edit.newText = raw.new_text != nullptr ? QString::fromUtf8(raw.new_text) : QString();
    return edit;
}

OutlineSymbol mapSymbol(const VdLspSymbol &raw)
{
    OutlineSymbol symbol;
    symbol.name = raw.name != nullptr ? QString::fromUtf8(raw.name) : QString();
    symbol.detail = raw.detail != nullptr ? QString::fromUtf8(raw.detail) : QString();
    symbol.kind = raw.kind;
    symbol.line = static_cast<int>(raw.range.start_line);
    symbol.column = static_cast<int>(raw.range.start_character);
    return symbol;
}

QString serviceError(VdService *service)
{
    VdError error{};
    if (service != nullptr) {
        vd_service_error(service, &error);
    }
    return QString::fromUtf8(error.message);
}

/// Copy a capability table out of the ABI (the caller frees the raw struct).
BackendCapabilities mapCapabilities(const VdLspCapabilities &raw)
{
    BackendCapabilities capabilities;
    capabilities.backend = raw.backend != nullptr ? QString::fromUtf8(raw.backend) : QString();
    capabilities.basis = raw.basis != nullptr ? QString::fromUtf8(raw.basis) : QString();
    capabilities.source = raw.source != nullptr ? QString::fromUtf8(raw.source) : QString();
    capabilities.note = raw.note != nullptr ? QString::fromUtf8(raw.note) : QString();
    for (size_t index = 0; index < raw.count; ++index) {
        const VdLspCapability &item = raw.items[index];
        CapabilityRow row;
        row.action = item.action != nullptr ? QString::fromUtf8(item.action) : QString();
        row.supported = item.supported;
        row.reason = item.reason != nullptr ? QString::fromUtf8(item.reason) : QString();
        capabilities.rows.append(row);
    }
    return capabilities;
}

QString takeOwnedString(char *text)
{
    if (text == nullptr) {
        return QString();
    }
    const QString value = QString::fromUtf8(text);
    vd_string_free(text);
    return value;
}

/// Owns the Rust `VdService` handle for one backend.
class ServiceHandle {
public:
    ServiceHandle()
        : service_(vd_service_create())
    {
    }
    ~ServiceHandle()
    {
        if (service_ != nullptr) {
            vd_service_destroy(service_);
        }
    }
    ServiceHandle(const ServiceHandle &) = delete;
    ServiceHandle &operator=(const ServiceHandle &) = delete;

    VdService *get() const { return service_; }
    QString lastError() const { return serviceError(service_); }

private:
    VdService *service_ = nullptr;
};

/// Build the borrowed C view of a QStringList. The caller keeps `backups` and
/// `pointers` alive for the duration of the C call; Rust copies every string.
struct FileList {
    QVector<QByteArray> backups;
    QVector<char *> pointers;

    explicit FileList(const QStringList &files)
    {
        backups.reserve(files.size());
        pointers.reserve(files.size());
        for (const QString &file : files) {
            backups.append(file.toUtf8());
        }
        for (QByteArray &backup : backups) {
            pointers.append(backup.data());
        }
    }

    VdStringList view() const
    {
        VdStringList list;
        list.count = static_cast<size_t>(pointers.size());
        list.items = pointers.isEmpty() ? nullptr : const_cast<char **>(pointers.constData());
        return list;
    }
};

class FfiLspSessionBackend final : public LspSessionBackend {
public:
    FfiLspSessionBackend() = default;
    ~FfiLspSessionBackend() override { stop(); }

    bool available() const { return service_.get() != nullptr; }

    bool start(const QString &lsPath,
               const QString &workspaceRoot,
               const QString &rulesPath,
               const QStringList &files,
               QString *error) override
    {
        stop();
        if (service_.get() == nullptr) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "the Rust service handle could not be created");
            }
            return false;
        }
        const QString effectiveServer = serverPath_.isEmpty() ? lsPath : serverPath_;
        FileList fileList(files);
        FileList includeList(includeDirs_);
        FileList defineList(defines_);
        const QByteArray ls = effectiveServer.toUtf8();
        const QByteArray root = workspaceRoot.toUtf8();
        const QByteArray rules = rulesPath.toUtf8();
        const VdStringList filesView = fileList.view();
        const VdStringList includeView = includeList.view();
        const VdStringList defineView = defineList.view();
        // AC-2.5: record the argv the session is about to use (no process yet).
        commandLine_ = readCommandLine(effectiveServer, workspaceRoot, rulesPath);
        session_ = vd_lsp_session_start_v2(service_.get(),
                                           ls.constData(),
                                           root.constData(),
                                           rulesPath.isEmpty() ? nullptr : rules.constData(),
                                           &filesView,
                                           static_cast<int32_t>(backend_),
                                           includeDirs_.isEmpty() ? nullptr : &includeView,
                                           defines_.isEmpty() ? nullptr : &defineView);
        if (session_ == 0) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return false;
        }
        // Once the session exists its own argv dump is authoritative.
        if (char *dump = vd_lsp_session_command_line(service_.get(), session_)) {
            commandLine_ = takeOwnedString(dump);
        }
        return true;
    }

    void stop() override
    {
        if (session_ != 0) {
            vd_lsp_session_stop(service_.get(), session_);
            vd_lsp_session_free(service_.get(), session_);
            session_ = 0;
        }
    }

    SessionSnapshot status() const override
    {
        SessionSnapshot snapshot;
        if (session_ == 0 || service_.get() == nullptr) {
            snapshot.state = BackendState::Idle;
            snapshot.message = QCoreApplication::translate("VEdit", "no LSP session");
            return snapshot;
        }
        VdLspStatus raw{};
        const int status = vd_lsp_session_status(service_.get(), session_, &raw);
        if (status != VD_STATUS_READY) {
            snapshot.state = BackendState::Failed;
            snapshot.message = service_.lastError();
            return snapshot;
        }
        snapshot.state = mapPhase(raw.phase);
        snapshot.filesExpected = static_cast<int>(raw.files_expected);
        snapshot.filesOpened = static_cast<int>(raw.files_opened);
        snapshot.diagnostics = static_cast<int>(raw.diagnostics);
        snapshot.generation = raw.generation;
        snapshot.serverPid = static_cast<int>(raw.server_pid);
        snapshot.restarts = static_cast<int>(raw.restarts);
        const QString backend = raw.backend_name != nullptr ? QString::fromUtf8(raw.backend_name)
                                                            : backendName();
        // AC-3 (batch-20261117): the Outline's preprocessing-error hint keys on
        // the backend that PRODUCED this session, so the snapshot carries the
        // same token the status line prints (never the requested setting, which
        // may already name a backend that has not started).
        snapshot.backendName = backend;
        const QString rawMessage =
            raw.message != nullptr ? QString::fromUtf8(raw.message) : QString();
        // T11/AC-1.2: the observable status line always names the backend that
        // produced the session ("状态行显示 backend=verible|slang").
        snapshot.message = rawMessage.isEmpty()
            ? QStringLiteral("backend=%1").arg(backend)
            : QStringLiteral("backend=%1 · %2").arg(backend, rawMessage);
        for (size_t index = 0; index < raw.skipped.count; ++index) {
            const char *item = raw.skipped.items[index];
            if (item != nullptr) {
                snapshot.skipped.append(QString::fromUtf8(item));
            }
        }
        vd_lsp_status_free(&raw);
        return snapshot;
    }

    quint64 requestDefinition(const QString &file, int line, int character) override
    {
        return request(VD_LSP_REQ_DEFINITION, file, line, character, QString());
    }
    quint64 requestReferences(const QString &file, int line, int character) override
    {
        return request(VD_LSP_REQ_REFERENCES, file, line, character, QString());
    }
    quint64 requestSymbols(const QString &file) override
    {
        return request(VD_LSP_REQ_SYMBOLS, file, 0, 0, QString());
    }
    quint64 requestRename(const QString &file,
                          int line,
                          int character,
                          const QString &newName) override
    {
        return request(VD_LSP_REQ_RENAME, file, line, character, newName);
    }
    quint64 requestFormat(const QString &file) override
    {
        return request(VD_LSP_REQ_FORMAT, file, 0, 0, QString());
    }
    quint64 requestDiagnostics(const QString &file) override
    {
        return request(VD_LSP_REQ_DIAGNOSTICS, file, 0, 0, QString());
    }
    quint64 requestHover(const QString &file, int line, int character) override
    {
        return request(VD_LSP_REQ_HOVER, file, line, character, QString());
    }
    quint64 requestSignatureHelp(const QString &file, int line, int character) override
    {
        return request(VD_LSP_REQ_SIGNATURE_HELP, file, line, character, QString());
    }

    bool changeBuffer(const QString &file, const QString &text, QString *error) override
    {
        if (session_ == 0 || service_.get() == nullptr) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "no LSP session");
            }
            return false;
        }
        const QByteArray path = file.toUtf8();
        const QByteArray body = text.toUtf8();
        const int status = vd_lsp_session_change(service_.get(), session_, path.constData(),
                                                 body.constData());
        if (status != VD_STATUS_READY) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return false;
        }
        return true;
    }

    LspPolled poll(quint64 requestId) override
    {
        if (session_ == 0 || requestId == 0) {
            return LspPolled::failure(QCoreApplication::translate("VEdit", "no LSP request to poll"));
        }
        VdLspResponse raw{};
        const int status = vd_lsp_request_poll(service_.get(), session_, requestId, &raw);
        if (status == VD_STATUS_PENDING) {
            return LspPolled::pendingResult();
        }
        if (status != VD_STATUS_READY) {
            return LspPolled::failure(service_.lastError());
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        switch (raw.kind) {
        case VD_LSP_RES_LOCATIONS:
            polled.kind = ResponseKind::Locations;
            for (size_t index = 0; index < raw.locations.count; ++index) {
                polled.locations.append(mapLocation(raw.locations.items[index]));
            }
            break;
        case VD_LSP_RES_SYMBOLS:
            polled.kind = ResponseKind::Symbols;
            for (size_t index = 0; index < raw.symbols.count; ++index) {
                polled.symbols.append(mapSymbol(raw.symbols.items[index]));
            }
            break;
        case VD_LSP_RES_EDITS:
            polled.kind = ResponseKind::Edits;
            for (size_t index = 0; index < raw.edits.count; ++index) {
                polled.edits.append(mapEdit(raw.edits.items[index]));
            }
            break;
        case VD_LSP_RES_DIAGNOSTICS:
            polled.kind = ResponseKind::Diagnostics;
            polled.published = true;
            for (size_t index = 0; index < raw.diagnostics.count; ++index) {
                // F4: a source-less row is labelled with the backend that
                // produced this session (verible / slang), never a fixed token.
                polled.diagnostics.append(
                    mapDiagnostic(raw.diagnostics.items[index], QString(), backendName()));
            }
            break;
        case VD_LSP_RES_DIAGNOSTICS_UNPUBLISHED:
            // Batch-20261118 AC-2: the server never published a set for this
            // document. An empty answer here is "not analysed", NOT "0
            // problems" - the controller decides the state from `published`.
            polled.kind = ResponseKind::Diagnostics;
            polled.published = false;
            break;
        case VD_LSP_RES_HOVER:
            polled.kind = ResponseKind::Hover;
            polled.hoverText = raw.text != nullptr ? QString::fromUtf8(raw.text) : QString();
            break;
        case VD_LSP_RES_SIGNATURE_HELP:
            polled.kind = ResponseKind::SignatureHelp;
            for (size_t index = 0; index < raw.signatures.count; ++index) {
                const VdLspSignature &rawSignature = raw.signatures.items[index];
                LspSignature signature;
                signature.label = rawSignature.label != nullptr
                                      ? QString::fromUtf8(rawSignature.label)
                                      : QString();
                for (size_t parameter = 0; parameter < rawSignature.parameters.count; ++parameter) {
                    const char *item = rawSignature.parameters.items[parameter];
                    signature.parameters.append(item != nullptr ? QString::fromUtf8(item)
                                                                : QString());
                }
                polled.signatures.append(signature);
            }
            polled.activeSignature = raw.active_signature;
            polled.activeParameter = raw.active_parameter;
            break;
        default:
            break;
        }
        vd_lsp_response_free(&raw);
        return polled;
    }

    // --- T11: backend selection, capability table and argv echo ------------
    bool selectBackend(LspBackendKind kind,
                       const QString &serverPath,
                       const QStringList &includeDirs,
                       const QStringList &defines,
                       QString *error) override
    {
        if (kind != LspBackendKind::Verible && kind != LspBackendKind::Slang) {
            if (error != nullptr) {
                *error = QStringLiteral("unknown LSP backend");
            }
            return false;
        }
        // Stop the old server; the next start() opens the selected one. The
        // controller owns the restart (reparse()/analyzeFile()), so switching
        // never recreates a panel or restarts the application.
        stop();
        backend_ = kind;
        serverPath_ = serverPath;
        includeDirs_ = includeDirs;
        defines_ = defines;
        staticCapabilitiesCache_ = BackendCapabilities{};
        commandLine_.clear();
        return true;
    }

    /// Batch-20261118 AC-1: deliver the project build context to a Ready slang
    /// session. The `-I`/`-D` lists are held here (set by `selectBackend`), so
    /// the controller only has to say "now". Verible is deliberately a no-op:
    /// its `-I`/`-D` already travel in the file list it is started with, and its
    /// behaviour must not change.
    ///
    /// G1-g (batch-20261119): the project's imported `.f` lists are NOT part of
    /// this call any more. The C ABI keeps its third parameter for compatibility
    /// but ignores it (the generated context compiles the SESSION's file set, see
    /// `vd_lsp_session_set_build_context`), and nesting the project `.f` with
    /// `-f` is exactly the G1-d defect: the server re-expanded the whole project
    /// and its own last-processed rule picked a definition the module tree never
    /// chose. So the argument is always null and no `.f` is forwarded.
    bool pushBuildContext(QString *error) override
    {
        if (session_ == 0 || service_.get() == nullptr) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "no LSP session");
            }
            return false;
        }
        if (backend_ != LspBackendKind::Slang) {
            return true;
        }
        FileList includeList(includeDirs_);
        FileList defineList(defines_);
        const VdStringList includeView = includeList.view();
        const VdStringList defineView = defineList.view();
        const int status = vd_lsp_session_set_build_context(
            service_.get(), session_,
            includeDirs_.isEmpty() ? nullptr : &includeView,
            defines_.isEmpty() ? nullptr : &defineView,
            nullptr);
        if (status != VD_STATUS_READY) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return false;
        }
        return true;
    }

    LspBackendKind backendKind() const override { return backend_; }

    QString backendName() const override
    {
        return backend_ == LspBackendKind::Slang ? QStringLiteral("slang")
                                                 : QStringLiteral("verible");
    }

    BackendCapabilities staticCapabilities() const override
    {
        if (!staticCapabilitiesCache_.isEmpty() || service_.get() == nullptr) {
            return staticCapabilitiesCache_;
        }
        VdLspCapabilities raw{};
        if (vd_lsp_backend_capabilities(static_cast<int32_t>(backend_), &raw) != VD_STATUS_READY) {
            return BackendCapabilities{};
        }
        BackendCapabilities capabilities = mapCapabilities(raw);
        vd_lsp_capabilities_free(&raw);
        if (capabilities.backend.isEmpty()) {
            capabilities.backend = backendName();
        }
        staticCapabilitiesCache_ = capabilities;
        return capabilities;
    }

    BackendCapabilities sessionCapabilities() const override
    {
        if (session_ == 0 || service_.get() == nullptr) {
            return staticCapabilities();
        }
        VdLspCapabilities raw{};
        if (vd_lsp_session_capabilities(service_.get(), session_, &raw) != VD_STATUS_READY) {
            return staticCapabilities();
        }
        BackendCapabilities capabilities = mapCapabilities(raw);
        vd_lsp_capabilities_free(&raw);
        if (capabilities.isEmpty()) {
            return staticCapabilities();
        }
        return capabilities;
    }

    QString commandLine() const override { return commandLine_; }

private:
    QString readCommandLine(const QString &serverPath,
                            const QString &workspaceRoot,
                            const QString &rulesPath) const
    {
        if (service_.get() == nullptr || serverPath.isEmpty() || workspaceRoot.isEmpty()) {
            return QString();
        }
        FileList includeList(includeDirs_);
        FileList defineList(defines_);
        const QByteArray ls = serverPath.toUtf8();
        const QByteArray root = workspaceRoot.toUtf8();
        const QByteArray rules = rulesPath.toUtf8();
        const VdStringList includeView = includeList.view();
        const VdStringList defineView = defineList.view();
        return takeOwnedString(vd_lsp_command_line(static_cast<int32_t>(backend_),
                                                   ls.constData(),
                                                   root.constData(),
                                                   rulesPath.isEmpty() ? nullptr : rules.constData(),
                                                   nullptr,
                                                   includeDirs_.isEmpty() ? nullptr : &includeView,
                                                   defines_.isEmpty() ? nullptr : &defineView));
    }

private:
    quint64 request(int kind, const QString &file, int line, int character, const QString &newName)
    {
        if (session_ == 0) {
            return 0;
        }
        const QByteArray fileUtf8 = file.toUtf8();
        const QByteArray nameUtf8 = newName.toUtf8();
        return vd_lsp_session_request(service_.get(),
                                      session_,
                                      kind,
                                      fileUtf8.constData(),
                                      static_cast<uint32_t>(line),
                                      static_cast<uint32_t>(character),
                                      newName.isEmpty() ? nullptr : nameUtf8.constData());
    }

    ServiceHandle service_;
    quint64 session_ = 0;
    LspBackendKind backend_ = LspBackendKind::Verible;
    QString serverPath_;
    QStringList includeDirs_;
    QStringList defines_;
    QString commandLine_;
    mutable BackendCapabilities staticCapabilitiesCache_;
};

class FfiSlangBackend final : public SlangBackend {
public:
    explicit FfiSlangBackend(QString slangPath)
        : slangPath_(std::move(slangPath))
    {
    }
    FfiSlangBackend(const FfiSlangBackend &) = delete;
    FfiSlangBackend &operator=(const FfiSlangBackend &) = delete;
    ~FfiSlangBackend() override
    {
        // A ticket the controller abandoned (another file was analysed first,
        // or the file set was replaced) still holds a one-file batch slot in the
        // Rust service: cancel it and release the id instead of leaving the
        // child running for the lifetime of the process.
        if (service_.get() == nullptr) {
            return;
        }
        for (quint64 job : jobs_) {
            vd_deeplint_batch_cancel(service_.get(), job);
            vd_deeplint_batch_release(service_.get(), job);
        }
    }

    bool available() const override
    {
        // No explicit path => the Rust service performs its own resolution
        // (pre-existing behaviour); an explicit path is pinned by the caller.
        return service_.get() != nullptr;
    }
    QString description() const override
    {
        const QString binary = engine_ == LintEngineKind::VeribleLint
            ? veribleLintPath_
            : slangPath_;
        return binary.isEmpty() ? QStringLiteral("%1 (FFI)").arg(engineName())
                                : QStringLiteral("%1 (FFI): %2").arg(engineName(), binary);
    }

    // --- T11: engine selection (the controller keeps calling startLint) ----
    void selectEngine(LintEngineKind engine,
                      const QString &slangPath,
                      const QString &veribleLintPath,
                      const QStringList &includeDirs,
                      const QStringList &defines,
                      const QString &rulesPath) override
    {
        engine_ = engine;
        slangPath_ = slangPath;
        veribleLintPath_ = veribleLintPath;
        includeDirs_ = includeDirs;
        defines_ = defines;
        rulesPath_ = rulesPath;
    }

    LintEngineKind engineKind() const override { return engine_; }

    QString engineName() const override
    {
        return engine_ == LintEngineKind::VeribleLint ? QStringLiteral("verible-verilog-lint")
                                                      : QStringLiteral("slang");
    }

    int startLint(const QString &file, QString *error) override
    {
        if (service_.get() == nullptr) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "the Rust service handle could not be created");
            }
            return 0;
        }
        // R2 + AC-5.8: ONE knob composition for BOTH layers.
        //
        // This per-file lint used to go through `vd_deeplint_lint_v2`, whose ABI
        // carries `rules` / include dirs / defines and NO waiver field, while
        // the project pass goes through `vd_deeplint_batch_start`, whose options
        // carry the registered `--waiver_files` (verible) / `--waiver-file`
        // (slang) and the ruleset / `-W` / error-limit knobs. The two layers
        // therefore disagreed BY CONSTRUCTION: a waived diagnostic vanished from
        // the project pass and reappeared from this run - and this is the row
        // the panel shows for a file a finished pass does not cover (an edited
        // file, or a view without a pass), so the user saw "I waived it and it
        // is still reported".
        //
        // A ONE-FILE batch is the same engine invocation with the same options,
        // so the layered view cannot disagree with itself any more. The
        // language-server path is untouched: `analyzeFile()` runs LSP-only
        // (diagnostics_controller.cpp: "file scope: the LSP is the fast path"),
        // and its rows never come from here.
        VdLintBatchOptions options{};
        options.engine = static_cast<int32_t>(engine_);
        const QByteArray slang = slangPath_.toUtf8();
        const QByteArray verible = veribleLintPath_.toUtf8();
        options.slang_binary = slang.isEmpty() ? nullptr : slang.constData();
        options.verible_binary = verible.isEmpty() ? nullptr : verible.constData();
        FileList includeList(includeDirs_);
        FileList defineList(defines_);
        options.include_dirs = includeList.view();
        options.defines = defineList.view();
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS) && defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
        // UX batch 2 / A4 + U6 / §0.1 R1: the SAME composition function the
        // Preferences page and the project pass use - the dedicated
        // `lint.verible.rules_config` first, then the legacy
        // `tools.verible_rules` path, then the project's DISCOVERED
        // `.rules.verible_lint` (`VdProjectInfo.rules_path`). The storage must
        // outlive the start call, and it does: both live in this scope.
        settings::LintBatchAbiStorage abiStrings;
        const settings::LintJobOptions jobOptions = settings::buildLintJobOptions(
            settings::currentLintValues(),
            engine_ == LintEngineKind::VeribleLint ? settings::LintBackend::Verible
                                                   : settings::LintBackend::Slang,
            rulesPath_, settings::currentDiscoveredRulesPath());
        settings::fillLintBatchOptions(jobOptions, &options, &abiStrings);
#else
        const QByteArray rules = rulesPath_.toUtf8();
        options.rules_path = rulesPath_.isEmpty() ? nullptr : rules.constData();
#endif
        FileList fileList(QStringList{file});
        const VdStringList files = fileList.view();
        const quint64 job = vd_deeplint_batch_start(service_.get(), &files, &options);
        if (job == 0) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return 0;
        }
        // The batch job id starts at 1<<32 (`FIRST_BATCH_ID` in
        // crates/vd-ffi/src/deeplint.rs) and this interface's ticket is an int,
        // so a small local ticket is handed out and the ABI id kept behind it.
        const int ticket = ++nextTicket_;
        jobs_.insert(ticket, job);
        return ticket;
    }

    LspPolled pollLint(int ticket) override
    {
        if (service_.get() == nullptr || ticket <= 0) {
            return LspPolled::failure(QCoreApplication::translate("VEdit", "no SLANG ticket to poll"));
        }
        const quint64 job = jobs_.value(ticket, 0);
        if (job == 0) {
            return LspPolled::failure(QCoreApplication::translate("VEdit", "no SLANG ticket to poll"));
        }
        VdDiagnostics raw{};
        const int status = vd_deeplint_batch_poll(service_.get(), job, &raw);
        if (status == VD_STATUS_PENDING) {
            return LspPolled::pendingResult();
        }
        // Terminal: the ABI hands its result over exactly once, so the job is
        // released here whether it finished, failed or was cancelled.
        jobs_.remove(ticket);
        vd_deeplint_batch_release(service_.get(), job);
        if (status != VD_STATUS_READY) {
            return LspPolled::failure(service_.lastError());
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        for (size_t index = 0; index < raw.count; ++index) {
            const VdDiagnostic &item = raw.items[index];
            DiagnosticEntry entry;
            entry.file = item.file != nullptr ? QString::fromUtf8(item.file) : QString();
            entry.line = static_cast<int>(item.line);
            entry.column = static_cast<int>(item.column);
            entry.endLine = static_cast<int>(item.end_line);
            entry.endColumn = static_cast<int>(item.end_column);
            entry.severity = item.severity;
            entry.code = item.code != nullptr ? QString::fromUtf8(item.code) : QString();
            // AC-5.8 / AC-N3: the typed slang warning option name travels with
            // the row. Without it the panel could only offer a whole-file slang
            // waiver, which AC-5.8 forbids; nothing is derived from `message`.
            entry.optionName = item.option_name != nullptr
                ? QString::fromUtf8(item.option_name)
                : QString();
            entry.source = engineName();
            entry.message = item.message != nullptr ? QString::fromUtf8(item.message) : QString();
            polled.diagnostics.append(entry);
        }
        vd_diagnostics_free(&raw);
        return polled;
    }

private:
    ServiceHandle service_;
    LintEngineKind engine_ = LintEngineKind::Slang;
    QString slangPath_;
    QString veribleLintPath_;
    QStringList includeDirs_;
    QStringList defines_;
    QString rulesPath_;
    /// Local ticket -> the ABI's one-file batch job (ids are 64-bit).
    QHash<int, quint64> jobs_;
    int nextTicket_ = 0;
};

} // namespace

bool ffiLspSessionBackendAvailable()
{
    return true;
}

std::unique_ptr<LspSessionBackend> createFfiLspSessionBackend()
{
    return std::make_unique<FfiLspSessionBackend>();
}

bool ffiSlangBackendAvailable()
{
    return true;
}

std::unique_ptr<SlangBackend> createFfiSlangBackend(const QString &slangPath)
{
    return std::make_unique<FfiSlangBackend>(slangPath);
}

} // namespace vedit::workspace::diagnostics

#else // !VEDIT_DIAGNOSTICS_HAVE_FFI

namespace vedit::workspace::diagnostics {

bool ffiLspSessionBackendAvailable()
{
    return false;
}

std::unique_ptr<LspSessionBackend> createFfiLspSessionBackend()
{
    return nullptr;
}

bool ffiSlangBackendAvailable()
{
    return false;
}

std::unique_ptr<SlangBackend> createFfiSlangBackend(const QString &)
{
    return nullptr;
}

} // namespace vedit::workspace::diagnostics

#endif // VEDIT_DIAGNOSTICS_HAVE_FFI

// --- T11: the testable backend-switch / gating core ---------------------------
//
// Defined outside the FFI guard so a no-FFI build and a harness can link the
// same implementation the product uses; the backends themselves are injected.
namespace vedit::workspace {

namespace {

/// The spellings `crates/vd-settings/src/schema.rs` accepts (case-insensitive,
/// trimmed). The Qt half must agree with it (AC-3.3): a legal spelling is never
/// silently a different backend, and an unknown value is never silently the
/// default.
QString trimmedLower(const QString &value)
{
    return value.trimmed().toLower();
}

QString canonicalLspBackend(const QString &raw, bool *known)
{
    const QString value = trimmedLower(raw);
    if (value == QStringLiteral("verible") || value == QStringLiteral("verible-verilog-ls")) {
        *known = true;
        return QStringLiteral("verible");
    }
    if (value == QStringLiteral("slang") || value == QStringLiteral("slang-server")) {
        *known = true;
        return QStringLiteral("slang");
    }
    *known = false;
    // Keep what was written, so the badge can show `emacs→slang`.
    return value;
}

QString canonicalLintEngine(const QString &raw, bool *known)
{
    const QString value = trimmedLower(raw);
    if (value == QStringLiteral("slang")) {
        *known = true;
        return QStringLiteral("slang");
    }
    if (value == QStringLiteral("verible") || value == QStringLiteral("verible-verilog-lint")) {
        *known = true;
        return QStringLiteral("verible");
    }
    *known = false;
    return value;
}

/// The same sentences the Rust normaliser emits, so the two layers cannot tell
/// the user different things about one value.
QString unknownLspMessage(const QString &raw)
{
    return QStringLiteral("tools.lsp_backend: `%1` is not a known language server backend; "
                          "expected verible | verible-verilog-ls | slang | slang-server; "
                          "using `slang`")
        .arg(raw);
}

QString unknownLintMessage(const QString &raw)
{
    return QStringLiteral("tools.lint_engine: `%1` is not a known lint engine; "
                          "expected slang | verible | verible-verilog-lint; using `slang`")
        .arg(raw);
}

} // namespace

CapabilityDecision decideActionGate(const diagnostics::BackendCapabilities &capabilities,
                                    const QString &action,
                                    bool formatterFound,
                                    bool editorSignatureHelpAvailable)
{
    CapabilityDecision decision;
    decision.action = action;

    // AC-1.4: formatting is a CLI job (verible-verilog-format), never a backend
    // capability: the selected LSP backend does NOT gate it.
    if (action == QStringLiteral("formatting")) {
        decision.source = QStringLiteral("always-verible-format");
        decision.enabled = formatterFound;
        decision.known = true;
        decision.reason = formatterFound
            ? QStringLiteral("formatting always uses verible-verilog-format, independent of "
                             "the LSP backend")
            : QStringLiteral("verible-verilog-format not found (bundled, local checkout, PATH "
                             "and env all empty)");
        return decision;
    }

    // The editor synthesises signatureHelp locally (T16); the capability table
    // says neither backend provides it, and gating on that would hide a feature
    // the editor owns. Absent (not broken) until the synthesiser exists.
    if (action == QStringLiteral("signatureHelp")) {
        decision.source = QStringLiteral("editor-synthesised");
        decision.known = true;
        decision.enabled = editorSignatureHelpAvailable;
        decision.reason = editorSignatureHelpAvailable
            ? QStringLiteral("editor-synthesised (syntax-level, not backend-provided)")
            : QStringLiteral("editor-synthesised by the editor (T16); absent until it lands");
        return decision;
    }

    // item 9 / F1: `hoverProvider:false` is NOT a reliable answer for every
    // server. The measured verible-verilog-ls advertises false and still
    // answers `textDocument/hover` with real content, so the advertised row
    // must not grey the action out. The request path is available whenever a
    // session is, and an empty answer is reported as "no hover here" instead of
    // pretending the feature does not exist.
    if (action == QStringLiteral("hover")) {
        decision.source = QStringLiteral("measured");
        // Always available: the honest answer to "does this server have hover"
        // is its reply, not its advertisement. An empty reply is reported.
        decision.known = true;
        decision.enabled = true;
        const diagnostics::CapabilityRow *hoverRow = capabilities.row(QStringLiteral("hover"));
        decision.reason = hoverRow == nullptr
            ? QStringLiteral("hover is probed with a real request (no capability row)")
            : QStringLiteral("the server advertises hoverProvider=%1, but hover is measured: "
                             "verible-verilog-ls answers with content while advertising false")
                  .arg(hoverRow->supported == 1 ? QStringLiteral("true")
                                                : (hoverRow->supported == 0
                                                       ? QStringLiteral("false")
                                                       : QStringLiteral("unknown")));
        return decision;
    }

    decision.source = QStringLiteral("backend");
    const diagnostics::CapabilityRow *row = capabilities.row(action);
    if (row == nullptr) {
        decision.known = false;
        decision.reason =
            QStringLiteral("no capability row for '%1' in the %2 table")
                .arg(action, capabilities.backend.isEmpty() ? QStringLiteral("selected backend")
                                                            : capabilities.backend);
        return decision;
    }
    if (row->supported == 1) {
        decision.enabled = true;
        return decision;
    }
    if (row->supported == 0) {
        decision.reason = row->reason.isEmpty()
            ? QStringLiteral("%1 does not provide %2").arg(capabilities.backend, action)
            : row->reason;
        return decision;
    }
    // Unknown: neither claim it nor deny it - say how to find out.
    decision.known = false;
    decision.reason = row->reason.isEmpty()
        ? QStringLiteral("%1 is not connected yet - connect to know")
              .arg(capabilities.backend.isEmpty() ? QStringLiteral("backend")
                                                  : capabilities.backend)
        : row->reason;
    return decision;
}

BackendSwitchState::BackendSwitchState(diagnostics::LspSessionBackend *lsp,
                                       diagnostics::SlangBackend *lint)
    : lsp_(lsp)
    , lint_(lint)
{
}

void BackendSwitchState::seedToolchain(const diagnostics::ToolchainReport &toolchain)
{
    toolchain_ = toolchain;
}

QStringList BackendSwitchState::effectiveIncludeDirs() const
{
    // Same precedence as buildLintRequest(): the project expansion APPENDS the
    // tools-page directories, so the project's list is the complete one when it
    // exists; with none discovered yet the settings values stay in force.
    return projectIncludeDirs_.isEmpty() ? settingsIncludeDirs_ : projectIncludeDirs_;
}

QStringList BackendSwitchState::effectiveDefines() const
{
    return projectDefines_.isEmpty() ? settingsDefines_ : projectDefines_;
}

bool BackendSwitchState::setProjectEnvironment(const QStringList &includeDirs,
                                               const QStringList &defines)
{
    projectIncludeDirs_ = includeDirs;
    projectDefines_ = defines;
    if (lsp_ == nullptr) {
        return false;
    }
    // `selectBackend` is the ONE writer of the LSP session's -I/-D. Re-selecting
    // the SAME backend and server with the new lists is what guarantees the
    // next `start()` cannot inherit the previous project's argv: it stops any
    // running session (the caller's reparse starts a fresh one) and replaces
    // the values the session builder reads. The lint engine is re-pinned on the
    // same call so the single-file lint path cannot disagree with the pass.
    const QString serverPath = lspPath_.isEmpty() ? toolchain_.verible.path : lspPath_;
    QString error;
    const bool selected = lsp_->selectBackend(lspKind_, serverPath, effectiveIncludeDirs(),
                                              effectiveDefines(), &error);
    if (lint_ != nullptr) {
        lint_->selectEngine(lintKind_, toolchain_.slang.path, toolchain_.veribleLint.path,
                            effectiveIncludeDirs(), effectiveDefines(), rulesPath_);
    }
    return selected;
}

bool BackendSwitchState::apply(const DiagnosticsBackendSettings &settings,
                               const diagnostics::ToolchainReport &toolchain,
                               QString *error)
{
    toolchain_ = toolchain;
    QHash<QString, QString> overrides;
    overrides.insert(QStringLiteral("verible-verilog-ls"), settings.veribleLs);
    overrides.insert(QStringLiteral("slang"), settings.slang);
    overrides.insert(QStringLiteral("slang-server"), settings.slangServer);
    overrides.insert(QStringLiteral("verible-verilog-format"), settings.veribleFormat);
    overrides.insert(QStringLiteral("verible-verilog-lint"), settings.veribleLint);
    diagnostics::applyToolOverrides(&toolchain_, overrides);

    // AC-3.3: the accepted spellings normalise identically on both sides. A
    // value that is not a backend/engine keeps the documented default and is
    // REPORTED through the badge (reason + message), never silently swallowed.
    bool lspKnown = true;
    bool lintKnown = true;
    requestedLsp_ = canonicalLspBackend(settings.lspBackend, &lspKnown);
    requestedLint_ = canonicalLintEngine(settings.lintEngine, &lintKnown);

    // RM-2: the settings values are the FALLBACK environment, not the whole
    // one. Once a project has delivered its `-I`/`-D` (they already include the
    // tools-page values, appended by the expansion), a settings change must not
    // revert the session to the settings-only lists - let alone to a previous
    // project's. `effectiveIncludeDirs()` keeps the project's values in force
    // and falls back to these when it has none.
    settingsIncludeDirs_ = settings.includeDirs;
    settingsDefines_ = settings.defines;

    // --- LSP axis ---------------------------------------------------------
    const bool wantSlang = !lspKnown || requestedLsp_ == QStringLiteral("slang");
    const diagnostics::LspBackendKind lspKind = wantSlang
        ? diagnostics::LspBackendKind::Slang
        : diagnostics::LspBackendKind::Verible;
    const QString serverPath = wantSlang ? toolchain_.slangServer.path
                                         : toolchain_.verible.path;
    lspReason_ = BadgeReasonCode::Ok;
    lspReasonMessage_.clear();
    bool lspOk = true;
    if (!lspKnown) {
        lspReason_ = BadgeReasonCode::UnknownValue;
        lspReasonMessage_ = unknownLspMessage(settings.lspBackend);
    }
    if (lsp_ == nullptr) {
        lspOk = false;
        lspReason_ = BadgeReasonCode::NoBackend;
        lspReasonMessage_ =
            QCoreApplication::translate("VEdit", "no LSP backend in this build");
    } else if (serverPath.isEmpty()) {
        lspOk = false;
        lspReason_ = BadgeReasonCode::ToolMissing;
        lspReasonMessage_ = wantSlang
            ? toolchain_.missingMessage(diagnostics::ToolKind::SlangServer)
            : toolchain_.missingMessage(diagnostics::ToolKind::Verible);
    } else {
        QString selectError;
        if (lsp_->selectBackend(lspKind, serverPath, effectiveIncludeDirs(),
                                effectiveDefines(), &selectError)) {
            lspKind_ = lspKind;
            lspPath_ = serverPath;
        } else {
            lspOk = false;
            lspReason_ = BadgeReasonCode::SelectFailed;
            lspReasonMessage_ = selectError.isEmpty()
                ? QCoreApplication::translate("VEdit", "%1 refused the selection")
                      .arg(lspBackendName())
                : selectError;
        }
    }

    // Keep a working selection; on a fresh launch try the other installed server.
    if (!lspOk && lsp_ != nullptr && lspPath_.isEmpty()) {
        const auto fallback = wantSlang ? diagnostics::LspBackendKind::Verible
                                        : diagnostics::LspBackendKind::Slang;
        const QString fallbackPath = wantSlang ? toolchain_.verible.path
                                               : toolchain_.slangServer.path;
        if (!fallbackPath.isEmpty() && lsp_->selectBackend(
                fallback, fallbackPath, effectiveIncludeDirs(), effectiveDefines(), nullptr)) {
            lspKind_ = fallback;
            lspPath_ = fallbackPath;
        }
    }

    // --- Lint axis (INDEPENDENT of the LSP axis: AC-T9.2) -----------------
    // "verible" selects the verible-verilog-lint engine; slang is the default.
    const bool wantVeribleLint = requestedLint_ == QStringLiteral("verible");
    const diagnostics::LintEngineKind lintKind = wantVeribleLint
        ? diagnostics::LintEngineKind::VeribleLint
        : diagnostics::LintEngineKind::Slang;
    const QString lintPath = wantVeribleLint ? toolchain_.veribleLint.path
                                             : toolchain_.slang.path;
    lintReason_ = BadgeReasonCode::Ok;
    lintReasonMessage_.clear();
    bool lintOk = true;
    if (!lintKnown) {
        lintReason_ = BadgeReasonCode::UnknownValue;
        lintReasonMessage_ = unknownLintMessage(settings.lintEngine);
    }
    if (lintPath.isEmpty()) {
        lintOk = false;
        lintReason_ = BadgeReasonCode::ToolMissing;
        lintReasonMessage_ = wantVeribleLint
            ? toolchain_.missingMessage(diagnostics::ToolKind::VeribleLint)
            : toolchain_.missingMessage(diagnostics::ToolKind::Slang);
    } else {
        if (lint_ != nullptr) {
            lint_->selectEngine(lintKind, toolchain_.slang.path, toolchain_.veribleLint.path,
                                effectiveIncludeDirs(), effectiveDefines(), settings.veribleRules);
        }
        lintKind_ = lintKind;
        lintPath_ = lintPath;
        rulesPath_ = settings.veribleRules;
    }

    if (!lspOk || !lintOk) {
        if (error != nullptr) {
            QStringList failures;
            if (!lspOk) {
                failures.append(lspReasonMessage_);
            }
            if (!lintOk) {
                failures.append(lintReasonMessage_);
            }
            *error = failures.join(QStringLiteral("; "));
        }
        return false;
    }
    return true;
}

QString BackendSwitchState::lspBackendName() const
{
    return lspKind_ == diagnostics::LspBackendKind::Slang ? QStringLiteral("slang")
                                                          : QStringLiteral("verible");
}

QString BackendSwitchState::lintEngineName() const
{
    return lintKind_ == diagnostics::LintEngineKind::VeribleLint
        ? QStringLiteral("verible-verilog-lint")
        : QStringLiteral("slang");
}

QString badgeReasonCodeName(BadgeReasonCode code)
{
    switch (code) {
    case BadgeReasonCode::Ok:
        return QStringLiteral("ok");
    case BadgeReasonCode::UnknownValue:
        return QStringLiteral("unknown-value");
    case BadgeReasonCode::ToolMissing:
        return QStringLiteral("tool-missing");
    case BadgeReasonCode::SelectFailed:
        return QStringLiteral("select-failed");
    case BadgeReasonCode::NoBackend:
        return QStringLiteral("no-backend");
    }
    return QStringLiteral("unknown");
}

QString BadgeState::summary() const
{
    const auto axisText = [](const QString &name, const BadgeAxis &axis) {
        const QString effective = axis.effective.isEmpty()
            ? QCoreApplication::translate("VEdit", "unavailable")
            : axis.effective;
        if (axis.applied) {
            return QStringLiteral("%1=%2").arg(name, effective);
        }
        const QString why = axis.reasonMessage.isEmpty()
            ? badgeReasonCodeName(axis.reason)
            : axis.reasonMessage;
        return QStringLiteral("%1=%2→%3 · %4").arg(name, axis.requested, effective, why);
    };
    return QStringLiteral("%1 %2 · basis=%3")
        .arg(axisText(QStringLiteral("lsp"), lsp),
             axisText(QStringLiteral("lint"), lint), basis);
}

void BackendSwitchState::setLoadWarnings(const QStringList &warnings)
{
    loadWarnings_ = warnings;
}

BadgeState BackendSwitchState::badge(const QString &languageId) const
{
    BadgeState out;
    out.basis = capabilities().basis;
    if (out.basis.isEmpty()) {
        out.basis = QStringLiteral("unknown");
    }
    out.language = languageId;
    // AC-T9.1: the store's own explanation travels with the badge facts.
    out.warnings = loadWarnings_;

    // AC-3.5: a VHDL file is NEVER answered by the Verilog backend. The route
    // comes from the same toolchain report the rest of the lane uses, so a
    // missing vhdl_ls is reported as missing instead of naming a backend that
    // cannot run.
    if (languageId.compare(QStringLiteral("vhdl"), Qt::CaseInsensitive) == 0) {
        const bool lsFound = toolchain_.vhdlLs.found();
        out.lsp.requested = QStringLiteral("vhdl_ls");
        out.lsp.effective = lsFound ? QStringLiteral("vhdl_ls") : QString();
        out.lsp.path = toolchain_.vhdlLs.path;
        out.lsp.applied = lsFound;
        out.lsp.reason = lsFound ? BadgeReasonCode::Ok : BadgeReasonCode::ToolMissing;
        out.lsp.reasonMessage =
            lsFound ? QString() : toolchain_.missingMessage(diagnostics::ToolKind::VhdlLs);

        const bool ghdlFound = toolchain_.ghdl.found();
        const bool vsgFound = toolchain_.vsg.found();
        const bool lintFound = ghdlFound && vsgFound;
        out.lint.requested = QStringLiteral("ghdl+vsg");
        out.lint.effective = lintFound ? QStringLiteral("ghdl+vsg") : QString();
        out.lint.path = QStringList{toolchain_.ghdl.path, toolchain_.vsg.path}.join(
            QStringLiteral(" "));
        out.lint.applied = lintFound;
        out.lint.reason = lintFound ? BadgeReasonCode::Ok : BadgeReasonCode::ToolMissing;
        if (!lintFound) {
            out.lint.reasonMessage = toolchain_.missingMessage(
                ghdlFound ? diagnostics::ToolKind::Vsg : diagnostics::ToolKind::Ghdl);
        }
        out.languageBackend = lsFound ? QStringLiteral("vhdl_ls") : QString();
        return out;
    }

    out.lsp.requested = requestedLsp_;
    out.lsp.effective = lspPath_.isEmpty() ? QString() : lspBackendName();
    out.lsp.path = lspPath_;
    out.lsp.applied = lspReason_ == BadgeReasonCode::Ok
        && out.lsp.requested.compare(out.lsp.effective, Qt::CaseInsensitive) == 0;
    if (out.lsp.applied) {
        out.lsp.reason = BadgeReasonCode::Ok;
    } else {
        out.lsp.reason = lspReason_;
        out.lsp.reasonMessage = lspReasonMessage_;
    }

    out.lint.requested = requestedLint_;
    out.lint.effective = lintEngineName();
    out.lint.path = lintPath_;
    // The request names the ENGINE (`verible`), the effective name is the
    // BINARY (`verible-verilog-lint`): compare the selected kind, not the
    // strings, so a legitimately applied engine is never reported as a mismatch.
    out.lint.applied = lintReason_ == BadgeReasonCode::Ok
        && (requestedLint_ == QStringLiteral("verible"))
            == (lintKind_ == diagnostics::LintEngineKind::VeribleLint);
    if (out.lint.applied) {
        out.lint.reason = BadgeReasonCode::Ok;
    } else {
        out.lint.reason = lintReason_;
        out.lint.reasonMessage = lintReasonMessage_;
    }

    // "Never name a backend that cannot run": with no resolved binary the
    // language route stays empty until an apply() succeeds.
    out.languageBackend = lspPath_.isEmpty() ? QString() : lspBackendName();
    return out;
}

QString BackendSwitchState::statusLine() const
{
    return QStringLiteral("backend=%1 lint=%2 lsp=%3 engine=%4")
        .arg(lspBackendName(), lintEngineName(),
             lspPath_.isEmpty() ? QStringLiteral("<unresolved>") : lspPath_,
             lintPath_.isEmpty() ? QStringLiteral("<unresolved>") : lintPath_);
}

QString BackendSwitchState::commandLine() const
{
    return lsp_ != nullptr ? lsp_->commandLine() : QString();
}

diagnostics::BackendCapabilities BackendSwitchState::capabilities() const
{
    return lsp_ != nullptr ? lsp_->sessionCapabilities() : diagnostics::BackendCapabilities{};
}

CapabilityDecision BackendSwitchState::gate(const QString &action) const
{
    CapabilityDecision decision =
        decideActionGate(capabilities(), action, toolchain_.veribleFormat.found(), false);
    if (action == QStringLiteral("formatting") && !toolchain_.veribleFormat.found()) {
        // T29/AC-8.1: name the tool AND the candidates this process actually
        // probed, so the reason cannot claim "env all empty" while an override
        // is set, nor deny a formatter the toolchain reports as present.
        decision.reason = toolchain_.veribleFormat.describe();
    }
    return decision;
}

} // namespace vedit::workspace
