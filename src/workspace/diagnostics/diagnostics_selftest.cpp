// Headless self-test of the diagnostics lane (no display, no Verible needed).
//
// It injects fakes through the two backend seams and asserts the acceptance
// properties that CAN be proven without a screen:
//   * A3c — the LSP session and the SLANG backend receive the same effective
//     file set, and the report says so;
//   * A3d — changing the file set advances the generation and the observable
//     state goes Reparsing -> Ready with the new counts;
//   * the five navigation entries (definition, references, symbols, rename,
//     format) answer with typed results and the outline is updated;
//   * a missing/broken backend degrades to a diagnosable state, never a crash.
//
// The real-server proof (verible-verilog-ls through the C ABI) lives in
// diagnostics_ffi_selftest.cpp; it SKIPs explicitly when the binary is absent.

#include "diagnostics_controller.h"
#include "diagnostics_outline_model.h"
#include "toolchain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n",
                ok ? "PASS" : "FAIL",
                name,
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
}

class FakeLsp final : public LspSessionBackend {
public:
    QStringList startedFiles;
    int startCount = 0;
    int statusCallsBeforeReady = 0; // 0 => ready on the first status()
    bool failStart = false;
    mutable int statusCalls = 0;
    SessionSnapshot snapshot;

    struct Recorded {
        quint64 id = 0;
        ResponseKind kind = ResponseKind::None;
        QString file;
        int line = 0;
        int character = 0;
        QString newName;
    };
    QVector<Recorded> requests;

    bool start(const QString &,
               const QString &,
               const QString &,
               const QStringList &files,
               QString *error) override
    {
        ++startCount;
        startedFiles = files;
        statusCalls = 0;
        snapshot = SessionSnapshot{};
        snapshot.filesExpected = files.size();
        snapshot.message = QStringLiteral("fake session starting");
        if (failStart) {
            if (error != nullptr) {
                *error = QStringLiteral("fake LSP refused to start (missing binary)");
            }
            return false;
        }
        return true;
    }

    void stop() override { snapshot.state = BackendState::Idle; }

    SessionSnapshot status() const override
    {
        SessionSnapshot copy = snapshot;
        if (!failStart) {
            ++statusCalls;
            if (statusCalls > statusCallsBeforeReady) {
                copy.state = BackendState::Ready;
                copy.filesOpened = copy.filesExpected;
                copy.message = QStringLiteral("fake session ready");
            } else {
                copy.state = BackendState::Reparsing;
                copy.message = QStringLiteral("fake session starting");
            }
        }
        return copy;
    }

    quint64 request(ResponseKind kind, const QString &file, int line, int character,
                    const QString &newName)
    {
        if (failStart) {
            return 0;
        }
        Recorded recorded;
        recorded.id = requests.size() + 1;
        recorded.kind = kind;
        recorded.file = file;
        recorded.line = line;
        recorded.character = character;
        recorded.newName = newName;
        requests.append(recorded);
        return recorded.id;
    }

    quint64 requestDefinition(const QString &file, int line, int character) override
    {
        return request(ResponseKind::Locations, file, line, character, QString());
    }
    quint64 requestReferences(const QString &file, int line, int character) override
    {
        return request(ResponseKind::Locations, file, line, character, QString());
    }
    quint64 requestSymbols(const QString &file) override
    {
        return request(ResponseKind::Symbols, file, 0, 0, QString());
    }
    quint64 requestRename(const QString &file, int line, int character,
                          const QString &newName) override
    {
        return request(ResponseKind::Edits, file, line, character, newName);
    }
    quint64 requestFormat(const QString &file) override
    {
        return request(ResponseKind::Edits, file, 0, 0, QString());
    }
    quint64 requestDiagnostics(const QString &file) override
    {
        return request(ResponseKind::Diagnostics, file, 0, 0, QString());
    }

