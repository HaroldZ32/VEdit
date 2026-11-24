// batch-20261118 AC-1 — the project build context reaches slang-server.
//
// This is the PRODUCT's own path: the real `FfiLspSessionBackend` (the class the
// diagnostics controller talks to) drives the real slang-server 0.3.1 over a
// synthetic project whose `inc/defs.svh` lives OUTSIDE the analysed file's
// directory:
//
//   design/rtl/top.sv  --  `include "defs.svh" ... logic [`WIDTH-1:0] data;
//   inc/defs.svh       --  `define WIDTH 8
//
// Before the context push the include cannot be resolved (slang-server ignores
// argv `-I`/`-D`); after `pushBuildContext()` the include resolves, the outline
// carries the module and deleting ONE semicolon produces a real `expected ';'`
// diagnostic. The test SKIPs (never fails) when no slang-server is resolvable,
// the same rule the other real-server tests apply.
//
//   build:  vedit-slang-build-context-e2e
//   env:    VEDIT_SLANG_SERVER=<path> (else the repo's AppDir wrapper is probed)

#include "lsp_backend.h"

#include "runtime_init.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
}

void skip(const char *name, const QString &reason)
{
    ++g_checks;
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", name, reason.toUtf8().constData());
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

QString slangServer()
{
    const QString fromEnv = qEnvironmentVariable("VEDIT_SLANG_SERVER");
    if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv)) {
        return fromEnv;
    }
    // The test binary runs from target/qt6-build/...; walk up to the checkout
    // root (the marker is scripts/build-qt.sh) and probe the AppDir wrapper.
    QDir directory(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 10; ++depth) {
        const QString wrapper =
            directory.filePath(QStringLiteral("packaging/runtime/out/AppDir/bin/slang-server"));
        if (QFileInfo(wrapper).isFile() && QFileInfo(wrapper).isExecutable()) {
            return wrapper;
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return QString();
}

bool waitReady(LspSessionBackend &backend, int timeoutMs, QString *error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const SessionSnapshot snapshot = backend.status();
        if (snapshot.state == BackendState::Ready) {
            return true;
        }
        if (snapshot.state == BackendState::Failed) {
            if (error != nullptr) {
                *error = snapshot.message;
            }
            return false;
        }
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    if (error != nullptr) {
        *error = QStringLiteral("the session did not reach Ready in %1 ms").arg(timeoutMs);
    }
    return false;
}

QString messagesOf(const QVector<DiagnosticEntry> &rows)
{
    QStringList messages;
    for (const DiagnosticEntry &row : rows) {
        messages.append(row.message);
    }
    return messages.join(QStringLiteral(" | "));
}

/// Ask for the current diagnostics of `file` and poll to completion, bounded.
bool diagnosticsOf(LspSessionBackend &backend, const QString &file, QVector<DiagnosticEntry> *rows,
                   bool *published, int timeoutMs)
{
    const quint64 id = backend.requestDiagnostics(file);
    if (id == 0) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const LspPolled polled = backend.poll(id);
        if (polled.pending) {
            QCoreApplication::processEvents();
            QThread::msleep(20);
            continue;
        }
        if (polled.failed) {
            std::printf("OBSERVED diagnostics failed: %s\n", qPrintable(polled.error));
            return false;
        }
        *rows = polled.diagnostics;
        if (published != nullptr) {
            *published = polled.published;
        }
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-slang-build-context-e2e"));

    const QString server = slangServer();
    if (server.isEmpty()) {
        skip("ac1.slang-server", QStringLiteral("no slang-server (set VEDIT_SLANG_SERVER)"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    std::printf("OBSERVED slang-server='%s'\n", qPrintable(server));

    QTemporaryDir dir;
    check("ac1.tempdir", dir.isValid());
    if (!dir.isValid()) {
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 1;
    }
    const QString root = dir.filePath(QStringLiteral("design"));
    const QString inc = dir.filePath(QStringLiteral("inc"));
    QDir().mkpath(root + QStringLiteral("/rtl"));
    QDir().mkpath(inc);
    check("ac1.fixture",
          writeFile(inc + QStringLiteral("/defs.svh"), QStringLiteral("`define WIDTH 8\n"))
              && writeFile(root + QStringLiteral("/rtl/top.sv"),
                           QStringLiteral("`include \"defs.svh\"\nmodule top;\n  logic "
                                          "[`WIDTH-1:0] data;\nendmodule\n")));
    const QString top = root + QStringLiteral("/rtl/top.sv");

    auto backend = createFfiLspSessionBackend();
    if (backend == nullptr) {
        skip("ac1.ffi-backend", QStringLiteral("this build has no Rust service layer"));
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return 0;
    }
    QString error;
    check("ac1.select",
          backend->selectBackend(LspBackendKind::Slang, server, QStringList{}, QStringList{},
                                 &error),
          error);
    // The include dirs are handed to the controller-level environment exactly as
    // the product does (the settings / project environment), but NOT through
    // argv: slang-server ignores argv.
    backend->selectBackend(LspBackendKind::Slang, server, QStringList{inc},
                           QStringList{QStringLiteral("WIDTH=8")}, &error);
    check("ac1.start", backend->start(server, root, QString(), QStringList{top}, &error), error);
    check("ac1.ready", waitReady(*backend, 25000, &error), error);

    // --- 1. BEFORE the context push: the include is unresolved ---------------
    QVector<DiagnosticEntry> before;
    bool beforePublished = false;
    const bool beforePolled =
        diagnosticsOf(*backend, top, &before, &beforePublished, 15000);
    std::printf("OBSERVED before published=%d rows=%s\n", beforePublished ? 1 : 0,
                qPrintable(messagesOf(before)));
    check("ac1.before-include-unresolved",
          beforePolled
              && (messagesOf(before).contains(QStringLiteral("No such file or directory"))
                  || messagesOf(before).contains(QStringLiteral("unknown macro"))),
          messagesOf(before));

    // --- 2. Push the context (what the controller does once the session is Ready)
    check("ac1.push-build-context", backend->pushBuildContext(&error), error);

    // --- 3. AFTER: the include resolves and the outline has the module ------
    QVector<DiagnosticEntry> after;
    bool resolved = false;
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 20000) {
        QVector<DiagnosticEntry> rows;
        if (!diagnosticsOf(*backend, top, &rows, nullptr, 5000)) {
            break;
        }
        after = rows;
        const QString text = messagesOf(rows);
        if (!text.contains(QStringLiteral("No such file or directory"))
            && !text.contains(QStringLiteral("unknown macro"))) {
            resolved = true;
            break;
        }
        QCoreApplication::processEvents();
        QThread::msleep(100);
    }
    std::printf("OBSERVED after rows=%s\n", qPrintable(messagesOf(after)));
    check("ac1.after-include-resolved", resolved, messagesOf(after));

    const quint64 symbolsId = backend->requestSymbols(top);
    QVector<OutlineSymbol> symbols;
    QElapsedTimer symbolTimer;
    symbolTimer.start();
    bool symbolAnswered = false;
    while (symbolsId != 0 && symbolTimer.elapsed() < 10000) {
        const LspPolled polled = backend->poll(symbolsId);
        if (polled.pending) {
            QCoreApplication::processEvents();
            QThread::msleep(20);
            continue;
        }
        symbols = polled.symbols;
        symbolAnswered = true;
        break;
    }
    QStringList symbolDescriptions;
    for (const OutlineSymbol &symbol : symbols) {
        symbolDescriptions.append(QStringLiteral("%1 (%2)").arg(symbol.name, symbol.detail));
    }
    std::printf("OBSERVED symbols answered=%d %s\n", symbolAnswered ? 1 : 0,
                qPrintable(symbolDescriptions.join(QStringLiteral(", "))));
    check("ac1.outline-has-symbols", symbolAnswered && !symbols.isEmpty(),
          symbolDescriptions.join(QStringLiteral(", ")));

    // --- 4. Delete one semicolon: `expected ';'` ----------------------------
    const QString broken =
        QStringLiteral("`include \"defs.svh\"\nmodule top;\n  logic [`WIDTH-1:0] data\n"
                       "endmodule\n");
    QString changeError;
    check("ac1.change-buffer", backend->changeBuffer(top, broken, &changeError), changeError);
    bool semicolon = false;
    QString semicolonDetail;
    QElapsedTimer brokenTimer;
    brokenTimer.start();
    while (brokenTimer.elapsed() < 20000) {
        QVector<DiagnosticEntry> rows;
        if (!diagnosticsOf(*backend, top, &rows, nullptr, 5000)) {
            break;
        }
        semicolonDetail = messagesOf(rows);
        if (semicolonDetail.contains(QStringLiteral("expected ';'"))) {
            semicolon = true;
            break;
        }
        QCoreApplication::processEvents();
        QThread::msleep(100);
    }
    std::printf("OBSERVED malformed rows=%s\n", qPrintable(semicolonDetail));
    check("ac1.missing-semicolon-reported", semicolon, semicolonDetail);

    backend->stop();
    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
