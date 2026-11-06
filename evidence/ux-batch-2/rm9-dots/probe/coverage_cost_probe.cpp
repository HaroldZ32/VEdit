// RM-9 (T20) evidence probe: the wall time of ONE project lint pass over a
// generated project of N files, through the PRODUCT seam
// (`createFfiProjectLintBackend()` = the same `vd_deeplint_batch_*` the Lint
// Project action and the project-open pass use). No GUI.
//
//   coverage_cost_probe --files 1176 --engine verible \
//       --verible /path/verible-verilog-lint [--slang /path/slang]
//   coverage_cost_probe --files 1176 --engine slang --slang /path/slang \
//       --verible /path/verible-verilog-lint
//
// Prints one machine line:
//   PASS-COST files=<n> engine=<slang|verible> elapsed_ms=<n> phase=<n> rows=<n>
// Reads/writes nothing but a temporary directory.

#include "diagnostics_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

bool writeModule(const QString &path, int index)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    // A small, LINT-CLEAN module (a full pass must not be dominated by
    // diagnostic formatting); one file reports a warning so the pass has real
    // rows to carry back.
    const QByteArray text = QStringLiteral("module m%1;\n  logic x;\nendmodule\n")
                                .arg(index)
                                .toUtf8();
    file.write(text);
    file.close();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int fileCount = 0;
    QString engineName = QStringLiteral("verible");
    QString slangPath;
    QString veriblePath;
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        const auto next = [&](QString *out) {
            if (index + 1 < argc) {
                *out = QString::fromLocal8Bit(argv[++index]);
            }
        };
        if (argument == QLatin1String("--files")) {
            QString value;
            next(&value);
            fileCount = value.toInt();
        } else if (argument == QLatin1String("--engine")) {
            next(&engineName);
        } else if (argument == QLatin1String("--slang")) {
            next(&slangPath);
        } else if (argument == QLatin1String("--verible")) {
            next(&veriblePath);
        }
    }
    if (fileCount <= 0) {
        std::printf("PASS-COST invalid: --files <n> is required\n");
        return 2;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("PASS-COST invalid: no temporary directory\n");
        return 2;
    }
    QStringList files;
    files.reserve(fileCount);
    for (int index = 0; index < fileCount; ++index) {
        const QString path = dir.filePath(QStringLiteral("m%1.sv").arg(index, 5, 10, QLatin1Char('0')));
        if (!writeModule(path, index)) {
            std::printf("PASS-COST invalid: cannot write %s\n", qPrintable(path));
            return 2;
        }
        files.append(path);
    }

    auto backend = createFfiProjectLintBackend();
    if (!backend) {
        std::printf("PASS-COST SKIP: this build has no Rust lint service\n");
        return 0;
    }
    LintPassRequest request;
    request.files = files;
    request.engine = engineName == QLatin1String("slang") ? 0 : 1;
    request.slangPath = slangPath;
    request.veriblePath = veriblePath;
    QString error;
    QElapsedTimer timer;
    timer.start();
    const quint64 job = backend->start(request, &error);
    if (job == 0) {
        std::printf("PASS-COST invalid: start refused: %s\n", qPrintable(error));
        return 2;
    }
    LintPassSnapshot snapshot = backend->progress(job);
    while (snapshot.running()) {
        QThread::msleep(20);
        snapshot = backend->progress(job);
    }
    LspPolled result = backend->poll(job);
    while (result.pending) {
        QThread::msleep(20);
        result = backend->poll(job);
    }
    const qint64 elapsed = timer.elapsed();
    const int rows = result.ready ? result.diagnostics.size() : -1;
    const int phase = static_cast<int>(snapshot.phase);
    std::printf("PASS-COST files=%d engine=%s elapsed_ms=%lld phase=%d rows=%d failed=%d\n",
                fileCount, qPrintable(engineName), static_cast<long long>(elapsed), phase, rows,
                result.failed ? 1 : 0);
    std::fflush(stdout);
    backend->release(job);
    return result.failed ? 1 : 0;
}
