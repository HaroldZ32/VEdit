// Independent third-pass re-review probe for ux-batch-2 B2 (b2-repair-r2).
// Reviewer-side only: not part of the product, its build or its tests.
//
// Re-runs the attacks that failed the lane in review-b2-r2 (F3/E3/D5/C4) and
// adds attacks the repair's own tests do not cover:
//   P*  the four previous corruptions, at the same code points the r2 probe used
//   N1  a save that lands DURING the scan (before SearchPanel::makeFrozenScan
//       captures the per-file stamps): can the line-shift wrong occurrence still
//       be replaced through the single 「替换」?
//   N2  the Ctrl+F bar's whole-project hand-off (applyHandoff) during the
//       counting scan: does the panel recover, or is it left locked?
//   N3  a single 「替换」 on a dirty buffer whose save keeps the hit range valid:
//       false refusal? what does the status say?
//   N4  the empty-query refusal at the API level (3-arg overload, 4-arg
//       overload, ProjectReplacer's worker gate)
//   N5  the control lock: every query/option/scope/replace widget during the
//       counting scan
//   N6  the confirmation-window timing case (query/option changed inside the
//       confirm handler)
#include "document_sync.h"
#include "project_replace.h"
#include "search_panel.h"
#include "search_query.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>

using namespace vedit::workspace::search;

static int failures = 0;

static void check(bool ok, const QString &name, const QString &detail = QString())
{
    std::printf("PROBE %s %s%s\n", qPrintable(name), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
    std::fflush(stdout);
    if (!ok) {
        ++failures;
    }
}

static void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(bytes);
    }
}

static QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

static bool waitIdle(SearchPanel &panel, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (!panel.isSearching() && !panel.isReplacing() && !panel.search()->isRunning() &&
            !panel.replacer()->isRunning()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (!panel.isSearching() && !panel.isReplacing() && !panel.search()->isRunning() &&
                !panel.replacer()->isRunning()) {
                return true;
            }
        }
        QThread::msleep(2);
    }
    return false;
}

static void baseSetup(SearchPanel &panel, const QString &root, const QString &query,
                      bool wholeWords = false)
{
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(vedit::search::SearchScope::WholeProject);
    panel.setQuery(query);
    panel.setCaseSensitive(true);
    panel.setWholeWords(wholeWords);
}

static int findHit(const SearchPanel &panel, int line, int column)
{
    for (int index = 0; index < panel.hits().size(); ++index) {
        const SearchHit &hit = panel.hits().at(index);
        if (hit.line == line && hit.column == column) {
            return index;
        }
    }
    return -1;
}

// An open, UNMODIFIED buffer: save() must not write, so the stamp captured at
// scan end stays the current stamp. `refresh()` does nothing (no reload).
class QuietBridge final : public OpenDocumentBridge
{
public:
    explicit QuietBridge(const QString &path)
        : path_(QFileInfo(path).absoluteFilePath())
    {
    }
    QStringList modifiedPaths() const override { return {}; }
    bool isOpen(const QString &absolutePath) const override
    {
        return QFileInfo(absolutePath).absoluteFilePath() == path_;
    }
    bool save(const QString &, QString *) override
    {
        ++saves;
        return true;
    }
    QString bufferText(const QString &) const override { return QString(); }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }
    int saves = 0;

private:
    QString path_;
};

// A dirty open buffer: save() writes the text the user now sees (the r2 probe's
// ShiftedBridge and the writer's SavingBridge).
class SavingBridge final : public OpenDocumentBridge
{
public:
    SavingBridge(const QString &path, const QByteArray &savedText)
        : path_(QFileInfo(path).absoluteFilePath())
        , savedText_(savedText)
    {
    }
    QStringList modifiedPaths() const override { return {path_}; }
    bool isOpen(const QString &absolutePath) const override
    {
        return QFileInfo(absolutePath).absoluteFilePath() == path_;
    }
    bool save(const QString &absolutePath, QString *) override
    {
        if (QFileInfo(absolutePath).absoluteFilePath() != path_) {
            return true;
        }
        writeAll(path_, savedText_);
        ++saves;
        return true;
    }
    QString bufferText(const QString &) const override { return QString(); }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }
    int saves = 0;

private:
    QString path_;
    QByteArray savedText_;
};

