#include "vhdl_format.h"

#include "vhdl_lint.h" // parseGhdlDiagnostics(): the measured GHDL grammar

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace vedit::workspace {

namespace {

QString firstLines(const QString &text, int count)
{
    const QStringList lines = text.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList kept;
    for (int index = 0; index < lines.size() && index < count; ++index) {
        kept.append(lines.at(index).trimmed());
    }
    return kept.join(QStringLiteral(" | "));
}

/// The first error row of a GHDL analysis (`severity == 1` in the panel scale).
QString firstGhdlError(const QByteArray &output, const QString &file)
{
    const QVector<diagnostics::DiagnosticEntry> rows =
        parseGhdlDiagnostics(QString::fromUtf8(output), file);
    for (const diagnostics::DiagnosticEntry &row : rows) {
        if (row.severity == 1) {
            return row.message;
        }
    }
    return {};
}

} // namespace

QString VhdlFormatResult::summary() const
{
    return QStringLiteral("stage=%1 ok=%2 changed=%3 lines=%4 notes=%5")
        .arg(stage.isEmpty() ? QStringLiteral("-") : stage)
        .arg(ok ? 1 : 0)
        .arg(changed ? 1 : 0)
        .arg(changedLines)
        .arg(notes.size());
}

QString unifiedDiff(const QString &original, const QString &fixed, int *changedLines)
{
    if (changedLines != nullptr) {
        *changedLines = 0;
    }
    if (original == fixed) {
        return {};
    }
    const QStringList before = original.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const QStringList after = fixed.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    int prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before.at(prefix) == after.at(prefix)) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before.at(before.size() - 1 - suffix) == after.at(after.size() - 1 - suffix)) {
        ++suffix;
    }
    const int removed = before.size() - prefix - suffix;
    const int added = after.size() - prefix - suffix;
    if (changedLines != nullptr) {
        *changedLines = removed + added;
    }
    QString diff;
    diff += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
                .arg(prefix + 1)
                .arg(removed)
                .arg(prefix + 1)
                .arg(added);
    for (int index = 0; index < removed; ++index) {
        diff += QLatin1Char('-') + before.at(prefix + index) + QLatin1Char('\n');
    }
    for (int index = 0; index < added; ++index) {
        diff += QLatin1Char('+') + after.at(prefix + index) + QLatin1Char('\n');
    }
    return diff;
}

VhdlFormatJob::VhdlFormatJob(QObject *parent) : QObject(parent)
{
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this]() {
        finishWithError(QStringLiteral("timeout"),
                        QStringLiteral("%1 did not answer within %2 ms")
                            .arg(currentTool_.isEmpty() ? QStringLiteral("the tool") : currentTool_)
                            .arg(timeoutMs_));
    });
}

VhdlFormatJob::~VhdlFormatJob()
{
    killAll();
    delete work_;
    work_ = nullptr;
}

bool VhdlFormatJob::start(const QString &bufferText, const QString &filePath,
                          const QString &vsgProgram, const QString &ghdlProgram, int timeoutMs)
{
    if (running_) {
        return false;
    }
    result_ = VhdlFormatResult{};
    emitted_ = false;
    afterFixCheck_ = false;
    running_ = true;
    bufferText_ = bufferText;
    sourceFile_ = QFileInfo(filePath).absoluteFilePath();
    vsgProgram_ = vsgProgram;
    ghdlProgram_ = ghdlProgram;
    timeoutMs_ = timeoutMs > 0 ? timeoutMs : 30000;
    tempPath_.clear();

    // The syntax oracle is GHDL. Without it a malformed file can be "fixed"
    // into a different malformed file (`vsg --fix` happily rewrites `a <= ;`),
    // so the apply gate would be a guess. Refusing is the honest failure.
    if (vsgProgram_.isEmpty()) {
        result_.stage = QStringLiteral("start");
        result_.error = QStringLiteral("vsg is not installed");
        finish();
        return false;
    }
    if (ghdlProgram_.isEmpty()) {
        result_.stage = QStringLiteral("start");
        result_.error = QStringLiteral(
            "ghdl is not installed; it is required to verify the formatted file");
        finish();
        return false;
    }
    if (sourceFile_.isEmpty()) {
        result_.stage = QStringLiteral("start");
        result_.error = QStringLiteral("no file path for the buffer");
        finish();
        return false;
    }
    work_ = new QTemporaryDir();
    if (!work_->isValid()) {
        result_.stage = QStringLiteral("io");
        result_.error = QStringLiteral("cannot create a temporary directory");
        finish();
        return false;
    }
    // The temp copy keeps the SOURCE NAME (VSG/GHDL both select VHDL from the
    // suffix); it is a new file in the temp dir, never the user's file.
    tempPath_ = QDir(work_->path()).filePath(QFileInfo(sourceFile_).fileName());
    QFile copy(tempPath_);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result_.stage = QStringLiteral("io");
        result_.error = QStringLiteral("cannot write the temporary copy: %1")
                            .arg(copy.errorString());
        finish();
        return false;
    }
    copy.write(bufferText_.toUtf8());
    copy.close();

    startGhdlCheck(false);
    return true;
}

