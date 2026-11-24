// VHDL merge lint (AC-6.4) — see vhdl_lint.h for the rules this file keeps.
//
// Everything here was measured against the pinned tools before it was written
// (GHDL 6.0.0 / VSG 3.36.0 in `packaging/runtime/out/AppDir/bin/`):
//
//   $ ghdl -a --std=08 -Wunused --workdir=<tmp> f.vhd
//   f.vhd:15:7:error: no declaration for "a"
//         a <= '1';
//         ^
//   $ vsg -f f.vhd -js <tmp>/vsg.json
//   {"files":[{"file_path":"f.vhd","violations":[{"rule":"process_016",
//     "linenumber":14,"severity":"Error","solution":"Add label for process statement"}]}]}
//
// Two measured traps this file is built around:
//   * GHDL echoes the source line and a caret line after every diagnostic, so a
//     line-based parser must key on the `<path>:<line>:<col>:<level>:` prefix —
//     the echo has no prefix and can never become a row;
//   * VSG REFUSES a file it cannot parse (stderr `Error while processing …`) and
//     still writes a JSON with `violations: []`. An empty list therefore means
//     "clean" ONLY when that refusal is absent; the refusal becomes an explicit
//     note instead of a silent zero.

#include "vhdl_lint.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace vedit::workspace {

namespace {

/// GHDL's diagnostic line. The level alternates are the tool's own vocabulary
/// (`error`, `warning`, `note`; `fatal` is accepted because GHDL has used it).
const QRegularExpression &ghdlDiagnosticPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(.*?):(\\d+):(\\d+):(error|warning|note|fatal):\\s*(.*)$"));
    return pattern;
}

/// Trim + squeeze whitespace, the same normalization the panel's merge key uses,
/// so a row's message reads the same everywhere.
QString normalized(const QString &text)
{
    QString out = text.simplified();
    return out;
}

/// The identity of one finding.
///
/// In the product this IS `diagnostics::diagnosticMergeKey()` — the panel's own
/// function — so the merge and the display cannot disagree about what "the same
/// finding" is. A build WITHOUT the diagnostics lane (never the delivered one)
/// keeps a same-shaped local key instead of failing to link; the shape is spelled
/// out so the two cannot silently drift: absolute path, 0-based line, severity,
/// whitespace-squeezed message.
QString rowMergeKey(const diagnostics::DiagnosticEntry &entry)
{
#if defined(VEDIT_VHDL_HAVE_MERGE_KEY)
    return diagnostics::diagnosticMergeKey(entry, diagnostics::DiagnosticOrigin::Lsp);
#else
    QString path = entry.file;
    if (!path.isEmpty()) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!absolute.isEmpty()) {
            path = absolute;
        }
    }
    return QStringLiteral("%1|%2|%3|%4")
        .arg(path)
        .arg(entry.line)
        .arg(entry.severity)
        .arg(entry.message.simplified());
#endif
}

QString firstLines(const QString &text, int lines)
{
    QStringList kept;
    const QStringList raw = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : raw) {
        if (kept.size() >= lines) {
            break;
        }
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            kept.append(trimmed);
        }
    }
    return kept.join(QStringLiteral(" | "));
}

} // namespace

int lintSeverityForGhdl(const QString &level)
{
    const QString value = level.trimmed().toLower();
    if (value == QStringLiteral("error") || value == QStringLiteral("fatal")) {
        return 1; // error
    }
    if (value == QStringLiteral("warning")) {
        return 2; // warning
    }
    if (value == QStringLiteral("note") || value == QStringLiteral("info")) {
        return 3; // information
    }
    return 0; // unknown vocabulary — the row is KEPT with severity 0
}

int lintSeverityForVsg(const QString &severity)
{
    const QString value = severity.trimmed().toLower();
    if (value == QStringLiteral("error") || value == QStringLiteral("fatal")) {
        return 1; // error
    }
    if (value == QStringLiteral("warning")) {
        return 2; // warning
    }
    if (value == QStringLiteral("info") || value == QStringLiteral("information")) {
        return 3; // information
    }
    return 0; // unknown vocabulary — the row is KEPT with severity 0
}

QString VhdlLintReport::summary() const
{
    return QStringLiteral("ghdl=%1 vsg=%2 merged=%3 deduped=%4 missing=[%5] notes=%6")
        .arg(ghdlRows)
        .arg(vsgRows)
        .arg(rows.size())
        .arg(dedupedRows)
        .arg(missing.join(QLatin1Char(',')))
        .arg(notes.size());
}

QVector<diagnostics::DiagnosticEntry> parseGhdlDiagnostics(const QString &text,
                                                           const QString &file)
{
    QVector<diagnostics::DiagnosticEntry> rows;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = ghdlDiagnosticPattern().match(line);
        if (!match.hasMatch()) {
            continue; // the echoed source line, the caret, or noise
        }
        diagnostics::DiagnosticEntry entry;
        const QString path = match.captured(1).trimmed();
        entry.file = path.isEmpty() ? file : path;
        // GHDL reports 1-based line/column; the panel's rows are 0-based.
        entry.line = match.captured(2).toInt() - 1;
        entry.column = match.captured(3).toInt() - 1;
        entry.endLine = entry.line;
        entry.endColumn = entry.column;
        entry.severity = lintSeverityForGhdl(match.captured(4));
        entry.source = QStringLiteral("ghdl");
        entry.message = normalized(match.captured(5));
        // GHDL has no rule ids: `code` stays empty (nothing invented).
        rows.append(entry);
    }
    return rows;
}