    LspPolled poll(quint64 requestId) override
    {
        if (requestId == 0 || requestId > static_cast<quint64>(requests.size())) {
            return LspPolled::failure(QStringLiteral("unknown fake request id"));
        }
        const Recorded &recorded = requests.at(static_cast<int>(requestId) - 1);
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = recorded.kind;
        switch (recorded.kind) {
        case ResponseKind::Locations: {
            LspLocation location;
            location.uri = QStringLiteral("file:///fixture/rtl/alu.sv");
            location.startLine = 3;
            polled.locations.append(location);
            break;
        }
        case ResponseKind::Symbols: {
            OutlineSymbol top;
            top.name = QStringLiteral("top");
            top.detail = QStringLiteral("module");
            top.kind = 2; // SymbolKind.Module
            top.line = 4;
            polled.symbols.append(top);
            OutlineSymbol alu;
            alu.name = QStringLiteral("u_alu");
            alu.detail = QStringLiteral("alu");
            alu.kind = 13; // SymbolKind.Variable
            alu.line = 20;
            polled.symbols.append(alu);
            break;
        }
        case ResponseKind::Edits: {
            LspTextEdit edit;
            edit.uri = QStringLiteral("file:///fixture/rtl/top.sv");
            edit.newText = QStringLiteral("alu_renamed");
            polled.edits.append(edit);
            break;
        }
        case ResponseKind::Diagnostics: {
            DiagnosticEntry entry;
            entry.severity = 2;
            entry.code = QStringLiteral("line-length");
            entry.message = QStringLiteral("fake LSP diagnostic");
            polled.diagnostics.append(entry);
            break;
        }
        case ResponseKind::None:
            break;
        }
        return polled;
    }
};

class FakeSlang final : public SlangBackend {
public:
    bool availableFlag = true;
    bool failLint = false;
    int pollsBeforeReady = 0;
    QStringList linted;
    mutable int pollCount = 0;

    bool available() const override { return availableFlag; }
    QString description() const override { return QStringLiteral("fake slang"); }

    int startLint(const QString &file, QString *error) override
    {
        if (!availableFlag) {
            if (error != nullptr) {
                *error = QStringLiteral("fake slang is unavailable");
            }
            return 0;
        }
        if (failLint) {
            if (error != nullptr) {
                *error = QStringLiteral("fake slang binary is missing");
            }
            return 0;
        }
        linted.append(file);
        pollCount = 0;
        return linted.size();
    }