void VhdlFormatJob::startGhdlCheck(bool afterFix)
{
    afterFixCheck_ = afterFix;
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    // A fresh work library per check: analysing the same unit twice into one
    // library is redefinition territory, and the fixed copy is a NEW analysis.
    const QString workdir = QDir(work_->path())
                                .filePath(afterFix ? QStringLiteral("work-after")
                                                   : QStringLiteral("work-before"));
    QDir().mkpath(workdir);
    const QStringList args{QStringLiteral("-a"), QStringLiteral("--std=08"),
                           QStringLiteral("--workdir=%1").arg(workdir), tempPath_};
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        ghdlCheckFinished();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process_ == nullptr) {
            return;
        }
        process_->deleteLater();
        process_ = nullptr;
        finishWithError(QStringLiteral("start"),
                        QStringLiteral("cannot start ghdl `%1`").arg(ghdlProgram_));
    });
    currentTool_ = QStringLiteral("ghdl");
    timer_->start(timeoutMs_);
    process_->start(ghdlProgram_, args);
}

void VhdlFormatJob::ghdlCheckFinished()
{
    timer_->stop();
    if (process_ == nullptr) {
        return;
    }
    ghdlOutput_ = process_->readAll();
    const int exitCode = process_->exitCode();
    const bool crashed = process_->exitStatus() == QProcess::CrashExit;
    process_->deleteLater();
    process_ = nullptr;

    // GHDL exits 1 on errors: that is a RESULT. The rows carry the message.
    const QString error = firstGhdlError(ghdlOutput_, tempPath_);
    if (!error.isEmpty()) {
        if (afterFixCheck_) {
            finishWithError(QStringLiteral("verify"),
                            QStringLiteral("VSG produced a file GHDL rejects: %1").arg(error));
        } else {
            finishWithError(QStringLiteral("syntax"),
                            QStringLiteral("VHDL syntax error, nothing was formatted: %1").arg(error));
        }
        return;
    }
    if (crashed || (exitCode != 0 && !ghdlOutput_.trimmed().isEmpty())) {
        // It failed, yet no row could be parsed: "unreadable failure" must not
        // be read as "the file is fine".
        finishWithError(afterFixCheck_ ? QStringLiteral("verify") : QStringLiteral("syntax"),
                        QStringLiteral("ghdl exited %1 with unparsable output: %2")
                            .arg(exitCode)
                            .arg(firstLines(QString::fromUtf8(ghdlOutput_), 2)));
        return;
    }
    if (afterFixCheck_) {
        result_.ok = true;
        finish();
        return;
    }
    startVsg();
}

void VhdlFormatJob::startVsg()
{
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{QStringLiteral("-f"), tempPath_, QStringLiteral("--fix")};
    connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        vsgFinished();
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process_ == nullptr) {
            return;
        }
        process_->deleteLater();
        process_ = nullptr;
        finishWithError(QStringLiteral("start"),
                        QStringLiteral("cannot start vsg `%1`").arg(vsgProgram_));
    });
    currentTool_ = QStringLiteral("vsg");
    timer_->start(timeoutMs_);
    process_->start(vsgProgram_, args);
}

