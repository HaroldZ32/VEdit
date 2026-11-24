#pragma once

// VHDL formatting (batch 20261110c, AC-6.6).
//
// VSG is the ONLY formatter for VHDL (`vsg --fix`); `verible-verilog-format`
// is Verilog-only and would reject the file. The path is deliberately the
// temp-copy path the criterion names:
//
//   current BUFFER text
//     -> written to <temp>/<same-name>.vhd        (the user's file is never
//                                                  handed to a tool that rewrites
//                                                  it in place)
//     -> GHDL syntax gate on the temp copy        (a file that does not analyse
//                                                  must never be "formatted")
//     -> `vsg -f <temp> --fix`
//     -> GHDL re-analysis of the FIXED copy       (the success gate: VSG must not
//                                                  turn a legal file into an
//                                                  illegal one)
//     -> diff(buffer, fixed)                      (the APPLY decision is text
//                                                  inequality; the diff is what
//                                                  the evidence log shows)
//     -> caller applies the fixed text to the editor as ONE undoable edit and
//        never saves.
//
// Failure is a STATE, never a silent no-op: a start failure, a syntax error, a
// VSG refusal ("Error while processing"), a crash and a timeout each produce
// `ok == false` with a stage and a sentence, and the caller must leave both the
// document and the file on disk untouched.
//
// Everything is asynchronous (QProcess + signals + a bound): the GUI thread
// never waits for VSG. `waitForFinished()` exists for evidence probes only.

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTemporaryDir;
class QTimer;
class QEventLoop;

namespace vedit::workspace {

/// One file's format outcome.
struct VhdlFormatResult {
    /// The run is trustworthy: every stage completed and the tool accepted the
    /// file. Only an `ok` result may be applied.
    bool ok = false;
    /// The fixed text differs from the buffer text (false when already clean).
    bool changed = false;
    /// The text VSG produced. Valid only when `ok`; empty otherwise.
    QString fixedText;
    /// One sentence for the status bar. Empty when `ok`.
    QString error;
    /// Where it failed: `start` | `io` | `syntax` | `fix` | `verify` | `timeout`.
    QString stage;
    /// Explicit statements about the run (the tool refused, a leg was skipped).
    QStringList notes;
    /// Unified diff (one hunk over the changed middle) of buffer -> fixedText.
    QString diff;
    /// Removed + added lines, for the log.
    int changedLines = 0;
    /// The temp copy that was fixed (the directory vanishes with the job).
    QString tempPath;

    /// `stage=fix ok=0 changed=0 lines=0 notes=1` — one machine line.
    QString summary() const;
};

/// ONE unified diff hunk over the changed middle of `original` -> `fixed`.
///
/// The apply decision is the text inequality, never this string: the diff is
/// for the evidence log and for a human reading why the buffer changed. Common
/// prefix/suffix lines are trimmed; the remaining middle is emitted as one
/// `@@` hunk with `-`/`+` lines. An unchanged input yields an empty string.
QString unifiedDiff(const QString &original, const QString &fixed, int *changedLines = nullptr);

/// One asynchronous `vsg --fix` run over ONE buffer snapshot.
///
/// Lifecycle: `start()` -> `finished(result)` exactly once (also for a missing
/// tool, a refused file or a timeout). The object owns its temp directory and
/// its child process; `stop()`/the destructor kills them.
class VhdlFormatJob : public QObject
{
    Q_OBJECT

public:
    explicit VhdlFormatJob(QObject *parent = nullptr);
    ~VhdlFormatJob() override;

    /// Format `bufferText` — the CURRENT editor buffer, never the file on disk.
    ///
    /// `filePath` only names the temp copy (its suffix selects VHDL) and is
    /// never opened for writing. `vsgProgram` is required; `ghdlProgram` is the
    /// syntax/verification gate and may be empty (the run then records that the
    /// file was not pre-validated, and only VSG's own refusal can fail it).
    bool start(const QString &bufferText, const QString &filePath, const QString &vsgProgram,
               const QString &ghdlProgram, int timeoutMs = 30000);

    /// Bounded, event-loop driven wait — for evidence probes only (the product
    /// never waits; it reacts to `finished`).
    bool waitForFinished(int timeoutMs = 60000);

    bool running() const { return running_; }

signals:
    void finished(const vedit::workspace::VhdlFormatResult &result);

private:
    void startGhdlCheck(bool afterFix);
    void ghdlCheckFinished();
    void startVsg();
    void vsgFinished();
    void finishWithError(const QString &stage, const QString &error);
    void finish();
    void killAll();

    QProcess *process_ = nullptr;
    QTimer *timer_ = nullptr;
    QTemporaryDir *work_ = nullptr;
    QString sourceFile_;
    QString tempPath_;
    QString bufferText_;
    QString vsgProgram_;
    QString ghdlProgram_;
    QString currentTool_;
    int timeoutMs_ = 30000;
    bool running_ = false;
    bool emitted_ = false;
    bool afterFixCheck_ = false;
    QEventLoop *waitLoop_ = nullptr;
    QByteArray ghdlOutput_;
    VhdlFormatResult result_;
};

} // namespace vedit::workspace

Q_DECLARE_METATYPE(vedit::workspace::VhdlFormatResult)
