// C ABI implementation of the diagnostics seams (Verible LSP + SLANG).
//
// Only this file touches the generated `vd_ffi.h`; the controller and the
// self-tests stay Qt-only. Every string crossing the ABI is copied by the Rust
// side (the caller keeps ownership of the `VdStringList` it builds), and every
// Rust-owned result is released with its `vd_*_free` function.

#include "lsp_backend.h"
#include "slang_backend.h"

#include <QByteArray>
#include <QFileInfo>
#include <QStringList>
#include <cstdint>
#include <QVector>

#if defined(VEDIT_DIAGNOSTICS_HAVE_FFI)

#include <vd_ffi.h>

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
                *error = QStringLiteral("the Rust service handle could not be created");
            }
            return false;
        }
        FileList fileList(files);
        const QByteArray ls = lsPath.toUtf8();
        const QByteArray root = workspaceRoot.toUtf8();
        const QByteArray rules = rulesPath.toUtf8();
        const VdStringList view = fileList.view();
        session_ = vd_lsp_session_start(service_.get(),
                                        ls.constData(),
                                        root.constData(),
                                        rulesPath.isEmpty() ? nullptr : rules.constData(),
                                        &view);
        if (session_ == 0) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return false;
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
            snapshot.message = QStringLiteral("no LSP session");
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
        snapshot.message = raw.message != nullptr ? QString::fromUtf8(raw.message) : QString();
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

    LspPolled poll(quint64 requestId) override
    {
        if (session_ == 0 || requestId == 0) {
            return LspPolled::failure(QStringLiteral("no LSP request to poll"));
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
        default:
            break;
        }
        vd_lsp_response_free(&raw);
        return polled;
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
        return slangPath_.isEmpty() ? QStringLiteral("slang (FFI)")
                                    : QStringLiteral("slang (FFI): %1").arg(slangPath_);
    }

    int startLint(const QString &file, QString *error) override
    {
        if (service_.get() == nullptr) {
            if (error != nullptr) {
                *error = QStringLiteral("the Rust service handle could not be created");
            }
            return 0;
        }
        const QByteArray path = file.toUtf8();
        const QByteArray slang = slangPath_.toUtf8();
        const quint64 job =
            vd_deeplint_lint(service_.get(), path.constData(), slang.isEmpty() ? nullptr
                                                                              : slang.constData());
        if (job == 0) {
            if (error != nullptr) {
                *error = service_.lastError();
            }
            return 0;
        }
        if (job > static_cast<quint64>(INT32_MAX)) {
            if (error != nullptr) {
                *error = QStringLiteral("the SLANG job id does not fit the backend ticket type");
            }
            return 0;
        }
        return static_cast<int>(job);
    }

    LspPolled pollLint(int ticket) override
    {
        if (service_.get() == nullptr || ticket <= 0) {
            return LspPolled::failure(QStringLiteral("no SLANG ticket to poll"));
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
            entry.source = QStringLiteral("slang");
            entry.message = item.message != nullptr ? QString::fromUtf8(item.message) : QString();
            polled.diagnostics.append(entry);
        }
        vd_diagnostics_free(&raw);
        return polled;
    }

private:
    ServiceHandle service_;
    QString slangPath_;
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