QVector<diagnostics::DiagnosticEntry> parseVsgDiagnostics(const QByteArray &json,
                                                          const QString &file,
                                                          QStringList *notes)
{
    QVector<diagnostics::DiagnosticEntry> rows;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (notes != nullptr) {
            notes->append(QStringLiteral("vsg: no machine-readable report (%1)")
                              .arg(parseError.errorString()));
        }
        return rows;
    }
    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    bool sawFile = false;
    for (const QJsonValue &fileValue : files) {
        const QJsonObject fileObject = fileValue.toObject();
        const QString path = fileObject.value(QStringLiteral("file_path")).toString(file);
        if (QFileInfo(path).fileName() == QFileInfo(file).fileName()
            || path == file) {
            sawFile = true;
        }
        const QJsonArray violations =
            fileObject.value(QStringLiteral("violations")).toArray();
        for (const QJsonValue &violationValue : violations) {
            const QJsonObject violation = violationValue.toObject();
            diagnostics::DiagnosticEntry entry;
            entry.file = path.isEmpty() ? file : QFileInfo(file).absoluteFilePath();
            // VSG reports a 1-based LINE and no column at all: the row spans the
            // whole line (column 0, endColumn 0), which is what it can support.
            entry.line = violation.value(QStringLiteral("linenumber")).toInt() - 1;
            entry.column = 0;
            entry.endLine = entry.line;
            entry.endColumn = 0;
            entry.severity =
                lintSeverityForVsg(violation.value(QStringLiteral("severity")).toString());
            entry.code = violation.value(QStringLiteral("rule")).toString();
            entry.source = QStringLiteral("vsg");
            entry.message =
                normalized(violation.value(QStringLiteral("solution")).toString());
            rows.append(entry);
        }
    }
    if (!sawFile && files.isEmpty() && notes != nullptr) {
        notes->append(QStringLiteral("vsg: the report lists no file (nothing analysed)"));
    }
    return rows;
}

QVector<diagnostics::DiagnosticEntry> mergeLintRows(
    const QVector<diagnostics::DiagnosticEntry> &ghdl,
    const QVector<diagnostics::DiagnosticEntry> &vsg, int *deduped)
{
    QVector<diagnostics::DiagnosticEntry> merged;
    merged.reserve(ghdl.size() + vsg.size());
    QSet<QString> seen;
    int removed = 0;
    const auto take = [&merged, &seen, &removed](const diagnostics::DiagnosticEntry &entry) {
        // The panel's OWN key (path + 0-based line + normalized severity +
        // whitespace-normalized message). Reusing it is the point: the merge and
        // the display can never disagree about "the same finding".
        const QString key = rowMergeKey(entry);
        if (seen.contains(key)) {
            ++removed; // counted, never silently dropped
            return;
        }
        seen.insert(key);
        merged.append(entry);
    };
    for (const diagnostics::DiagnosticEntry &entry : ghdl) {
        take(entry);
    }
    for (const diagnostics::DiagnosticEntry &entry : vsg) {
        take(entry);
    }
    std::sort(merged.begin(), merged.end(),
              [](const diagnostics::DiagnosticEntry &left,
                 const diagnostics::DiagnosticEntry &right) {
                  if (left.file != right.file) {
                      return left.file < right.file;
                  }
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  if (left.column != right.column) {
                      return left.column < right.column;
                  }
                  if (left.severity != right.severity) {
                      return left.severity < right.severity;
                  }
                  return left.source < right.source;
              });
    if (deduped != nullptr) {
        *deduped = removed;
    }
    return merged;
}

// --- the asynchronous run ---------------------------------------------------

VhdlLintJob::VhdlLintJob(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<vedit::workspace::VhdlLintReport>("vedit::workspace::VhdlLintReport");
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this]() {
        report_.notes.append(
            QStringLiteral("%1: no answer within %2 ms (the child was killed)")
                .arg(currentTool_.isEmpty() ? QStringLiteral("tool") : currentTool_)
                .arg(timeoutMs_));
        killAll();
        finish();
    });
}

VhdlLintJob::~VhdlLintJob()
{
    killAll();
    delete work_;
    work_ = nullptr;
}

bool VhdlLintJob::start(const QString &file, const QString &ghdlProgram,
                        const QString &vsgProgram, int timeoutMs)
{
    if (running_) {
        return false;
    }
    if (!QFileInfo(file).isFile()) {
        report_.notes.append(QStringLiteral("no such file: %1").arg(file));
        finish();
        return false;
    }
    file_ = QFileInfo(file).absoluteFilePath();
    ghdlProgram_ = ghdlProgram;
    vsgProgram_ = vsgProgram;
    timeoutMs_ = timeoutMs > 0 ? timeoutMs : 20000;
    report_ = VhdlLintReport{};
    finishedEmitted_ = false;
    running_ = true;
    if (work_ == nullptr) {
        work_ = new QTemporaryDir();
    }
    if (!work_->isValid()) {
        report_.notes.append(QStringLiteral("cannot create a temporary work library"));
        finish();
        return false;
    }
    if (ghdlProgram_.isEmpty()) {
        report_.missing.append(QStringLiteral("ghdl"));
    }
    if (vsgProgram_.isEmpty()) {
        report_.missing.append(QStringLiteral("vsg"));
    }
    if (!ghdlProgram_.isEmpty()) {
        startGhdl();
    } else if (!vsgProgram_.isEmpty()) {
        startVsg();
    } else {
        finish();
    }
    return true;
}

