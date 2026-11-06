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

DiagnosticEntry mapDiagnostic(const VdLspDiagnostic &raw, const QString &file)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = static_cast<int>(raw.range.start_line);
    entry.column = static_cast<int>(raw.range.start_character);
    entry.endLine = static_cast<int>(raw.range.end_line);
    entry.endColumn = static_cast<int>(raw.range.end_character);
    entry.severity = raw.severity;
    entry.code = raw.code != nullptr ? QString::fromUtf8(raw.code) : QString();
    entry.source = raw.source != nullptr ? QString::fromUtf8(raw.source) : QString();
    entry.message = raw.message != nullptr ? QString::fromUtf8(raw.message) : QString();
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
            for (size_t index = 0; index < raw.diagnostics.count; ++index) {
                polled.diagnostics.append(mapDiagnostic(raw.diagnostics.items[index], QString()));
            }
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
        const QByteArray path = file.toUtf8();
        const QByteArray slang = slangPath_.toUtf8();
        const QByteArray verible = veribleLintPath_.toUtf8();
        // UX batch 2 / A4 + U6 / §0.1 R1: the rules file comes from the SAME
        // composition function the Preferences page and the project pass use —
        // the dedicated `lint.verible.rules_config` first, then the legacy
        // `tools.verible_rules` path, then the project's DISCOVERED
        // `.rules.verible_lint` (`VdProjectInfo.rules_path`, published by the
        // integration when the project file set arrives).
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS)
        const vedit::workspace::settings::LintJobOptions jobOptions =
            vedit::workspace::settings::buildLintJobOptions(
                vedit::workspace::settings::currentLintValues(),
                engine_ == LintEngineKind::VeribleLint
                    ? vedit::workspace::settings::LintBackend::Verible
                    : vedit::workspace::settings::LintBackend::Slang,
                rulesPath_, vedit::workspace::settings::currentDiscoveredRulesPath());
        const QByteArray rules = jobOptions.rulesPath.toUtf8();
#else
        const QByteArray rules = rulesPath_.toUtf8();
#endif
        FileList includeList(includeDirs_);
        FileList defineList(defines_);
        const VdStringList includeView = includeList.view();
        const VdStringList defineView = defineList.view();
        const quint64 job = vd_deeplint_lint_v2(service_.get(),
                                                path.constData(),
                                                static_cast<int32_t>(engine_),
                                                slang.isEmpty() ? nullptr : slang.constData(),
                                                verible.isEmpty() ? nullptr : verible.constData(),
#if defined(VEDIT_DIAGNOSTICS_HAVE_LINT_OPTIONS)
                                                jobOptions.rulesPath.isEmpty()
                                                    ? nullptr
                                                    : rules.constData(),
#else
                                                rulesPath_.isEmpty() ? nullptr : rules.constData(),
#endif
                                                includeDirs_.isEmpty() ? nullptr : &includeView,
                                                defines_.isEmpty() ? nullptr : &defineView);
        if (job == 0) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return 0;
        }
        if (job > static_cast<quint64>(INT32_MAX)) {
            if (error != nullptr) {
                *error = QCoreApplication::translate("VEdit", "the SLANG job id does not fit the backend ticket type");
            }
            return 0;
        }
        return static_cast<int>(job);
    }

    LspPolled pollLint(int ticket) override
    {
        if (service_.get() == nullptr || ticket <= 0) {
            return LspPolled::failure(QCoreApplication::translate("VEdit", "no SLANG ticket to poll"));
        }
        VdDiagnostics raw{};
        const int status =
            vd_deeplint_poll(service_.get(), static_cast<quint64>(ticket), &raw);
        if (status == VD_STATUS_PENDING) {
            return LspPolled::pendingResult();
        }
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
            entry.line = static_cast<int>(item.line);
            entry.column = static_cast<int>(item.column);
            entry.endLine = static_cast<int>(item.end_line);
            entry.endColumn = static_cast<int>(item.end_column);
            entry.severity = item.severity;
            entry.code = item.code != nullptr ? QString::fromUtf8(item.code) : QString();
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
    if (lsp_ == nullptr) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("VEdit", "no LSP backend in this build");
        }
        return false;
    }
    toolchain_ = toolchain;
    QHash<QString, QString> overrides;
    overrides.insert(QStringLiteral("verible-verilog-ls"), settings.veribleLs);
    overrides.insert(QStringLiteral("slang"), settings.slang);
    overrides.insert(QStringLiteral("slang-server"), settings.slangServer);
    overrides.insert(QStringLiteral("verible-verilog-format"), settings.veribleFormat);
    overrides.insert(QStringLiteral("verible-verilog-lint"), settings.veribleLint);
    diagnostics::applyToolOverrides(&toolchain_, overrides);

    const bool wantSlang = settings.lspBackend.compare(QStringLiteral("slang"),
                                                       Qt::CaseInsensitive) == 0;
    const diagnostics::LspBackendKind lspKind = wantSlang
        ? diagnostics::LspBackendKind::Slang
        : diagnostics::LspBackendKind::Verible;
    const QString serverPath = wantSlang ? toolchain_.slangServer.path
                                         : toolchain_.verible.path;
    if (serverPath.isEmpty()) {
        if (error != nullptr) {
            *error = wantSlang
                ? toolchain_.missingMessage(diagnostics::ToolKind::SlangServer)
                : toolchain_.missingMessage(diagnostics::ToolKind::Verible);
        }
        return false;
    }

    // "verible" selects the verible-verilog-lint engine; anything else is slang.
    const bool wantVeribleLint = settings.lintEngine.compare(QStringLiteral("verible"),
                                                             Qt::CaseInsensitive) == 0;
    const diagnostics::LintEngineKind lintKind = wantVeribleLint
        ? diagnostics::LintEngineKind::VeribleLint
        : diagnostics::LintEngineKind::Slang;
    const QString lintPath = wantVeribleLint ? toolchain_.veribleLint.path
                                             : toolchain_.slang.path;
    if (lintPath.isEmpty()) {
        if (error != nullptr) {
            *error = wantVeribleLint
                ? toolchain_.missingMessage(diagnostics::ToolKind::VeribleLint)
                : toolchain_.missingMessage(diagnostics::ToolKind::Slang);
        }
        return false;
    }

    // RM-2: the settings values are the FALLBACK environment, not the whole
    // one. Once a project has delivered its `-I`/`-D` (they already include the
    // tools-page values, appended by the expansion), a settings change must not
    // revert the session to the settings-only lists - let alone to a previous
    // project's. `effectiveIncludeDirs()` keeps the project's values in force
    // and falls back to these when it has none.
    settingsIncludeDirs_ = settings.includeDirs;
    settingsDefines_ = settings.defines;
    QString selectError;
    if (!lsp_->selectBackend(lspKind, serverPath, effectiveIncludeDirs(), effectiveDefines(),
                             &selectError)) {
        if (error != nullptr) {
            *error = selectError;
        }
        return false;
    }
    if (lint_ != nullptr) {
        lint_->selectEngine(lintKind, toolchain_.slang.path, toolchain_.veribleLint.path,
                            effectiveIncludeDirs(), effectiveDefines(), settings.veribleRules);
    }
    lspKind_ = lspKind;
    lintKind_ = lintKind;
    lspPath_ = serverPath;
    lintPath_ = lintPath;
    rulesPath_ = settings.veribleRules;
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