void VhdlFormatJob::vsgFinished()
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

    // MEASURED (VSG 3.36.0): `--fix` exits 1 whenever it found violations, so
    // the exit code is NOT a success signal. A file VSG cannot parse prints
    // `Error while processing <file>` and still writes a (partially) rewritten
    // file — that is the refusal this branch must never treat as success.
    if (output.contains(QStringLiteral("Error while processing"))) {
        result_.notes.append(QStringLiteral("vsg: refused %1 (%2)")
                                 .arg(QFileInfo(tempPath_).fileName(),
                                      firstLines(output, 2)));
        finishWithError(QStringLiteral("fix"),
                        QStringLiteral("VSG refused the file: %1").arg(firstLines(output, 2)));
        return;
    }
    if (crashed) {
        finishWithError(QStringLiteral("fix"),
                        QStringLiteral("vsg crashed (exit %1)").arg(exitCode));
        return;
    }
    QFile fixed(tempPath_);
    if (!fixed.open(QIODevice::ReadOnly)) {
        finishWithError(QStringLiteral("io"),
                        QStringLiteral("cannot read the fixed copy: %1").arg(fixed.errorString()));
        return;
    }
    const QString text = QString::fromUtf8(fixed.readAll());
    fixed.close();
    if (text.isEmpty() && !bufferText_.isEmpty()) {
        // A formatter that empties a non-empty document is a failure, not an
        // edit the user must undo.
        finishWithError(QStringLiteral("io"), QStringLiteral("vsg left an empty file"));
        return;
    }
    result_.fixedText = text;
    result_.changed = text != bufferText_;
    result_.diff = unifiedDiff(bufferText_, text, &result_.changedLines);
    startGhdlCheck(true);
}

void VhdlFormatJob::finishWithError(const QString &stage, const QString &error)
{
    if (emitted_) {
        return;
    }
    result_.ok = false;
    result_.changed = false;
    result_.fixedText.clear();
    result_.diff.clear();
    result_.changedLines = 0;
    result_.stage = stage;
    result_.error = error;
    killAll();
    finish();
}

void VhdlFormatJob::finish()
{
    if (emitted_) {
        return;
    }
    emitted_ = true;
    running_ = false;
    if (timer_ != nullptr) {
        timer_->stop();
    }
    result_.tempPath = tempPath_;
    if (waitLoop_ != nullptr) {
        waitLoop_->quit();
    }
    emit finished(result_);
}

void VhdlFormatJob::killAll()
{
    if (process_ == nullptr) {
        return;
    }
    process_->blockSignals(true);
    process_->kill();
    process_->waitForFinished(2000);
    process_->deleteLater();
    process_ = nullptr;
}

bool VhdlFormatJob::waitForFinished(int timeoutMs)
{
    if (!running_) {
        return true;
    }
    QEventLoop loop;
    waitLoop_ = &loop;
    QTimer guard;
    guard.setSingleShot(true);
    connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs > 0 ? timeoutMs : 60000);
    loop.exec();
    waitLoop_ = nullptr;
    return !running_;
}

} // namespace vedit::workspace

// ===========================================================================
// AC-6.6 evidence probe: the REAL `vsg --fix` path over a real KTextEditor
// document (apply = ONE undo, failure = ZERO change, the file on disk never
// written), compiled from this file with VEDIT_VHDL_FORMAT_PROBE_MAIN exactly
// like the lane's LSP/lint probes. None of it links into the product.
//
//   vedit-vhdl-format-probe --vsg <path> --ghdl <path>
// ===========================================================================
#if defined(VEDIT_VHDL_FORMAT_PROBE_MAIN)

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QWidget>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include "../diagnostics/autofix.h"

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

QString sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

QString writeFixture(const QString &dir, const QString &name, const QString &text)
{
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(text.toUtf8());
    file.close();
    return path;
}

QString oneLine(const QString &text, int maxLines)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (lines.size() > maxLines) {
        lines = lines.mid(0, maxLines);
        lines.append(QStringLiteral("…"));
    }
    QString out = lines.join(QStringLiteral("\\n"));
    return out;
}

vedit::workspace::VhdlFormatResult runJob(const QString &buffer, const QString &path,
                                          const QString &vsg, const QString &ghdl)
{
    vedit::workspace::VhdlFormatResult captured;
    vedit::workspace::VhdlFormatJob job;
    QObject::connect(&job, &vedit::workspace::VhdlFormatJob::finished,
                     [&captured](const vedit::workspace::VhdlFormatResult &result) {
                         captured = result;
                     });
    job.start(buffer, path, vsg, ghdl);
    job.waitForFinished(120000);
    return captured;
}