void VhdlLintJob::startGhdl()
{
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    // `-a` (analyse) covers syntax AND semantics; `--std=08` is the lane's
    // policy (the newest ratified standard, and VHDL-93 sources analyse under
    // it); `-Wunused` is the ONE extra warning flag (measured: it turns
    // "procedure is never referenced" into a real `warning:` row); `--workdir`
    // keeps GHDL's work library OUT of the project tree and OUT of a shared
    // directory (a shared one reports a spurious `-Wlibrary` redefinition).
    const QStringList args{QStringLiteral("-a"),
                           QStringLiteral("--std=08"),
                           QStringLiteral("-Wunused"),
                           QStringLiteral("--workdir=%1").arg(work_->path()),
                           file_};
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        ghdlFinished();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            report_.notes.append(QStringLiteral("ghdl: cannot start `%1`").arg(ghdlProgram_));
            report_.ghdlRan = false;
            killAll();
            if (!vsgProgram_.isEmpty()) {
                startVsg();
            } else {
                finish();
            }
        }
    });
    currentTool_ = QStringLiteral("ghdl");
    timer_->start(timeoutMs_);
    process_->start(ghdlProgram_, args);
}

void VhdlLintJob::ghdlFinished()
{
    timer_->stop();
    if (process_ == nullptr) {
        return;
    }
    ghdlOutput_ = process_->readAll();
    report_.ghdlRan = true;
    const QVector<diagnostics::DiagnosticEntry> ghdlRows =
        parseGhdlDiagnostics(QString::fromUtf8(ghdlOutput_), file_);
    report_.ghdlRows = ghdlRows.size();
    // GHDL exits 1 when it found errors: that is a RESULT, not a failure. Only
    // "ran, produced nothing, exited nonzero" needs an explanation.
    if (ghdlRows.isEmpty() && process_->exitStatus() == QProcess::CrashExit) {
        report_.notes.append(QStringLiteral("ghdl: crashed (no diagnostics parsed)"));
    } else if (ghdlRows.isEmpty() && process_->exitCode() != 0
               && !ghdlOutput_.trimmed().isEmpty()) {
        report_.notes.append(QStringLiteral("ghdl: exit %1 with unparsable output (%2)")
                                 .arg(process_->exitCode())
                                 .arg(firstLines(QString::fromUtf8(ghdlOutput_), 2)));
    }
    process_->deleteLater();
    process_ = nullptr;
    pendingGhdl_ = ghdlRows;
    if (!vsgProgram_.isEmpty()) {
        startVsg();
    } else {
        finish();
    }
}

void VhdlLintJob::startVsg()
{
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    vsgJsonPath_ = QDir(work_->path()).filePath(QStringLiteral("vsg.json"));
    QFile::remove(vsgJsonPath_);
    const QStringList args{QStringLiteral("-f"), file_, QStringLiteral("-js"), vsgJsonPath_};
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        vsgFinished();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            report_.notes.append(QStringLiteral("vsg: cannot start `%1`").arg(vsgProgram_));
            report_.vsgRan = false;
            killAll();
            finish();
        }
    });
    currentTool_ = QStringLiteral("vsg");
    timer_->start(timeoutMs_);
    process_->start(vsgProgram_, args);
}

void VhdlLintJob::vsgFinished()
{
    timer_->stop();
    if (process_ == nullptr) {
        return;
    }
    const QString output = QString::fromUtf8(process_->readAll());
    const int exitCode = process_->exitCode();
    const bool crashed = process_->exitStatus() == QProcess::CrashExit;
    process_->deleteLater();
    process_ = nullptr;
    report_.vsgRan = true;

    QFile json(vsgJsonPath_);
    const bool haveJson = json.open(QIODevice::ReadOnly);
    const QByteArray bytes = haveJson ? json.readAll() : QByteArray();
    if (haveJson) {
        json.close();
    }
    // MEASURED: VSG REFUSES a file it cannot parse — stderr
    // `Error while processing <file>:`, exit 1 — and still writes
    // `violations: []`. An empty list is "clean" only when that refusal is
    // absent; this branch is what keeps "refused" and "clean" distinguishable.
    if (output.contains(QStringLiteral("Error while processing"))) {
        report_.notes.append(QStringLiteral("vsg: refused this file (%1)")
                                 .arg(firstLines(output, 2)));
    }
    if (!haveJson) {
        report_.notes.append(QStringLiteral("vsg: no report file was written (exit %1)%2")
                                 .arg(exitCode)
                                 .arg(crashed ? QStringLiteral(" [crashed]") : QString()));
        report_.vsgRows = 0;
    } else {
        QVector<diagnostics::DiagnosticEntry> vsgRows =
            parseVsgDiagnostics(bytes, file_, &report_.notes);
        report_.vsgRows = vsgRows.size();
        mergedFromVsg_ = vsgRows;
    }
    finish();
}

void VhdlLintJob::finish()
{
    if (finishedEmitted_) {
        return;
    }
    finishedEmitted_ = true;
    running_ = false;
    timer_->stop();
    QVector<diagnostics::DiagnosticEntry> merged =
        mergeLintRows(pendingGhdl_, mergedFromVsg_, &report_.dedupedRows);
    report_.rows = merged;
    killAll();
    emit finished(report_);
}

void VhdlLintJob::killAll()
{
    timer_->stop();
    if (process_ != nullptr) {
        process_->kill();
        process_->waitForFinished(500);
        process_->deleteLater();
        process_ = nullptr;
    }
}

bool VhdlLintJob::waitForFinished(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!finishedEmitted_ && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return finishedEmitted_;
}

} // namespace vedit::workspace

