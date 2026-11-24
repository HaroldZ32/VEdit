#pragma once

// VHDL merge lint (batch 20261110c, AC-6.4).
//
// GHDL answers "does this file analyse?" and VSG answers "does it follow the
// style guide?". They are two different tools with two different vocabularies,
// yet the panel has ONE row list, so this file owns the merge:
//
//   * the SEVERITY MAP is one documented function per producer (LSP scale, the
//     panel's own scale) — never an implicit cast;
//   * the DEDUPE KEY is the diagnostics lane's own merge key, so the panel and
//     the merge cannot disagree about what "the same finding" is, and every
//     removed row is COUNTED (`dedupedRows`), never silently dropped;
//   * an UNRECOGNISED severity becomes 0 (unknown) and the row is KEPT — an
//     unknown vocabulary must not delete a finding;
//   * a tool that ran but produced nothing machine-readable is a NOTE, not a
//     silent zero: "no output" and "clean" must never look the same.
//
// The tools are measured, not assumed (GHDL 6.0.0, VSG 3.36.0, the pinned
// AppDir): `ghdl -a` prints `<path>:<line>:<col>:<level>: <message>` on stderr
// followed by the echoed source line and a caret; VSG's `-js <file>` writes
// `{"files":[{"file_path":…,"violations":[{"rule","linenumber","severity",
// "solution"}]}]}` and has NO column. `--output_format json` does not exist in
// VSG 3.36.0 (only vsg/syntastic/summary), which is why the machine-readable
// path is `-js`.
//
// Everything here is asynchronous (QProcess + signals + a bound): the GUI thread
// never waits for a compiler.

#include "../diagnostics/diagnostics_types.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QTemporaryDir;
class QTimer;

namespace vedit::workspace {

/// AC-6.4: the ONE severity vocabulary of this lane, spelled out per producer.
///
/// The panel's scale is the LSP one (`diagnostics_types.h`: 1 error, 2 warning,
/// 3 information, 4 hint, 0 unknown). The mapping is a total function: every
/// input yields a value, and an unrecognised input yields 0 with the row KEPT.
int lintSeverityForGhdl(const QString &level);
int lintSeverityForVsg(const QString &severity);

/// One file's merged result.
struct VhdlLintReport {
    /// Merged rows, sorted by (file, line, column, severity, source).
    QVector<diagnostics::DiagnosticEntry> rows;
    int ghdlRows = 0;   ///< rows GHDL produced (before the merge)
    int vsgRows = 0;    ///< rows VSG produced (before the merge)
    int dedupedRows = 0;///< rows the merge removed; always counted, never silent
    bool ghdlRan = false;
    bool vsgRan = false;
    /// Tools that were not installed. A missing tool is reported here — the
    /// lane never pretends a missing tool found nothing.
    QStringList missing;
    /// Explicit statements about the RUN (unparsable output, timeout, refused
    /// analysis). An empty `notes` means "every tool answered and was parsed".
    QStringList notes;
    /// `ghdl=1 vsg=1 merged=2 deduped=0 missing=[] notes=0` — one machine line.
    QString summary() const;
    bool clean() const { return rows.isEmpty() && notes.isEmpty() && missing.isEmpty(); }
};

/// Parse GHDL's stderr.
///
/// Grammar (measured): `<path>:<line>:<col>:<error|warning|note>: <message>`,
/// with the echoed source line and a `^` caret line following it. Only the
/// diagnostic lines are rows; everything else is skipped, and a caret/echo line
/// can never be mistaken for a diagnostic because it has no `<line>:<col>:`
/// prefix. GHDL stops after a SYNTAX error (measured) but reports every
/// SEMANTIC error, so multi-row answers happen and each line keeps its own
/// number.
QVector<diagnostics::DiagnosticEntry> parseGhdlDiagnostics(const QString &text,
                                                           const QString &file);

/// Parse VSG's `-js` document. `notes` receives one explicit line per problem
/// (no document, unparsable JSON, an entry without a line number) so a
/// machine-unreadable run can never be read as "clean".
QVector<diagnostics::DiagnosticEntry> parseVsgDiagnostics(const QByteArray &json,
                                                          const QString &file,
                                                          QStringList *notes);

/// Merge the two producers' rows.
///
/// Identity = `diagnostics::diagnosticMergeKey(row, DiagnosticOrigin::Lsp)`,
/// i.e. the SAME key the panel uses (absolute path, 0-based line, normalized
/// severity, whitespace-normalized message). A duplicate is removed and counted
/// in `*deduped`; the surviving row keeps its own `source`. A cross-producer
/// duplicate therefore collapses to ONE row — "one row per distinct finding",
/// exactly the panel's rule — and the count is reported instead of hidden.
QVector<diagnostics::DiagnosticEntry> mergeLintRows(
    const QVector<diagnostics::DiagnosticEntry> &ghdl,
    const QVector<diagnostics::DiagnosticEntry> &vsg, int *deduped);

/// One asynchronous GHDL+VSG run over exactly one file.
///
/// Lifecycle: `start()` -> `finished(report)` exactly once (also on a tool that
/// failed, timed out or was missing). The object owns its temporary work library
/// and its child processes; `stop()` kills them.
class VhdlLintJob : public QObject
{
    Q_OBJECT

public:
    explicit VhdlLintJob(QObject *parent = nullptr);
    ~VhdlLintJob() override;

    /// Run GHDL, then VSG, over `file`.
    ///
    /// `ghdlProgram` / `vsgProgram` are the resolved binary paths; an empty
    /// program is recorded in `report.missing` and its leg is skipped (never
    /// faked). `-Wunused` is the ONE extra GHDL warning flag the lane passes
    /// (measured: `procedure "x" is never referenced [-Wunused]`); everything
    /// else is GHDL's own default warning set. GHDL writes its work library into
    /// a temporary directory, so the project tree is never polluted (and a shared
    /// work library would report a spurious `-Wlibrary` redefinition).
    bool start(const QString &file, const QString &ghdlProgram, const QString &vsgProgram,
               int timeoutMs = 20000);

    /// Bounded, event-loop driven wait — for the evidence probe only (the product
    /// never waits; it reacts to `finished`).
    bool waitForFinished(int timeoutMs = 30000);

    bool running() const { return running_; }

signals:
    void finished(const vedit::workspace::VhdlLintReport &report);

private:
    void startGhdl();
    void ghdlFinished();
    void startVsg();
    void vsgFinished();
    void finish();
    void killAll();

    QProcess *process_ = nullptr;
    QTimer *timer_ = nullptr;
    QTemporaryDir *work_ = nullptr;
    QString file_;
    QString ghdlProgram_;
    QString vsgProgram_;
    QString vsgJsonPath_;
    QString currentTool_;
    int timeoutMs_ = 20000;
    bool running_ = false;
    bool finishedEmitted_ = false;
    VhdlLintReport report_;
    QByteArray ghdlOutput_;
    QVector<diagnostics::DiagnosticEntry> pendingGhdl_;
    QVector<diagnostics::DiagnosticEntry> mergedFromVsg_;
};

} // namespace vedit::workspace

Q_DECLARE_METATYPE(vedit::workspace::VhdlLintReport)