// --- P1: r2 probe F3, at the exact code point it used (the confirm handler) --
static void probeF3ConfirmWindow()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("whole.v"));
    writeAll(path, "foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"), true);
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("P1.search"));
    check(panel.hits().size() == 1, QStringLiteral("P1.hit"),
          QString::number(panel.hits().size()));

    bool confirmed = false;
    panel.setReplaceConfirmHandler([&panel, &confirmed, path](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        panel.setWholeWords(false);       // the whole-word toggle cleared
        writeAll(path, "foo_bar\n");      // the user's save during the operation
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceAllRequested(), QStringLiteral("P1.start"));
    check(waitIdle(panel), QStringLiteral("P1.idle"));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    std::printf("PROBE P1.after='%s' confirmed=%d ok=%d error='%s'\n",
                readAll(path).constData(), confirmed ? 1 : 0, outcome.ok ? 1 : 0,
                qPrintable(outcome.error));
    check(!outcome.ok, QStringLiteral("P1.refused"), outcome.error);
    check(readAll(path) == QByteArray("foo_bar\n"), QStringLiteral("P1.no-corruption"));
    check(panel.queryEdit()->isEnabled(), QStringLiteral("P1.controls-restored"));
    panel.hide();
}

// --- P2: r2 probe E3 (query cleared inside the confirm handler) --------------
static void probeE3ConfirmWindow()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("batch.v"));
    writeAll(path, "foo foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("P2.search"));
    check(panel.hits().size() >= 1, QStringLiteral("P2.hits"),
          QString::number(panel.hits().size()));

    bool confirmed = false;
    panel.setReplaceConfirmHandler([&panel, &confirmed, path](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        panel.setQuery(QString());        // the query cleared
        writeAll(path, "fix fix\n");      // the user's save during the operation
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceAllRequested(), QStringLiteral("P2.start"));
    check(waitIdle(panel), QStringLiteral("P2.idle"));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    std::printf("PROBE P2.after='%s' confirmed=%d ok=%d error='%s'\n",
                readAll(path).constData(), confirmed ? 1 : 0, outcome.ok ? 1 : 0,
                qPrintable(outcome.error));
    check(!outcome.ok, QStringLiteral("P2.refused"), outcome.error);
    check(readAll(path) == QByteArray("fix fix\n"), QStringLiteral("P2.no-corruption"));
    panel.hide();
}

// --- P3: r2 probe D5 (single 「替换」 after the query was cleared while idle) --
static void probeD5SingleCleared()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("stale.v"));
    writeAll(path, "  assign bar = foo + foo;\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("P3.search"));
    const int index = findHit(panel, 1, 16);
    check(index >= 0, QStringLiteral("P3.hit"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    SavingBridge bridge(path, QByteArray("  assign bar = fix + foo;\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setQuery(QString());
    const bool started = panel.replaceCurrentHit();
    check(waitIdle(panel), QStringLiteral("P3.idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    std::printf("PROBE P3.after='%s' started=%d saves=%d ok=%d error='%s'\n",
                readAll(path).constData(), started ? 1 : 0, bridge.saves,
                panel.lastReplaceOutcome().ok ? 1 : 0, qPrintable(panel.lastReplaceOutcome().error));
    check(!started || !panel.lastReplaceOutcome().ok, QStringLiteral("P3.refused"));
    check(bridge.saves == 0, QStringLiteral("P3.no-save"));
    check(readAll(path) == QByteArray("  assign bar = foo + foo;\n"),
          QStringLiteral("P3.no-write"));
    panel.hide();
}

// --- P4: r2 probe C4/A3 (line shift saved after the scan) --------------------
static void probeC4LineShiftAfterScan()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("shift.v"));
    writeAll(path, "foo\nabc foo\nxyz foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("P4.search"));
    const int index = findHit(panel, 2, 5);
    check(index >= 0, QStringLiteral("P4.hit"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    SavingBridge bridge(path, QByteArray("abc foo\nxyz foo\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(waitIdle(panel), QStringLiteral("P4.idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    std::printf("PROBE P4.after='%s' started=%d saves=%d ok=%d error='%s'\n",
                readAll(path).constData(), started ? 1 : 0, bridge.saves,
                panel.lastReplaceOutcome().ok ? 1 : 0, qPrintable(panel.lastReplaceOutcome().error));
    check(!started || !panel.lastReplaceOutcome().ok, QStringLiteral("P4.refused"));
    check(!readAll(path).contains("BAR"), QStringLiteral("P4.no-wrong-occurrence"));
    panel.hide();
}

// --- N1: a save DURING the scan (before the stamps are captured) -------------
// The scan reads the file, its hits arrive (pre-shift), and while they are being
// delivered the user's save writes the shifted content. SearchPanel::makeFrozenScan
// runs only when `finished` is delivered, i.e. AFTER that write, so the frozen
// stamp describes the POST-shift file while the hits describe the PRE-shift file;
// the worker's stamp check therefore cannot see the change, and the text check
// accepts the same token at the same coordinates.
static void probeSaveDuringScan()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("freeze.v"));
    writeAll(path, "foo\nabc foo\nxyz foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));

    bool savedDuringScan = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&savedDuringScan, path](const QList<SearchHit> &) {
            if (savedDuringScan) {
                return;
            }
            savedDuringScan = true;
            // The user's save while the counting scan is still in flight, i.e.
            // before SearchPanel::onSearchFinished freezes the document stamps.
            writeAll(path, "abc foo\nxyz foo\n");
        });
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("N1.search"));
    QObject::disconnect(hook);
    check(savedDuringScan, QStringLiteral("N1.save-during-scan"));

    const int index = findHit(panel, 2, 5);
    check(index >= 0, QStringLiteral("N1.hit"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    QuietBridge bridge(path);
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(waitIdle(panel), QStringLiteral("N1.idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const QByteArray after = readAll(path);
    std::printf("PROBE N1.after='%s' started=%d ok=%d replacements=%d status='%s' error='%s'\n",
                after.constData(), started ? 1 : 0, panel.lastReplaceOutcome().ok ? 1 : 0,
                panel.lastReplaceOutcome().replacements,
                qPrintable(panel.replaceStatusLabel()->text()),
                qPrintable(panel.lastReplaceOutcome().error));
    // The user selected the occurrence that is now on line 1 (`abc foo`); a
    // correct implementation refuses or replaces THAT one. `xyz BAR` means the
    // wrong occurrence (the stale line/column of a different match).
    check(after != QByteArray("abc foo\nxyz BAR\n"),
          QStringLiteral("N1.no-wrong-occurrence"), after);
    panel.hide();
}

// --- N1b: the same scan-window race through 「全部替换」 ---------------------
// Pre-scan file: "foo a\nfoo b\n" (hits line 1 col 1 and line 2 col 1). While
// the counting scan is in flight the user saves an inserted line at the top:
// "foo c\nfoo a\nfoo b\n". The stamps are frozen after that save and every old
// range still holds "foo", so both the stamp guard and the text guard accept;
// the replacement then hits the NEW "foo c" and the shifted "foo a", leaves
// "foo b" untouched, and reports success with the pre-scan count.
static void probeSaveDuringScanReplaceAll()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("freeze-all.v"));
    writeAll(path, "foo a\nfoo b\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));

    bool savedDuringScan = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&savedDuringScan, path](const QList<SearchHit> &) {
            if (savedDuringScan) {
                return;
            }
            savedDuringScan = true;
            writeAll(path, "foo c\nfoo a\nfoo b\n");
        });
    bool confirmed = false;
    panel.setReplaceConfirmHandler([&confirmed](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceAllRequested(), QStringLiteral("N1b.start"));
    check(waitIdle(panel), QStringLiteral("N1b.idle"));
    QObject::disconnect(hook);
    check(savedDuringScan, QStringLiteral("N1b.save-during-scan"));

    const QByteArray after = readAll(path);
    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    std::printf("PROBE N1b.after='%s' confirmed=%d ok=%d replacements=%d status='%s' error='%s'\n",
                after.constData(), confirmed ? 1 : 0, outcome.ok ? 1 : 0,
                outcome.replacements, qPrintable(panel.replaceStatusLabel()->text()),
                qPrintable(outcome.error));
    // Correct outcomes: refuse (the document changed) or replace the two
    // occurrences the scan saw (`foo a`, `foo b`). The corruption is the
    // replacement of the new `foo c` with `foo b` left behind.
    check(after != QByteArray("BAR c\nBAR a\nfoo b\n"),
          QStringLiteral("N1b.no-wrong-occurrence"), after);
    panel.hide();
}

// --- N2: the Ctrl+F bar's whole-project hand-off during the counting scan ----
static void probeHandoffDuringScan()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("handoff.v"));
    writeAll(path, "foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));
    // If the abandoned operation ever reached the dialog, this keeps the probe
    // offscreen and non-blocking.
    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return false; });

    bool handedOff = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&panel, &handedOff](const QList<SearchHit> &) {
            if (handedOff) {
                return;
            }
            handedOff = true;
            // What FindReplaceBar::requestProjectSearch() does: it publishes the
            // bar's query/options (scope WholeProject) and invokes the dock's
            // handler, which is SearchPanel::applyHandoff().
            vedit::search::SearchHandoff handoff;
            handoff.text = QStringLiteral("bar");
            handoff.options.caseSensitive = true;
            handoff.options.scope = vedit::search::SearchScope::WholeProject;
            panel.applyHandoff(handoff);
        });
    panel.replaceAllRequested();
    // Let both the abandoned counting scan and the hand-off's browse scan settle.
    for (int spin = 0; spin < 20 && (panel.isSearching() || panel.search()->isRunning()); ++spin) {
        waitIdle(panel);
    }
    QObject::disconnect(hook);
    // Extra event turns: the generation-dropped `finished` of the cancelled scan
    // and the new scan's result both need to be delivered.
    for (int spin = 0; spin < 200; ++spin) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    const bool idle = !panel.isSearching() && !panel.isReplacing() &&
                      !panel.search()->isRunning() && !panel.replacer()->isRunning();
    auto *replaceAll = panel.findChild<QPushButton *>(QStringLiteral("search-replace-all"));
    std::printf("PROBE N2.handed=%d idle=%d queryEnabled=%d replaceAllEnabled=%d "
                "replaceEditEnabled=%d outcomeOk=%d error='%s'\n",
                handedOff ? 1 : 0, idle ? 1 : 0, panel.queryEdit()->isEnabled() ? 1 : 0,
                replaceAll != nullptr && replaceAll->isEnabled() ? 1 : 0,
                panel.replaceEdit()->isEnabled() ? 1 : 0, outcome.ok ? 1 : 0,
                qPrintable(outcome.error));
    check(handedOff && idle, QStringLiteral("N2.handoff-delivered"));
    check(readAll(path) == QByteArray("foo\n"), QStringLiteral("N2.no-write"));
    // The defect: with no scan/replace running, the panel must be interactive
    // again. A disabled query field after the hand-off means the abandoned
    // Replace All left `operationActive_` set with no owner to clear it.
    check(panel.queryEdit()->isEnabled(), QStringLiteral("N2.controls-recovered"));
    check(replaceAll != nullptr && replaceAll->isEnabled(),
          QStringLiteral("N2.replace-all-recovered"));
    panel.hide();
}

// --- N3: single 「替换」 on a dirty buffer whose save keeps the range valid ---
static void probeSingleDirtyBuffer()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dirty.v"));
    writeAll(path, "foo\nabc foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("N3.search"));
    const int index = findHit(panel, 1, 1);
    check(index >= 0, QStringLiteral("N3.hit"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The user's unsaved edit is on line 2; the hit range on line 1 is untouched
    // by it, so the replacement itself would be exact.
    SavingBridge bridge(path, QByteArray("foo\nabc foo edited\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(waitIdle(panel), QStringLiteral("N3.idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    std::printf("PROBE N3.after='%s' started=%d saves=%d ok=%d error='%s'\n",
                readAll(path).constData(), started ? 1 : 0, bridge.saves,
                panel.lastReplaceOutcome().ok ? 1 : 0, qPrintable(panel.lastReplaceOutcome().error));
    check(bridge.saves == 1, QStringLiteral("N3.save-first-ran"));
    // Report only: this probe records whether a valid-range dirty-buffer replace
    // is refused by the stamp guard that was frozen before the panel's own save.
    check(readAll(path) == QByteArray("foo\nabc foo edited\n") ||
              readAll(path) == QByteArray("BAR\nabc foo edited\n"),
          QStringLiteral("N3.one-of-two-documented-outcomes"), readAll(path));
    panel.hide();
}

// --- N4: the empty-query refusal at the API level ------------------------------
static void probeEmptyQueryApi(const QString &root)
{
    const QList<ReplaceHit> hits{ReplaceHit{QStringLiteral("/tmp/x.v"), 1, 1, 3}};
    const TextReplaceResult legacy =
        applyReplaceRanges(QStringLiteral("foo\n"), hits, QStringLiteral("BAR"));
    check(!legacy.ok, QStringLiteral("N4.three-arg-refused"), legacy.error);
    check(legacy.error.contains(QStringLiteral("no query")), QStringLiteral("N4.three-arg-names-reason"),
          legacy.error);

    const TextReplaceResult explicitEmpty = applyReplaceRanges(
        QStringLiteral("foo\n"), hits, QStringLiteral("BAR"), ReplaceQuery{});
    check(!explicitEmpty.ok, QStringLiteral("N4.empty-query-refused"), explicitEmpty.error);

    const TextReplaceResult valid = applyReplaceRanges(
        QStringLiteral("foo\n"), hits, QStringLiteral("BAR"),
        ReplaceQuery{QStringLiteral("foo"), true, false, false});
    check(valid.ok && valid.text == QStringLiteral("BAR\n"), QStringLiteral("N4.valid-control"),
          valid.error);

    // The worker gate: an empty snapshot query and a zero scan id must both be
    // refused before any file is opened.
    const QString path = QDir(root).filePath(QStringLiteral("worker-gate.v"));
    writeAll(path, "foo\n");
    {
        ProjectReplacer replacer;
        bool done = false;
        ReplaceOutcome outcome;
        QObject::connect(&replacer, &ProjectReplacer::finished, &replacer,
                         [&done, &outcome](const ReplaceOutcome &value) {
                             done = true;
                             outcome = value;
                         });
        ReplaceSnapshot snapshot;
        snapshot.query = ReplaceQuery{QStringLiteral("foo"), true, false, false};
        snapshot.scanId = 0;
        const bool started = replacer.start({ReplaceHit{path, 1, 1, 3, 7}}, QStringLiteral("BAR"),
                                            atomicSaveRewriter(), snapshot);
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(2);
        }
        check(started && done && !outcome.ok, QStringLiteral("N4.worker-zero-scan-refused"),
              outcome.error);
        check(outcome.error.contains(QStringLiteral("frozen scan identity")),
              QStringLiteral("N4.worker-zero-scan-names-reason"), outcome.error);
        check(readAll(path) == QByteArray("foo\n"), QStringLiteral("N4.worker-zero-scan-no-write"));
    }
    {
        ProjectReplacer replacer;
        bool done = false;
        ReplaceOutcome outcome;
        QObject::connect(&replacer, &ProjectReplacer::finished, &replacer,
                         [&done, &outcome](const ReplaceOutcome &value) {
                             done = true;
                             outcome = value;
                         });
        ReplaceSnapshot snapshot;
        snapshot.scanId = 7;
        const bool started = replacer.start({ReplaceHit{path, 1, 1, 3, 7}}, QStringLiteral("BAR"),
                                            atomicSaveRewriter(), snapshot);
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(2);
        }
        check(started && done && !outcome.ok, QStringLiteral("N4.worker-empty-query-refused"),
              outcome.error);
        check(readAll(path) == QByteArray("foo\n"),
              QStringLiteral("N4.worker-empty-query-no-write"));
    }
}

// --- N5: the control lock during the counting scan ---------------------------
static void probeControlLock()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("lock.v"));
    writeAll(path, "foo\n");

    SearchPanel panel;
    baseSetup(panel, dir.path(), QStringLiteral("foo"));

    bool measured = false;
    bool allDisabled = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&panel, &measured, &allDisabled](const QList<SearchHit> &) {
            if (measured) {
                return;
            }
            measured = true;
            allDisabled = !panel.queryEdit()->isEnabled() && !panel.wholeWordButton()->isEnabled() &&
                          !panel.regexButton()->isEnabled() && !panel.scopeCombo()->isEnabled() &&
                          !panel.replaceEdit()->isEnabled();
        });
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("N5.search"));
    QObject::disconnect(hook);
    check(measured, QStringLiteral("N5.measured"));
    check(allDisabled, QStringLiteral("N5.query-options-scope-replace-disabled"));
    panel.hide();
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("review-b2-r3-probe"));

    probeF3ConfirmWindow();
    probeE3ConfirmWindow();
    probeD5SingleCleared();
    probeC4LineShiftAfterScan();
    probeSaveDuringScan();
    probeSaveDuringScanReplaceAll();
    probeHandoffDuringScan();
    probeSingleDirtyBuffer();
    QTemporaryDir apiDir;
    probeEmptyQueryApi(apiDir.path());
    probeControlLock();

    std::printf("PROBE SUMMARY failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