// ---------------------------------------------------------------------------
// Evidence probe (VEDIT_VHDL_LINT_PROBE_MAIN).
//
// Compiled from THIS file (src/workspace/vhdl/CMakeLists.txt) exactly like the
// lane's LSP probe, so the frozen write scope stays exact and none of it links
// into the product. It drives the REAL GHDL + VSG over seven fixtures and prints
// every row, so the log shows what the tools said instead of what the code
// believes.
//
//   vedit-vhdl-lint-probe [--ghdl <path>] [--vsg <path>]
// ---------------------------------------------------------------------------
#if defined(VEDIT_VHDL_LINT_PROBE_MAIN)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("VHDL-CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

QString writeFixture(const QString &dir, const QString &name, const QString &text)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }
    file.write(text.toUtf8());
    return path;
}

void printFixture(const QString &name, const QString &text)
{
    std::printf("VHDL-FIXTURE %s (1-based lines)\n", name.toUtf8().constData());
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (int index = 0; index < lines.size(); ++index) {
        if (index == lines.size() - 1 && lines.at(index).isEmpty()) {
            break;
        }
        std::printf("  %2d | %s\n", index + 1, lines.at(index).toUtf8().constData());
    }
    std::fflush(stdout);
}

void printRows(const QString &fixture, const vedit::workspace::VhdlLintReport &report)
{
    std::printf("VHDL-MERGE %s %s\n", fixture.toUtf8().constData(),
                report.summary().toUtf8().constData());
    for (const auto &row : report.rows) {
        std::printf("VHDL-ROW %s source=%s sev=%d line=%d col=%d code='%s' msg='%s'\n",
                    QFileInfo(row.file).fileName().toUtf8().constData(),
                    row.source.toUtf8().constData(), row.severity, row.line + 1,
                    row.column + 1, row.code.toUtf8().constData(),
                    row.message.toUtf8().constData());
    }
    for (const QString &note : report.notes) {
        std::printf("VHDL-NOTE %s: %s\n", fixture.toUtf8().constData(),
                    note.toUtf8().constData());
    }
    for (const QString &missing : report.missing) {
        std::printf("VHDL-MISSING %s: %s\n", fixture.toUtf8().constData(),
                    missing.toUtf8().constData());
    }
    std::fflush(stdout);
}

vedit::workspace::VhdlLintReport run(const QString &file, const QString &ghdl,
                                     const QString &vsg)
{
    vedit::workspace::VhdlLintReport captured;
    vedit::workspace::VhdlLintJob job;
    QObject::connect(&job, &vedit::workspace::VhdlLintJob::finished,
                     [&captured](const vedit::workspace::VhdlLintReport &report) {
                         captured = report;
                     });
    if (!job.start(file, ghdl, vsg)) {
        return captured;
    }
    job.waitForFinished(60000);
    return captured;
}

/// A fixture that VSG itself made compliant (`vsg -f x --fix`), so "clean" is
/// the TOOL's verdict, not our idea of clean. The history matters: the first
/// version of this fixture (clean-before) still carried two VSG violations.
const char *kClean =
    "library ieee;\n"
    "  use ieee.std_logic_1164.all;\n"
    "\n"
    "entity clean is\n"
    "  port (\n"
    "    clk  : in    std_logic;\n"
    "    din  : in    std_logic;\n"
    "    dout : out   std_logic\n"
    "  );\n"
    "end entity clean;\n"
    "\n"
    "architecture rtl of clean is\n"
    "\n"
    "begin\n"
    "\n"
    "  p_clk : process (clk) is\n"
    "  begin\n"
    "\n"
    "    if rising_edge(clk) then\n"
    "      dout <= din;\n"
    "    end if;\n"
    "\n"
    "  end process p_clk;\n"
    "\n"
    "end architecture rtl;\n";

const char *kCleanBefore =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "\n"
    "entity clean is\n"
    "  port (\n"
    "    clk : in std_logic\n"
    "  );\n"
    "end entity clean;\n"
    "\n"
    "architecture rtl of clean is\n"
    "begin\n"
    "  process (clk) is\n"
    "  begin\n"
    "    if rising_edge(clk) then\n"
    "      null;\n"
    "    end if;\n"
    "  end process;\n"
    "end architecture rtl;\n";

// VSG-CLEAN on purpose (measured: this exact text yields VSG 0 violations), so
// the fixture isolates the GHDL half. The first version of it was NOT VSG-clean
// (`port_007`: the spaces after `in`) — "GHDL-only" is a claim about BOTH tools.
const char *kGhdlOnly =
    "library ieee;\n"
    "  use ieee.std_logic_1164.all;\n"
    "\n"
    "entity ghdl_only is\n"
    "  port (\n"
    "    clk : in    std_logic\n"
    "  );\n"
    "end entity ghdl_only;\n"
    "\n"
    "architecture rtl of ghdl_only is\n"
    "\n"
    "begin\n"
    "\n"
    "  p_clk : process (clk) is\n"
    "  begin\n"
    "\n"
    "    if rising_edge(clk) then\n"
    "      dout <= '1';\n"
    "    end if;\n"
    "\n"
    "  end process p_clk;\n"
    "\n"
    "end architecture rtl;\n";

const char *kVsgOnly =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "\n"
    "entity vsg_only is\n"
    "  port (\n"
    "    clk : in std_logic\n"
    "  );\n"
    "end entity vsg_only;\n"
    "\n"
    "architecture rtl of vsg_only is\n"
    "  signal counter : integer := 0;\n"
    "begin\n"
    "  process (clk) is\n"
    "  begin\n"
    "    if rising_edge(clk) then\n"
    "      counter <= counter + 1;\n"
    "    end if;\n"
    "  end process;\n"
    "end architecture rtl;\n";

