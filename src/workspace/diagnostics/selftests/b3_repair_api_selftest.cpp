// UX batch 2 / B3 repair — unit-level regressions for the two fixes whose
// behaviour is a pure function of the new API:
//
//   * `renameTargetAt()` - the B5 pre-dialog validation must reject number
//     literals, comment text, string text, system tasks and keywords (and still
//     accept a real identifier);
//   * `AutofixEngine::probeAsync()` - the hover probe must never block the
//     caller: it returns immediately and delivers the measured answer later.
//
// The engine leg uses the REAL verible-verilog-lint and SKIPs explicitly when
// the binary is absent, so the suite stays honest on a bare machine.
//
//   build: vedit-b3-repair-api-selftest [--verible-lint <path>]

#include "autofix.h"
#include "symbol_scan.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

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
    std::printf("CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.isEmpty() ? "" : detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const QString &why)
{
    ++g_skips;
    std::printf("SKIP %s  %s\n", name, why.toUtf8().constData());
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return done();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString veribleLint;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QLatin1String("--verible-lint") && index + 1 < argc) {
            veribleLint = QString::fromLocal8Bit(argv[++index]);
        }
    }

    // -----------------------------------------------------------------------
    std::printf("\n## B5 repair — renameTargetAt rejects everything it can name\n");
    const QString text = QStringLiteral(
        "module rn;\n"
        "  // commentword here\n"
        "  /* block comment word */\n"
        "  initial $display(\"hello\");\n"
        "  assign x = 1'b0;\n"
        "  localparam int n = 123;\n"
        "endmodule\n");
    auto target = [&text](int line, int column) { return renameTargetAt(text, line, column); };

    const RenameTarget valid = target(0, 8); // module NAME `rn`
    check("rename.valid-identifier", valid.valid && valid.symbol == QStringLiteral("rn"),
          QStringLiteral("symbol='%1' reason='%2'").arg(valid.symbol, valid.reason));
    const RenameTarget signal = target(4, 9); // `x` on the assign line
    check("rename.valid-signal", signal.valid && signal.symbol == QStringLiteral("x"),
          QStringLiteral("symbol='%1'").arg(signal.symbol));

    const RenameTarget keyword = target(0, 2);
    check("rename.keyword", !keyword.valid && keyword.reason.contains(QStringLiteral("keyword")),
          keyword.reason);
    const RenameTarget commentLine = target(1, 8);
    check("rename.line-comment",
          !commentLine.valid && commentLine.reason.contains(QStringLiteral("comment")),
          commentLine.reason);
    const RenameTarget commentBlock = target(2, 12);
    check("rename.block-comment",
          !commentBlock.valid && commentBlock.reason.contains(QStringLiteral("comment")),
          commentBlock.reason);
    const RenameTarget insideString = target(3, 21);
    check("rename.string",
          !insideString.valid && insideString.reason.contains(QStringLiteral("string")),
          insideString.reason);
    const RenameTarget systemTask = target(3, 12);
    check("rename.system-task",
          !systemTask.valid && systemTask.reason.contains(QStringLiteral("system task")),
          systemTask.reason);
    const RenameTarget decimal = target(5, 22);
    check("rename.number-decimal",
          !decimal.valid && decimal.reason.contains(QStringLiteral("number literal")),
          decimal.reason);
    const RenameTarget based = target(4, 15);
    check("rename.number-based",
          !based.valid && based.reason.contains(QStringLiteral("number literal")), based.reason);
    const RenameTarget none = target(7, 0); // the blank line after the source
    check("rename.no-token", !none.valid && none.reason.contains(QStringLiteral("no symbol")),
          none.reason);
    const RenameTarget outside = target(99, 0);
    check("rename.outside-document", !outside.valid, outside.reason);

    // -----------------------------------------------------------------------
    std::printf("\n## item 9 repair — probeAsync never blocks the caller\n");
    if (veribleLint.isEmpty() || !QFileInfo(veribleLint).isExecutable()) {
        skip("autofix.async", QStringLiteral("no verible-verilog-lint"));
    } else {
        QTemporaryDir dir;
        AutofixEngine engine(veribleLint);
        const QString probeText = QStringLiteral("module af;\n  logic x;   \nendmodule\n");
        bool called = false;
        bool supported = false;
        QString error;
        QElapsedTimer timer;
        timer.start();
        engine.probeAsync(QStringLiteral("no-trailing-spaces"),
                          dir.filePath(QStringLiteral("af.sv")), probeText,
                          [&called, &supported, &error](const AutofixOutcome &outcome) {
                              called = true;
                              supported = outcome.supported;
                              error = outcome.error;
                          });
        const qint64 elapsed = timer.elapsed();
        check("autofix.async-returns-immediately", elapsed < 100,
              QStringLiteral("elapsed-ms=%1 called=%2").arg(elapsed).arg(called ? 1 : 0));
        check("autofix.async-not-called-inline", !called,
              QStringLiteral("the child process must not run on the caller's stack"));
        const bool finished = waitUntil([&called]() { return called; }, 20000);
        check("autofix.async-completes", finished && supported,
              QStringLiteral("called=%1 supported=%2 error='%3'")
                  .arg(called ? 1 : 0)
                  .arg(supported ? 1 : 0)
                  .arg(error));
        const quint64 probes = engine.probeCount();
        bool cachedCalled = false;
        engine.probeAsync(QStringLiteral("no-trailing-spaces"),
                          dir.filePath(QStringLiteral("af.sv")), probeText,
                          [&cachedCalled](const AutofixOutcome &) { cachedCalled = true; });
        check("autofix.async-cache-is-deferred", !cachedCalled);
        check("autofix.async-cache-completes",
              waitUntil([&cachedCalled]() { return cachedCalled; }, 1000)
                  && engine.probeCount() == probes,
              QStringLiteral("probes=%1").arg(engine.probeCount()));
    }

    std::printf("\nSUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
