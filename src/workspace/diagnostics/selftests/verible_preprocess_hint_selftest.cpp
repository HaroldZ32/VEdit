// AC-3 (batch-20261117) — the Verible preprocessing-error hint in the Outline.
//
// The user's 8051 project: the pinned verible-verilog-ls cannot expand the macros
// that arrive through `include` / `+define+`, so deleting a semicolon ANYWHERE
// produced one `preprocessing error at "`MACRO" ... might not be defined before.`
// row anchored at the first macro it could not expand - hiding the real syntax
// error. The product cannot fix Verible, but the Outline can say what happened
// and how to see the real diagnostic (switch the LSP backend to slang).
//
// Fakes only, offscreen, no Rust service and no language server: a tiny LSP
// session answers diagnostics for one file and reports the backend name the
// snapshot carries; the panel under test is the REAL widget. Asserted:
//   * the pure function: the frozen sentence for verible + that row, and an
//     empty answer for another backend, ordinary rows, half a marker, no rows;
//   * the panel: one hint line with verible + that row, hidden with slang,
//     hidden with ordinary rows - and the hint never becomes a diagnostic row
//     (the issue count and the error count do not move).

#include "diagnostics_controller.h"
#include "diagnostics_hint.h"
#include "diagnostics_panel.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdio>
#include <memory>

using namespace vedit::workspace::diagnostics;

namespace {

// The frozen AC-3 sentence (the contract's exact wording; the test holds its own
// copy so a source edit cannot silently change what the user reads).
constexpr char kFrozenHint[] =
    "Verible stopped at a macro preprocessing error — the real syntax error (for example a "
    "missing semicolon) is hidden. Switch the LSP backend to slang in Preferences → Tools to "
    "see it.";

// Verible's real row (pinned v0.0-4298-g085eef05); both fragments are its own.
constexpr char kVeriblePreprocessRow[] =
    "preprocessing error at \"`OC8051_WRS_ACC1\" Error expanding macro identifier, might not "
    "be defined before.";

int g_checks = 0;
int g_failures = 0;

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

void section(const char *name)
{
    std::printf("\nSECTION %s\n", name);
    std::fflush(stdout);
}

DiagnosticEntry diagnostic(const QString &message)
{
    DiagnosticEntry entry;
    entry.line = 97;
    entry.column = 28;
    entry.endLine = 97;
    entry.endColumn = 45;
    entry.severity = 1;
    entry.source = QStringLiteral("verible");
    entry.message = message;
    return entry;
}

/// A session that answers one fixed diagnostics set and reports `backend` in its
/// snapshot - exactly the two facts the hint keys on. No process, no timing.
///
/// It models the REAL publish order (batch-20261117 F1): a didChange does not
/// move the diagnostics generation by itself - the server publishes later. So
/// changeBuffer() only MARKS the publish and the first diagnostics poll after it
/// advances `generation_`; status() reports that bumped counter from then on.
/// Without this a fake with a constant generation can never clear the merged T2
/// gate (`session_.generation > bufferGenerationAtApply_`), and the panel sees
/// no rows at all.
class TinyFakeLsp final : public LspSessionBackend {
public:
    QVector<DiagnosticEntry> rows;
    QString backend = QStringLiteral("verible");