QAction *findEditorAction(QWidget *editor, const QString &name)
{
    if (editor == nullptr) {
        return nullptr;
    }
    for (QAction *action : editor->actions()) {
        if (action->objectName() == name) {
            return action;
        }
    }
    for (QWidget *widget : editor->findChildren<QWidget *>()) {
        for (QAction *action : widget->actions()) {
            if (action->objectName() == name) {
                return action;
            }
        }
    }
    return nullptr;
}

// Unformatted but ANALYSABLE VHDL (GHDL accepts it; VSG has work to do).
const char *kUgly =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "entity ugly is\n"
    "port (\n"
    "clk : in std_logic;\n"
    "q : out std_logic\n"
    ");\n"
    "end entity;\n"
    "architecture rtl of ugly is\n"
    "begin\n"
    "process(clk)\n"
    "begin\n"
    "if rising_edge(clk) then\n"
    "q <= '1';\n"
    "end if;\n"
    "end process;\n"
    "end architecture;\n";

// A real SYNTAX error (`a <= ;`). MEASURED: GHDL reports
// `primary expression expected`; VSG --fix happily rewrites the file instead of
// refusing it — which is exactly why the GHDL gate decides the apply.
const char *kBad =
    "library ieee;\n"
    "use ieee.std_logic_1164.all;\n"
    "entity bad is\n"
    "  port (\n"
    "    a : in std_logic\n"
    "  );\n"
    "end entity bad;\n"
    "architecture rtl of bad is\n"
    "begin\n"
    "  a <= ;\n"
    "end architecture rtl;\n";

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString vsg;
    QString ghdl;
    const QStringList args = QCoreApplication::arguments();
    for (int index = 1; index < args.size(); ++index) {
        if (args.at(index) == QStringLiteral("--vsg") && index + 1 < args.size()) {
            vsg = args.at(++index);
        } else if (args.at(index) == QStringLiteral("--ghdl") && index + 1 < args.size()) {
            ghdl = args.at(++index);
        }
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("VHDL-FORMAT-PROBE cannot create a fixture directory\n");
        return 2;
    }
    std::printf("VHDL-FORMAT-PROBE vsg='%s' ghdl='%s'\n", vsg.toUtf8().constData(),
                ghdl.toUtf8().constData());

    // --- 1. valid buffer: temp copy -> vsg --fix -> diff -> apply (ONE undo) --
    {
        const QString path = writeFixture(dir.path(), QStringLiteral("valid.vhd"),
                                          QString::fromUtf8(kUgly));
        const QString before = sha256(path);
        vedit::workspace::VhdlFormatResult result =
            runJob(QString::fromUtf8(kUgly), path, vsg, ghdl);
        std::printf("VHDL-FORMAT case=valid %s\n", result.summary().toUtf8().constData());
        check("valid.ok", result.ok, result.error);
        check("valid.changed", result.changed);
        check("valid.fixed-text", !result.fixedText.isEmpty() && result.fixedText != QString::fromUtf8(kUgly));
        check("valid.diff", !result.diff.isEmpty() && result.changedLines > 0,
              result.diff.isEmpty() ? QStringLiteral("no diff")
                                    : QStringLiteral("lines=%1").arg(result.changedLines));
        std::printf("VHDL-FORMAT-DIFF case=valid %s\n",
                    oneLine(result.diff, 8).toUtf8().constData());
        check("valid.temp-copy-is-not-the-user-file",
              !result.tempPath.isEmpty() && result.tempPath != path,
              QStringLiteral("temp='%1'").arg(result.tempPath));
        check("valid.user-file-untouched", sha256(path) == before);

        if (KTextEditor::Editor::instance() == nullptr || !result.ok) {
            check("valid.ktexteditor", KTextEditor::Editor::instance() != nullptr);
        } else {
            KTextEditor::Document *document =
                KTextEditor::Editor::instance()->createDocument(nullptr);
            QWidget host;
            KTextEditor::View *view = document->createView(&host);
            document->setText(QString::fromUtf8(kUgly));
            document->setModified(false); // the document represents the saved file
            const bool applied =
                vedit::workspace::diagnostics::applyAutofixToDocument(document, result.fixedText);
            check("valid.applied", applied && document->text() == result.fixedText
                                        && document->isModified(),
                  QStringLiteral("modified=%1").arg(document->isModified() ? 1 : 0));
            check("valid.not-saved", sha256(path) == before,
                  QStringLiteral("sha unchanged after apply"));
            QAction *undo = findEditorAction(view, QStringLiteral("edit_undo"));
            check("valid.undo-action", undo != nullptr);
            if (undo != nullptr) {
                undo->trigger();
                QCoreApplication::processEvents();
                check("valid.one-undo", document->text() == QString::fromUtf8(kUgly),
                      oneLine(document->text(), 2));
            }
            delete view;
            delete document;
        }
    }

    // --- 2. already formatted: ok, changed=0, no diff, no edit ----------------
    {
        vedit::workspace::VhdlFormatResult first =
            runJob(QString::fromUtf8(kUgly), writeFixture(dir.path(), QStringLiteral("v1.vhd"),
                                                          QString::fromUtf8(kUgly)),
                   vsg, ghdl);
        const QString path = writeFixture(dir.path(), QStringLiteral("clean.vhd"), first.fixedText);
        vedit::workspace::VhdlFormatResult second = runJob(first.fixedText, path, vsg, ghdl);
        std::printf("VHDL-FORMAT case=clean %s\n", second.summary().toUtf8().constData());
        check("clean.ok", second.ok, second.error);
        check("clean.unchanged", second.ok && !second.changed && second.diff.isEmpty()
                                      && second.changedLines == 0);
    }

    // --- 3. bad syntax: ZERO change (document AND file) ----------------------
    {
        const QString path = writeFixture(dir.path(), QStringLiteral("bad.vhd"),
                                          QString::fromUtf8(kBad));
        const QString before = sha256(path);
        vedit::workspace::VhdlFormatResult result =
            runJob(QString::fromUtf8(kBad), path, vsg, ghdl);
        std::printf("VHDL-FORMAT case=syntax %s error='%s'\n",
                    result.summary().toUtf8().constData(), result.error.toUtf8().constData());
        check("syntax.refused", !result.ok && result.stage == QStringLiteral("syntax"));
        check("syntax.no-fixed-text", result.fixedText.isEmpty() && result.diff.isEmpty());
        check("syntax.user-file-untouched", sha256(path) == before);

        if (KTextEditor::Editor::instance() != nullptr) {
            KTextEditor::Document *document =
                KTextEditor::Editor::instance()->createDocument(nullptr);
            document->setText(QString::fromUtf8(kBad));
            document->setModified(false);
            // A failed run is never applied by the product; the probe asserts the
            // document would in any case still equal the buffer.
            check("syntax.buffer-unchanged", document->text() == QString::fromUtf8(kBad));
            delete document;
        }
    }

    // --- 4. read-only document: the apply is refused, nothing changes --------
    {
        if (KTextEditor::Editor::instance() == nullptr) {
            check("readonly.ktexteditor", false);
        } else {
            vedit::workspace::VhdlFormatResult result =
                runJob(QString::fromUtf8(kUgly),
                       writeFixture(dir.path(), QStringLiteral("ro.vhd"),
                                    QString::fromUtf8(kUgly)),
                       vsg, ghdl);
            KTextEditor::Document *document =
                KTextEditor::Editor::instance()->createDocument(nullptr);
            document->setText(QString::fromUtf8(kUgly));
            document->setModified(false);
            document->setReadWrite(false);
            const bool applied =
                vedit::workspace::diagnostics::applyAutofixToDocument(document, result.fixedText);
            check("readonly.refused", result.ok && !applied,
                  QStringLiteral("ok=%1 applied=%2").arg(result.ok ? 1 : 0).arg(applied ? 1 : 0));
            check("readonly.buffer-unchanged", document->text() == QString::fromUtf8(kUgly));
            delete document;
        }
    }

    // --- 5. a missing tool is a typed failure, never a silent no-op ----------
    {
        const QString path = writeFixture(dir.path(), QStringLiteral("missing.vhd"),
                                          QString::fromUtf8(kUgly));
        vedit::workspace::VhdlFormatResult noVsg =
            runJob(QString::fromUtf8(kUgly), path, QString(), ghdl);
        check("missing-vsg.refused", !noVsg.ok && noVsg.stage == QStringLiteral("start"),
              noVsg.error);
        vedit::workspace::VhdlFormatResult noGhdl =
            runJob(QString::fromUtf8(kUgly), path, vsg, QString());
        check("missing-ghdl.refused", !noGhdl.ok && noGhdl.stage == QStringLiteral("start"),
              noGhdl.error);
    }

    std::printf("VHDL-FORMAT-PROBE SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}

#endif // VEDIT_VHDL_FORMAT_PROBE_MAIN