// AC-6.4's coexistence case: ONE GHDL analysis error (the undeclared signal
// `dout`) and ONE VSG style violation (the missing process label) in the SAME
// file, both parseable by both tools.
const char *kMixed =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "\n"
    "entity mixed is\n"
    "  port (\n"
    "    clk : in std_logic\n"
    "  );\n"
    "end entity mixed;\n"
    "\n"
    "architecture rtl of mixed is\n"
    "  signal counter : integer := 0;\n"
    "begin\n"
    "  process (clk) is\n"
    "  begin\n"
    "    if rising_edge(clk) then\n"
    "      counter <= counter + 1;\n"
    "      dout <= '1';\n"
    "    end if;\n"
    "  end process;\n"
    "end architecture rtl;\n";

// A real SYNTAX error (missing semicolon). Measured: GHDL stops after it, and
// VSG REFUSES the file — the case that must not look like "clean".
const char *kSyntax =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "\n"
    "entity syntax is\n"
    "  port (\n"
    "    clk : in std_logic\n"
    "  );\n"
    "end entity syntax;\n"
    "\n"
    "architecture rtl of syntax is\n"
    "  signal counter : integer := 0;\n"
    "begin\n"
    "  p_clk : process (clk) is\n"
    "  begin\n"
    "    if rising_edge(clk) then\n"
    "      counter <= counter + 1\n"
    "    end if;\n"
    "  end process p_clk;\n"
    "end architecture rtl;\n";