    bool start(const QString &, const QString &, const QString &, const QStringList &files,
               QString *) override
    {
        startedFiles_ = files;
        generation_ = 1;
        publishPending_ = false;
        return true;
    }
    void stop() override { startedFiles_.clear(); }
    bool changeBuffer(const QString &, const QString &, QString *) override
    {
        // didChange only MARKS the publish: the floor the controller records
        // right after this call must still be the OLD generation, or the gate
        // could never be cleared by a later publish.
        publishPending_ = true;
        return true;
    }
    SessionSnapshot status() const override
    {
        SessionSnapshot snapshot;
        snapshot.state = BackendState::Ready;
        snapshot.filesExpected = startedFiles_.size();
        snapshot.filesOpened = startedFiles_.size();
        snapshot.generation = generation_;
        snapshot.backendName = backend;
        return snapshot;
    }
    quint64 requestDefinition(const QString &, int, int) override { return 0; }
    quint64 requestReferences(const QString &, int, int) override { return 0; }
    quint64 requestSymbols(const QString &) override { return 0; }
    quint64 requestRename(const QString &, int, int, const QString &) override { return 0; }
    quint64 requestFormat(const QString &) override { return 0; }
    quint64 requestDiagnostics(const QString &) override { return ++lastRequest_; }
    LspPolled poll(quint64 requestId) override
    {
        if (requestId == 0) {
            return LspPolled::failure(QStringLiteral("unknown fake request id"));
        }
        // The pending publish lands HERE: this poll answers the NEW set and moves
        // the session generation; the controller's next refresh reads the moved
        // counter in status() and accepts the poll. A fake that never moves the
        // counter models a server that never publishes.
        if (publishPending_) {
            publishPending_ = false;
            ++generation_;
        }
        LspPolled polled;
        polled.pending = false;
        polled.ready = true;
        polled.kind = ResponseKind::Diagnostics;
        polled.diagnostics = rows;
        return polled;
    }
    BackendCapabilities staticCapabilities() const override { return BackendCapabilities{}; }
    QString backendName() const override { return backend; }

private:
    QStringList startedFiles_;
    quint64 generation_ = 1;
    bool publishPending_ = false;
    quint64 lastRequest_ = 0;
};

/// No lint leg: this binary proves the LSP-derived hint, not the CLI pass.
class TinyFakeSlang final : public SlangBackend {
public:
    bool available() const override { return false; }
    QString description() const override { return QStringLiteral("unavailable fake slang"); }
    int startLint(const QString &, QString *error) override
    {
        if (error != nullptr) {
            *error = QStringLiteral("fake slang is unavailable");
        }
        return 0;
    }
    LspPolled pollLint(int) override { return LspPolled::failure(QStringLiteral("no lint")); }
};

/// Pump the controller's own poll until the session settles (the panel never
/// blocks on it either).
void settle(DiagnosticsController &controller)
{
    QElapsedTimer timer;
    timer.start();
    while (controller.state() == BackendState::Reparsing && timer.elapsed() < 3000) {
        controller.refresh();
        QCoreApplication::processEvents();
    }
    controller.refresh();
}

/// What one panel scenario observed. The controller/panel are destroyed at the
/// end of the scenario, so every value is copied out.
struct Observation {
    bool analyzed = false;
    QString snapshotBackend;
    bool labelExists = false;
    bool hintVisible = false;
    QString hintText;
    int issueRows = -1;
    int errors = -1;
    /// The session's diagnostics generation after the scenario settled: > 1
    /// proves the fake modelled the post-didChange publish the T2 gate needs.
    quint64 generation = 0;
};

Observation observePanel(const QString &backend, const QVector<DiagnosticEntry> &rows)
{
    Observation observed;
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("oc8051_acc.v"));
    auto lsp = std::make_unique<TinyFakeLsp>();
    lsp->backend = backend;
    lsp->rows = rows;
    DiagnosticsController controller(std::move(lsp), std::make_unique<TinyFakeSlang>());
    controller.setConfiguration(QStringLiteral("/fake/verible-verilog-ls"), dir.path());
    observed.analyzed =
        controller.analyzeFile(file, QStringLiteral("module m;\n  logic x\nendmodule\n"), false);
    settle(controller);
    observed.snapshotBackend = controller.session().backendName;
    observed.generation = controller.session().generation;