    LspPolled pollLint(int ticket) override
    {
        if (ticket <= 0 || ticket > linted.size()) {
            return LspPolled::failure(QStringLiteral("unknown fake slang ticket"));
        }
        if (pollsBeforeReady > 0 && pollCount < pollsBeforeReady) {
            ++pollCount;
            return LspPolled::pendingResult();
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        DiagnosticEntry entry;
        entry.line = 2;
        entry.severity = 1;
        entry.code = QStringLiteral("UndeclaredIdentifier");
        entry.message = QStringLiteral("fake slang diagnostic");
        polled.diagnostics.append(entry);
        return polled;
    }
};

QString makeExecutable(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write("#!/bin/sh\nexit 0\n");
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

/// Refresh until the controller reaches a terminal state or the budget ends.
BackendState refreshUntilTerminal(DiagnosticsController &controller, int maxIterations = 200)
{
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        controller.refresh();
        const BackendState state = controller.state();
        if (state != BackendState::Reparsing) {
            return state;
        }
    }
    return controller.state();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- 1. A3c: one effective file set reaches both backends --------------
    auto lsp = std::make_unique<FakeLsp>();
    auto slang = std::make_unique<FakeSlang>();
    auto *lspRaw = lsp.get();
    auto *slangRaw = slang.get();
    {
        DiagnosticsController controller(std::move(lsp), std::move(slang));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        const QStringList files{
            QStringLiteral("rtl/alu_pkg.sv"),
            QStringLiteral("rtl/alu.sv"),
            QStringLiteral("rtl/top.sv"),
        };
        controller.setFileSet(files);
        controller.refresh();
        check("a3c.lsp-received-same-set", lspRaw->startedFiles == files,
              lspRaw->startedFiles.join(QLatin1Char(',')));
        check("a3c.slang-received-same-set", slangRaw->linted == files,
              slangRaw->linted.join(QLatin1Char(',')));
        const FileSetReport &report = controller.fileSetReport();
        check("a3c.report-marks-same-set",
              report.lspReceivedWholeSet && report.slangRequested == files.size()
                  && report.slangCompleted == files.size() && report.slangFailures == 0,
              QStringLiteral("lsp=%1 slang=%2/%3")
                  .arg(report.lspFilesOpened)
                  .arg(report.slangCompleted)
                  .arg(report.slangRequested));

        // ---- 2. A3d: observable state after setFileSet ---------------------
        check("a3d.state-ready", controller.state() == BackendState::Ready,
              QString::number(static_cast<int>(controller.state())));
        const quint64 generationBefore = controller.generation();
        const QStringList reduced{
            QStringLiteral("rtl/top.sv"),
            QStringLiteral("rtl/mem.sv"),
        };
        controller.setFileSet(reduced);
        check("a3d.generation-advanced", controller.generation() > generationBefore,
              QString::number(controller.generation()));
        check("a3d.new-counts-follow-set",
              controller.fileSetReport().slangRequested == reduced.size()
                  && lspRaw->startedFiles == reduced,
              QString::number(controller.fileSetReport().slangRequested));
        check("a3d.state-ready-after-change",
              refreshUntilTerminal(controller) == BackendState::Ready);

        // ---- 3. six entry points answer with typed results -----------------
        check("entry.definition",
              controller.requestDefinition(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        const LspPolled definition = controller.pollNavigation();
        check("entry.definition-result",
              definition.ready && definition.kind == ResponseKind::Locations
                  && !definition.locations.isEmpty());
        check("entry.references",
              controller.requestReferences(QStringLiteral("rtl/top.sv"), 10, 3) != 0);
        check("entry.references-result",
              controller.pollNavigation().kind == ResponseKind::Locations);
        check("entry.symbols", controller.requestSymbols(QStringLiteral("rtl/top.sv")) != 0);
        const LspPolled symbols = controller.pollNavigation();
        check("entry.symbols-result",
              symbols.ready && symbols.kind == ResponseKind::Symbols && !symbols.symbols.isEmpty());
        check("entry.symbols-updates-outline",
              controller.outline().size() == symbols.symbols.size()
                  && controller.outline().front().name == QStringLiteral("top"));
        check("entry.rename",
              controller.requestRename(QStringLiteral("rtl/top.sv"), 10, 3,
                                       QStringLiteral("alu_renamed")) != 0);
        const LspPolled rename = controller.pollNavigation();
        check("entry.rename-result",
              rename.ready && rename.kind == ResponseKind::Edits && !rename.edits.isEmpty());
        check("entry.format", controller.requestFormat(QStringLiteral("rtl/top.sv")) != 0);
        check("entry.format-result", controller.pollNavigation().kind == ResponseKind::Edits);
        check("diagnostics.collected", !controller.diagnostics().isEmpty(),
              QString::number(controller.diagnostics().size()));
    }

    // ---- 4. SLANG unavailable: LSP keeps working, state is degraded --------
    {
        auto lsp2 = std::make_unique<FakeLsp>();
        auto slang2 = std::make_unique<FakeSlang>();
        slang2->availableFlag = false;
        DiagnosticsController controller(std::move(lsp2), std::move(slang2));
        controller.setConfiguration(QStringLiteral("/payload/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        refreshUntilTerminal(controller);
        check("degrade.slang-missing-is-observable",
              controller.state() == BackendState::Degraded && !controller.lastError().isEmpty(),
              controller.lastError());
        check("degrade.slang-missing-not-crash",
              controller.fileSetReport().slangAvailable == false);
    }

    // ---- 5. Verible missing: diagnosable failure, no crash -----------------
    {
        auto lsp3 = std::make_unique<FakeLsp>();
        lsp3->failStart = true;
        auto slang3 = std::make_unique<FakeSlang>();
        slang3->availableFlag = false;
        DiagnosticsController controller(std::move(lsp3), std::move(slang3));
        controller.setConfiguration(QStringLiteral("/missing/verible-verilog-ls"),
                                    QStringLiteral("/fixture"));
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        refreshUntilTerminal(controller);
        check("degrade.lsp-missing-is-diagnosable",
              controller.state() == BackendState::Failed && !controller.lastError().isEmpty(),
              controller.lastError());
        check("degrade.lsp-missing-request-fails-typed",
              controller.requestDefinition(QStringLiteral("rtl/top.sv"), 0, 0) == 0);
    }

    // ---- 6. No backends at all: failed state with a message ----------------
    {
        DiagnosticsController controller(nullptr, nullptr);
        controller.setFileSet(QStringList{QStringLiteral("rtl/top.sv")});
        refreshUntilTerminal(controller);
        check("degrade.no-backend-is-diagnosable",
              controller.state() == BackendState::Failed && !controller.lastError().isEmpty());
    }

    // ---- 7. Toolchain observability: bundled > PATH > env (T82) ------------
    {
        QTemporaryDir tools;
        const QString bundled = tools.path() + QStringLiteral("/bundled");
        const QString pathDir = tools.path() + QStringLiteral("/path");
        QDir().mkpath(bundled);
        QDir().mkpath(pathDir);
        const QString bundledLs = makeExecutable(bundled + QStringLiteral("/verible-verilog-ls"));
        const QString bundledSlang = makeExecutable(bundled + QStringLiteral("/slang"));
        const QString pathLs = makeExecutable(pathDir + QStringLiteral("/verible-verilog-ls"));
        const QString pathSlang = makeExecutable(pathDir + QStringLiteral("/slang"));

        ToolchainSearchContext context;
        context.bundledDirs = QStringList() << bundled;
        context.pathDirs = QStringList() << pathDir;
        context.env.insert(QStringLiteral("VEDIT_VERIBLE_LS"),
                           pathLs + QStringLiteral(".env"));
        context.env.insert(QStringLiteral("VEDIT_SLANG"), pathSlang + QStringLiteral(".env"));

        const ToolchainReport bundledReport = resolveToolchain(context);
        check("toolchain.bundled-wins",
              bundledReport.verible.path == bundledLs
                  && bundledReport.verible.sourceLabel() == QStringLiteral("bundled")
                  && bundledReport.slang.path == bundledSlang
                  && bundledReport.slang.sourceLabel() == QStringLiteral("bundled"),
              bundledReport.statusLine());

        ToolchainSearchContext pathContext = context;
        pathContext.bundledDirs = QStringList() << (tools.path() + QStringLiteral("/absent"));
        const ToolchainReport pathReport = resolveToolchain(pathContext);
        check("toolchain.path-before-env",
              pathReport.verible.path == pathLs
                  && pathReport.verible.sourceLabel() == QStringLiteral("PATH")
                  && pathReport.slang.path == pathSlang
                  && pathReport.slang.sourceLabel() == QStringLiteral("PATH"),
              pathReport.statusLine());

        ToolchainSearchContext envContext = pathContext;
        envContext.pathDirs = QStringList() << (tools.path() + QStringLiteral("/absent-path"));
        envContext.env.insert(QStringLiteral("VEDIT_VERIBLE_LS"), bundledLs);
        envContext.env.insert(QStringLiteral("VEDIT_SLANG"), bundledSlang);
        const ToolchainReport envReport = resolveToolchain(envContext);
        check("toolchain.env-is-last",
              envReport.verible.path == bundledLs
                  && envReport.verible.sourceLabel() == QStringLiteral("env:VEDIT_VERIBLE_LS")
                  && envReport.slang.sourceLabel() == QStringLiteral("env:VEDIT_SLANG"),
              envReport.statusLine());

        ToolchainSearchContext empty;
        empty.bundledDirs = QStringList() << (tools.path() + QStringLiteral("/absent"));
        const ToolchainReport missingReport = resolveToolchain(empty);
        check("toolchain.missing-list",
              missingReport.missing().contains(QStringLiteral("verible"))
                  && missingReport.missing().contains(QStringLiteral("slang"))
                  && missingReport.statusLine().contains(
                         QStringLiteral("missing=[verible, slang]")),
              missingReport.statusLine());
        check("toolchain.missing-message-visible",
              missingReport.userMessage().contains(QStringLiteral("Diagnostics unavailable"))
                  && missingReport.userMessage().contains(QStringLiteral("Fix:")),
              missingReport.userMessage());
    }

    std::printf("VE-DIAGNOSTICS-SELFTEST checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