// Two SEMANTIC errors on two different lines: GHDL reports every one of them,
// which is what the "line numbers stay correct after skipping the echo/caret"
// assertion needs.
const char *kTwoErrors =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "\n"
    "entity two is\n"
    "  port (\n"
    "    clk : in std_logic\n"
    "  );\n"
    "end entity two;\n"
    "\n"
    "architecture rtl of two is\n"
    "begin\n"
    "  p_clk : process (clk) is\n"
    "  begin\n"
    "    if rising_edge(clk) then\n"
    "      a <= '1';\n"
    "      b <= '0';\n"
    "    end if;\n"
    "  end process p_clk;\n"
    "end architecture rtl;\n";

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString ghdl;
    QString vsg;
    const QStringList args = QCoreApplication::arguments();
    for (int index = 1; index < args.size(); ++index) {
        if (args.at(index) == QStringLiteral("--ghdl") && index + 1 < args.size()) {
            ghdl = args.at(++index);
        } else if (args.at(index) == QStringLiteral("--vsg") && index + 1 < args.size()) {
            vsg = args.at(++index);
        }
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("VHDL-LINT-PROBE cannot create a fixture directory\n");
        return 2;
    }
    std::printf("VHDL-TOOLS ghdl='%s' vsg='%s'\n", ghdl.toUtf8().constData(),
                vsg.toUtf8().constData());

    const QVector<QPair<QString, QString>> fixtures{
        {QStringLiteral("clean"), QString::fromUtf8(kClean)},
        {QStringLiteral("clean-before"), QString::fromUtf8(kCleanBefore)},
        {QStringLiteral("ghdl-only"), QString::fromUtf8(kGhdlOnly)},
        {QStringLiteral("vsg-only"), QString::fromUtf8(kVsgOnly)},
        {QStringLiteral("mixed"), QString::fromUtf8(kMixed)},
        {QStringLiteral("syntax"), QString::fromUtf8(kSyntax)},
        {QStringLiteral("two-errors"), QString::fromUtf8(kTwoErrors)},
    };
    QHash<QString, vedit::workspace::VhdlLintReport> reports;
    for (const auto &fixture : fixtures) {
        const QString path = writeFixture(dir.path(), fixture.first + QStringLiteral(".vhd"),
                                          fixture.second);
        printFixture(fixture.first, fixture.second);
        reports.insert(fixture.first, run(path, ghdl, vsg));
        printRows(fixture.first, reports.value(fixture.first));
    }

    // --- 1. a genuinely clean file -----------------------------------------
    {
        const auto &report = reports.value(QStringLiteral("clean"));
        check("clean.ghdl-zero-rows", report.ghdlRows == 0, report.summary());
        check("clean.vsg-zero-rows", report.vsgRows == 0, report.summary());
        check("clean.merged-zero-rows", report.rows.isEmpty() && report.clean());
    }
    // --- 2. the SAME file before VSG made it compliant ---------------------
    {
        const auto &report = reports.value(QStringLiteral("clean-before"));
        check("clean-before.vsg-two-rows", report.vsgRows == 2, report.summary());
        check("clean-before.ghdl-zero-rows", report.ghdlRows == 0, report.summary());
    }
    // --- 3. GHDL only ------------------------------------------------------
    {
        const auto &report = reports.value(QStringLiteral("ghdl-only"));
        check("ghdl-only.one-error", report.ghdlRows == 1 && report.rows.size() == 1,
              report.summary());
        check("ghdl-only.severity-error",
              !report.rows.isEmpty() && report.rows.first().severity == 1);
        check("ghdl-only.line-and-source",
              !report.rows.isEmpty() && report.rows.first().line == 17
                  && report.rows.first().source == QStringLiteral("ghdl"));
        check("ghdl-only.vsg-clean", report.vsgRows == 0);
    }
    // --- 4. VSG only -------------------------------------------------------
    {
        const auto &report = reports.value(QStringLiteral("vsg-only"));
        check("vsg-only.vsg-three-rows", report.vsgRows == 3, report.summary());
        check("vsg-only.ghdl-clean", report.ghdlRows == 0);
        bool everyVsgRowIsVsg = true;
        bool everyVsgRowHasRule = true;
        for (const auto &row : report.rows) {
            everyVsgRowIsVsg = everyVsgRowIsVsg && row.source == QStringLiteral("vsg");
            everyVsgRowHasRule = everyVsgRowHasRule && !row.code.isEmpty();
        }
        check("vsg-only.sources-labelled", everyVsgRowIsVsg);
        check("vsg-only.rule-ids", everyVsgRowHasRule);
    }
    // --- 5. AC-6.4 coexistence: GHDL error + VSG violation, one file -------
    {
        const auto &report = reports.value(QStringLiteral("mixed"));
        bool sawGhdl = false;
        bool sawVsg = false;
        for (const auto &row : report.rows) {
            sawGhdl = sawGhdl || row.source == QStringLiteral("ghdl");
            sawVsg = sawVsg || row.source == QStringLiteral("vsg");
        }
        check("mixed.ghdl-row-present", sawGhdl, report.summary());
        check("mixed.vsg-row-present", sawVsg, report.summary());
        check("mixed.merged-count", report.rows.size() == report.ghdlRows + report.vsgRows
                  - report.dedupedRows,
              report.summary());
    }
    // --- 6. a SYNTAX error: GHDL row + an EXPLICIT vsg note (never silent) --
    {
        const auto &report = reports.value(QStringLiteral("syntax"));
        check("syntax.ghdl-error", report.ghdlRows == 1, report.summary());
        check("syntax.vsg-refusal-is-noted", !report.notes.isEmpty(), report.summary());
        check("syntax.vsg-has-no-rows", report.vsgRows == 0);
        check("syntax.not-clean", !report.clean());
    }
    // --- 7. two errors on two lines: numbers stay correct ------------------
    {
        const auto &report = reports.value(QStringLiteral("two-errors"));
        QVector<int> lines;
        for (const auto &row : report.rows) {
            if (row.source == QStringLiteral("ghdl")) {
                lines.append(row.line);
            }
        }
        std::sort(lines.begin(), lines.end());
        check("two-errors.row-count", lines.size() == 2, report.summary());
        check("two-errors.lines-are-15-and-16",
              lines.size() == 2 && lines.at(0) == 14 && lines.at(1) == 15,
              [&lines]() {
                  QStringList text;
                  for (int line : lines) {
                      text.append(QString::number(line + 1));
                  }
                  return text.join(QLatin1Char(','));
              }());
    }
    // --- 8. the severity MAP, exhaustively (synthetic inputs) --------------
    {
        check("map.ghdl-error", vedit::workspace::lintSeverityForGhdl(QStringLiteral("error")) == 1);
        check("map.ghdl-warning",
              vedit::workspace::lintSeverityForGhdl(QStringLiteral("warning")) == 2);
        check("map.ghdl-note", vedit::workspace::lintSeverityForGhdl(QStringLiteral("note")) == 3);
        check("map.ghdl-unknown-kept-as-zero",
              vedit::workspace::lintSeverityForGhdl(QStringLiteral("bogus")) == 0);
        check("map.vsg-error", vedit::workspace::lintSeverityForVsg(QStringLiteral("Error")) == 1);
        check("map.vsg-warning",
              vedit::workspace::lintSeverityForVsg(QStringLiteral("Warning")) == 2);
        check("map.vsg-info", vedit::workspace::lintSeverityForVsg(QStringLiteral("Info")) == 3);
        check("map.vsg-none-kept-as-zero",
              vedit::workspace::lintSeverityForVsg(QStringLiteral("None")) == 0);
    }
    // --- 9. the dedupe policy (synthetic inputs) ---------------------------
    {
        vedit::workspace::diagnostics::DiagnosticEntry first;
        first.file = QStringLiteral("/p/x.vhd");
        first.line = 3;
        first.severity = 1;
        first.source = QStringLiteral("ghdl");
        first.message = QStringLiteral("boom");
        vedit::workspace::diagnostics::DiagnosticEntry copy = first;
        copy.source = QStringLiteral("vsg");
        vedit::workspace::diagnostics::DiagnosticEntry other = first;
        other.line = 4;
        int deduped = 0;
        const auto merged = vedit::workspace::mergeLintRows({first}, {copy, other}, &deduped);
        check("dedupe.cross-producer-collapses", merged.size() == 2 && deduped == 1,
              QStringLiteral("merged=%1 deduped=%2").arg(merged.size()).arg(deduped));
        check("dedupe.survivor-keeps-its-source",
              !merged.isEmpty() && merged.first().source == QStringLiteral("ghdl"));
        int none = 0;
        const auto kept = vedit::workspace::mergeLintRows({first}, {other}, &none);
        check("dedupe.distinct-rows-are-kept", kept.size() == 2 && none == 0);
        int empty = 0;
        check("dedupe.empty-inputs",
              vedit::workspace::mergeLintRows({}, {}, &empty).isEmpty() && empty == 0);
    }
    // --- 10. a missing tool is reported, never faked -----------------------
    {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("clean.vhd"));
        const auto report = run(path, QString(), vsg);
        check("missing.ghdl-is-named", report.missing.contains(QStringLiteral("ghdl")),
              report.summary());
        check("missing.ghdl-rows-zero", report.ghdlRows == 0);
        check("missing.vsg-still-ran", report.vsgRan && report.vsgRows == 0);
        check("missing.is-not-clean", !report.clean());
    }
    // --- 11. every row is inside the file and ordered ----------------------
    {
        const auto &report = reports.value(QStringLiteral("vsg-only"));
        bool ordered = true;
        for (int index = 1; index < report.rows.size(); ++index) {
            ordered = ordered && report.rows.at(index - 1).line <= report.rows.at(index).line;
        }
        check("rows.sorted-by-line", ordered);
        bool allInFile = true;
        for (const auto &row : report.rows) {
            allInFile = allInFile && QFileInfo(row.file).fileName() == QStringLiteral("vsg-only.vhd");
        }
        check("rows.file-is-the-analysed-one", allInFile);
    }

    std::printf("VHDL-LINT-PROBE checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif // VEDIT_VHDL_LINT_PROBE_MAIN

// ---------------------------------------------------------------------------
// Lane-publish probe (VEDIT_VHDL_LANE_PROBE_MAIN).
//
// What a LANE's rows do once they reach the product's ONE display view. The
// product run proves the rows arrive (DIAG-ROW lines with source=ghdl/vsg); this
// probe proves the SEMANTICS of `setLaneDiagnostics`, which a GUI run cannot
// drive deterministically (no xdotool in the image, and "edit a file so the pass
// stops covering it" is not a keystroke this harness can send):
//
//   1. publish -> the rows appear in `displayDiagnostics()` with their source;
//   2. the same source RE-publishes = REPLACE (never a stack);
//   3. an EMPTY set CLEARS that source;
//   4. a file the UPPER layer covers keeps the pass's verdict (lane rows are
//      suppressed: one file, one truth);
//   5. two sources are independent (each replaces only its own);
//   6. `statusForFile()` / `fileStatuses()` follow the lane rows (the dot).
// ---------------------------------------------------------------------------
#if defined(VEDIT_VHDL_LANE_PROBE_MAIN)

#include "../diagnostics/diagnostics_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("VHDL-CHECK %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

using vedit::workspace::diagnostics::DiagnosticEntry;
using vedit::workspace::diagnostics::DiagnosticOrigin;
using vedit::workspace::diagnostics::DisplayDiagnostic;

DiagnosticEntry row(const QString &file, int line, int severity, const QString &source,
                    const QString &code, const QString &message)
{
    DiagnosticEntry entry;
    entry.file = file;
    entry.line = line;
    entry.column = 2;
    entry.endLine = line;
    entry.endColumn = 4;
    entry.severity = severity;
    entry.source = source;
    entry.code = code;
    entry.message = message;
    return entry;
}

/// A project-pass backend that answers with canned rows, so the UPPER layer
/// becomes live for exactly `files` (the controller's own `replaceUpperLayer`).
class FakePass final : public vedit::workspace::diagnostics::ProjectLintBackend
{
public:
    QStringList files;
    QVector<DiagnosticEntry> rows;

    bool available() const override { return true; }
    QString toolName() const override { return QStringLiteral("fake-pass"); }
    quint64 start(const vedit::workspace::diagnostics::LintPassRequest &request,
                  QString *) override
    {
        files = request.files;
        ++job_;
        return job_;
    }
    vedit::workspace::diagnostics::LintPassSnapshot progress(quint64) const override
    {
        vedit::workspace::diagnostics::LintPassSnapshot snapshot;
        snapshot.phase = vedit::workspace::diagnostics::LintPhase::Finished;
        snapshot.done = static_cast<int>(files.size());
        snapshot.total = static_cast<int>(files.size());
        return snapshot;
    }
    vedit::workspace::diagnostics::LspPolled poll(quint64) override
    {
        vedit::workspace::diagnostics::LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = vedit::workspace::diagnostics::ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        return polled;
    }
    void cancel(quint64) override {}
    void release(quint64) override {}

private:
    quint64 job_ = 0;
};

int countFor(const vedit::workspace::diagnostics::DiagnosticsController &controller,
             const QString &file)
{
    int count = 0;
    for (const DisplayDiagnostic &display : controller.displayDiagnostics()) {
        if (display.entry.file == file) {
            ++count;
        }
    }
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    vedit::workspace::diagnostics::DiagnosticsController controller(nullptr, nullptr);
    const QString vhdl = QStringLiteral("/tmp/probe/coexist.vhd");
    const QString other = QStringLiteral("/tmp/probe/other.vhd");

    // 1. publish -> visible, with the producer labelled per row
    const QVector<DiagnosticEntry> ghdlRows{
        row(vhdl, 16, 1, QStringLiteral("ghdl"), QString(), QStringLiteral("no declaration for \"dout\"")),
    };
    const QVector<DiagnosticEntry> vsgRows{
        row(vhdl, 10, 1, QStringLiteral("vsg"), QStringLiteral("signal_007"),
            QStringLiteral("Remove default assignment.")),
        row(vhdl, 12, 2, QStringLiteral("vsg"), QStringLiteral("process_016"),
            QStringLiteral("Add label for process statement")),
    };
    QVector<DiagnosticEntry> laneRows = ghdlRows;
    laneRows += vsgRows;
    controller.setLaneDiagnostics(QStringLiteral("vhdl"), laneRows);
    check("publish.rows-visible", countFor(controller, vhdl) == 3,
          QStringLiteral("count=%1").arg(countFor(controller, vhdl)));
    {
        bool sawGhdl = false;
        bool sawVsg = false;
        for (const DisplayDiagnostic &display : controller.displayDiagnostics()) {
            sawGhdl = sawGhdl || display.entry.source == QStringLiteral("ghdl");
            sawVsg = sawVsg || display.entry.source == QStringLiteral("vsg");
        }
        check("publish.sources-labelled", sawGhdl && sawVsg);
    }
    // the dot follows
    check("publish.dot-is-error",
          controller.statusForFile(vhdl)
              == vedit::workspace::diagnostics::DiagnosticsController::FileStatus::Error);

    // 2. the same source republishes -> REPLACE (no stacking)
    controller.setLaneDiagnostics(QStringLiteral("vhdl"), ghdlRows);
    check("replace.no-stacking", countFor(controller, vhdl) == 1,
          QStringLiteral("count=%1").arg(countFor(controller, vhdl)));

    // 3. an EMPTY set clears the source
    controller.setLaneDiagnostics(QStringLiteral("vhdl"), {});
    check("clear.empty-set-removes-rows", countFor(controller, vhdl) == 0,
          QStringLiteral("count=%1").arg(countFor(controller, vhdl)));
    check("clear.dot-is-not-error",
          controller.statusForFile(vhdl)
              != vedit::workspace::diagnostics::DiagnosticsController::FileStatus::Error);

    // 4. a file the UPPER layer covers keeps the pass's verdict
    //
    // NOTE (measured, and a real constraint): the project pass REFUSES a `.vhd`
    // file - the language registry gives VHDL no `Lint` capability, because the
    // pass's engines (slang / verible-verilog-lint) cannot lint VHDL. That is
    // correct: the VHDL verdict IS this lane. The suppression rule is about any
    // COVERED file, so this probe exercises it with a `.sv` path - a real pass,
    // a real coverage decision, the same controller rule.
    {
        const QString covered = QStringLiteral("/tmp/probe/covered.sv");
        auto *pass = new FakePass();
        pass->rows.append(row(covered, 5, 0, QStringLiteral("cli:fake"), QString(),
                              QStringLiteral("upper layer says: one error")));
        controller.setProjectLintBackend(std::unique_ptr<FakePass>(pass));
        controller.setOpenFiles(QStringList());
        vedit::workspace::diagnostics::LintPassRequest request;
        request.files = QStringList{covered};
        QString error;
        const bool started = controller.startProjectPass(request, &error);
        QElapsedTimer timer;
        timer.start();
        while (controller.projectPassRunning() && timer.elapsed() < 3000) {
            controller.refresh();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        controller.refresh();
        check("upper.pass-started-and-finished",
              started && !controller.projectPassRunning() && controller.hasUpperLayer(covered),
              error);
        const QVector<DiagnosticEntry> laneForCovered{
            row(covered, 5, 2, QStringLiteral("vsg"), QStringLiteral("process_016"),
                QStringLiteral("Add label for process statement")),
            row(covered, 9, 1, QStringLiteral("ghdl"), QString(),
                QStringLiteral("no declaration for \"zzz\"")),
        };
        controller.setLaneDiagnostics(QStringLiteral("vhdl"), laneForCovered);
        int upperRows = 0;
        int laneRowsShown = 0;
        for (const DisplayDiagnostic &display : controller.displayDiagnostics()) {
            if (display.entry.file != covered) {
                continue;
            }
            if (display.origin == DiagnosticOrigin::Cli) {
                ++upperRows;
            } else {
                ++laneRowsShown;
            }
        }
        check("upper.suppresses-lane-rows", laneRowsShown == 0 && upperRows == 1,
              QStringLiteral("upper=%1 lane=%2").arg(upperRows).arg(laneRowsShown));
        // Suppression is a VIEW rule, not a delete: once the pass no longer
        // covers the file (an edit voids its verdict), the lane's rows are back.
        controller.markFileEdited(covered);
        int laneRowsBack = 0;
        for (const DisplayDiagnostic &display : controller.displayDiagnostics()) {
            if (display.entry.file == covered && display.origin != DiagnosticOrigin::Cli) {
                ++laneRowsBack;
            }
        }
        check("upper.lane-rows-survive-suppression", laneRowsBack == 2,
              QStringLiteral("lane=%1").arg(laneRowsBack));
    }

    // 5. two sources are independent
    {
        // (a different file, so the upper layer above does not interfere)
        const QVector<DiagnosticEntry> vhdlOnly{
            row(other, 3, 2, QStringLiteral("vsg"), QStringLiteral("process_016"),
                QStringLiteral("Add label for process statement")),
        };
        const QVector<DiagnosticEntry> second{
            row(other, 9, 1, QStringLiteral("other-lane"), QStringLiteral("R1"),
                QStringLiteral("second source row")),
        };
        controller.setLaneDiagnostics(QStringLiteral("vhdl"), vhdlOnly);
        controller.setLaneDiagnostics(QStringLiteral("other"), second);
        check("sources.two-sources-coexist", countFor(controller, other) == 2,
              QStringLiteral("count=%1").arg(countFor(controller, other)));
        controller.setLaneDiagnostics(QStringLiteral("vhdl"), vhdlOnly);
        check("sources.replace-is-per-source", countFor(controller, other) == 2);
        controller.setLaneDiagnostics(QStringLiteral("vhdl"), {});
        check("sources.clearing-one-keeps-the-other", countFor(controller, other) == 1,
              QStringLiteral("count=%1").arg(countFor(controller, other)));
        check("sources.panel-and-dot-agree",
              controller.statusForFile(other)
                  == vedit::workspace::diagnostics::DiagnosticsController::FileStatus::Error);
        controller.setLaneDiagnostics(QStringLiteral("other"), {});
        check("sources.all-cleared", countFor(controller, other) == 0);
    }

    // 6. no verdict is NOT a clean verdict
    check("status.no-rows-means-no-verdict",
          controller.statusForFile(vhdl)
              != vedit::workspace::diagnostics::DiagnosticsController::FileStatus::Clean);

    std::printf("VHDL-LANE-PROBE checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif // VEDIT_VHDL_LANE_PROBE_MAIN