    DiagnosticsPanel panel(&controller);
    panel.refreshFromController();
    observed.labelExists = panel.findChild<QLabel *>(QStringLiteral("diagnosticsPreprocessHint"))
        != nullptr;
    observed.hintVisible = panel.hintVisible();
    observed.hintText = panel.hintText();
    observed.issueRows = panel.issueRowCount();
    observed.errors = panel.errorCount();
    return observed;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // =======================================================================
    // (1) The pure function: the trigger and the frozen sentence.
    // =======================================================================
    section("pure function");
    const QVector<DiagnosticEntry> preprocessRows{diagnostic(QString::fromUtf8(kVeriblePreprocessRow))};
    check("hint.verible-frozen-sentence",
          veriblePreprocessHint(QStringLiteral("verible"), preprocessRows) == QString::fromUtf8(kFrozenHint),
          veriblePreprocessHint(QStringLiteral("verible"), preprocessRows));
    check("hint.slang-never",
          veriblePreprocessHint(QStringLiteral("slang"), preprocessRows).isEmpty());
    check("hint.unknown-backend-never",
          veriblePreprocessHint(QString(), preprocessRows).isEmpty());
    check("hint.ordinary-row-never",
          veriblePreprocessHint(
              QStringLiteral("verible"),
              QVector<DiagnosticEntry>{diagnostic(QStringLiteral("syntax error at \"x\""))})
              .isEmpty());
    check("hint.half-marker-never",
          veriblePreprocessHint(QStringLiteral("verible"),
                                QVector<DiagnosticEntry>{diagnostic(
                                    QStringLiteral("preprocessing error at \"`M\" (no second marker)"))})
              .isEmpty());
    check("hint.second-half-only-never",
          veriblePreprocessHint(QStringLiteral("verible"),
                                QVector<DiagnosticEntry>{diagnostic(
                                    QStringLiteral("might not be defined before"))})
              .isEmpty());
    check("hint.no-rows-never",
          veriblePreprocessHint(QStringLiteral("verible"), {}).isEmpty());
    // One matching row among ordinary ones is enough (the hint describes the
    // displayed set, not every row).
    check("hint.one-row-among-ordinary",
          !veriblePreprocessHint(
               QStringLiteral("verible"),
               QVector<DiagnosticEntry>{diagnostic(QStringLiteral("syntax error at \"x\"")),
                                        diagnostic(QString::fromUtf8(kVeriblePreprocessRow))})
               .isEmpty());

    // =======================================================================
    // (2) The REAL panel: visible for verible + that row, hidden otherwise -
    //     and the hint is never counted as a diagnostic row.
    // =======================================================================
    section("panel");
    const Observation verible = observePanel(QStringLiteral("verible"), preprocessRows);
    check("panel.analyzed-verible", verible.analyzed);
    check("panel.snapshot-carries-backend", verible.snapshotBackend == QStringLiteral("verible"),
          verible.snapshotBackend);
    // F1 regression guard: the fake must model the didChange publish, otherwise
    // the T2 gate correctly drops its rows and the checks below observe nothing
    // (the bug was exactly this: rows=0 while the product is right).
    check("panel.publish-generation-advanced", verible.generation > 1,
          QStringLiteral("generation=%1").arg(verible.generation));
    check("panel.hint-label-is-persistent", verible.labelExists);
    check("panel.hint-visible", verible.hintVisible);
    check("panel.hint-text-frozen", verible.hintText == QString::fromUtf8(kFrozenHint),
          verible.hintText);
    check("panel.hint-is-not-a-row", verible.issueRows == 1 && verible.errors == 1,
          QStringLiteral("rows=%1 errors=%2").arg(verible.issueRows).arg(verible.errors));

    // The user can switch the LSP backend to slang: the same row must NOT bring
    // the Verible hint along (slang parses the macros; the sentence is false).
    const Observation slang = observePanel(QStringLiteral("slang"), preprocessRows);
    check("panel.slang-snapshot", slang.snapshotBackend == QStringLiteral("slang"),
          slang.snapshotBackend);
    check("panel.hint-hidden-for-slang", !slang.hintVisible);
    check("panel.hint-text-empty-for-slang", slang.hintText.isEmpty(), slang.hintText);
    check("panel.slang-row-still-listed", slang.issueRows == 1 && slang.errors == 1,
          QStringLiteral("rows=%1 errors=%2").arg(slang.issueRows).arg(slang.errors));

    // Verible with an ordinary diagnostic must not show the hint either.
    const Observation ordinary = observePanel(
        QStringLiteral("verible"),
        QVector<DiagnosticEntry>{diagnostic(QStringLiteral("syntax error at \"x\""))});
    check("panel.ordinary-row-has-no-hint", !ordinary.hintVisible);
    check("panel.ordinary-row-text-empty", ordinary.hintText.isEmpty(), ordinary.hintText);
    check("panel.ordinary-row-listed", ordinary.issueRows == 1 && ordinary.errors == 1,
          QStringLiteral("rows=%1 errors=%2").arg(ordinary.issueRows).arg(ordinary.errors));

    std::printf("VE-VERIBLE-PREPROCESS-HINT-SELFTEST checks=%d failures=%d\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
