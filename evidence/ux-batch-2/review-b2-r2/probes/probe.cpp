// Independent re-review probe for ux-batch-2 B2 (b2-repair), reviewer-side only.
// Not part of the product or its tests. Exercises the reviewed functions directly:
//   A) applyReplaceRanges range-validation semantics (High 2), BOM/CRLF/astral (High 4)
//   B) the real EditorOpenDocumentBridge reload guard (Blocker 1)
#include "document_sync.h"
#include "project_replace.h"
#include "search_panel.h"
#include "search_query.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTreeWidget>
#include <QTest>

using vedit::workspace::search::ReplaceHit;
using vedit::workspace::search::ReplaceQuery;
using vedit::workspace::search::TextReplaceResult;

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

static QList<ReplaceHit> hits(std::initializer_list<ReplaceHit> list)
{
    QList<ReplaceHit> out;
    for (const ReplaceHit &hit : list) {
        out.append(hit);
    }
    return out;
}

static TextReplaceResult apply(const QString &text, const QList<ReplaceHit> &hits, const QString &replacement,
                               const ReplaceQuery &query)
{
    return vedit::workspace::search::applyReplaceRanges(text, hits, replacement, query);
}

static void partA()
{
    const ReplaceQuery literal{QStringLiteral("foo"), true, false, false};

    // A1: valid literal range on unchanged text (control).
    {
        const auto r = apply(QStringLiteral("  assign bar = foo + foo;\n"),
                             hits({ReplaceHit{QString(), 1, 16, 3}}), QStringLiteral("BAR"), literal);
        check(r.ok && r.text == QStringLiteral("  assign bar = BAR + foo;\n"), QStringLiteral("A1.literal-valid"),
              r.text + QStringLiteral(" / ") + r.error);
    }
    // A2: the original review's stale probe: the same length, different text at the range.
    {
        const auto r = apply(QStringLiteral("  assign bar = fix + foo;\n"),
                             hits({ReplaceHit{QString(), 1, 16, 3}}), QStringLiteral("BAR"), literal);
        check(!r.ok && r.text.contains(QStringLiteral("fix")), QStringLiteral("A2.stale-refused"), r.error);
    }
    // A3: a stale hit after a line deletion: the old line/column now holds ANOTHER
    // occurrence of the same query, so the text at the range still matches.
    {
        const auto r = apply(QStringLiteral("abc foo\nxyz foo\n"),
                             hits({ReplaceHit{QString(), 2, 5, 3}}), QStringLiteral("BAR"), literal);
        check(r.ok && r.text == QStringLiteral("abc foo\nxyz BAR\n"), QStringLiteral("A3.line-shift-accepted"),
              r.text + QStringLiteral(" / ") + r.error);
    }
    // A4: literal metacharacter must be escaped by the validator too.
    {
        const auto r = apply(QStringLiteral("a.b aXb\n"), hits({ReplaceHit{QString(), 1, 5, 3}}),
                             QStringLiteral("Z"), ReplaceQuery{QStringLiteral("a.b"), true, false, false});
        check(!r.ok, QStringLiteral("A4.literal-meta-stale-refused"), r.error);
    }
    // A5: whole word: an embedded range is refused, a standalone range is replaced.
    {
        const ReplaceQuery whole{QStringLiteral("foo"), true, true, false};
        const auto embedded = apply(QStringLiteral("foo_bar foo\n"), hits({ReplaceHit{QString(), 1, 1, 3}}),
                                    QStringLiteral("BAR"), whole);
        const auto standalone = apply(QStringLiteral("foo_bar foo\n"), hits({ReplaceHit{QString(), 1, 9, 3}}),
                                      QStringLiteral("BAR"), whole);
        check(!embedded.ok && standalone.ok && standalone.text == QStringLiteral("foo_bar BAR\n"),
              QStringLiteral("A5.whole-word"), embedded.error + QStringLiteral(" / ") + standalone.text);
    }
    // A6: regex range: valid and stale.
    {
        const ReplaceQuery regex{QStringLiteral("[0-9]+"), true, false, true};
        const auto valid = apply(QStringLiteral("abc 123\n"), hits({ReplaceHit{QString(), 1, 5, 3}}),
                                 QStringLiteral("N"), regex);
        const auto stale = apply(QStringLiteral("abc xyz\n"), hits({ReplaceHit{QString(), 1, 5, 3}}),
                                 QStringLiteral("N"), regex);
        check(valid.ok && valid.text == QStringLiteral("abc N\n") && !stale.ok, QStringLiteral("A6.regex"),
              valid.error + QStringLiteral(" / ") + stale.error);
    }
    // A7: case-insensitive range.
    {
        const auto r = apply(QStringLiteral("FOO\n"), hits({ReplaceHit{QString(), 1, 1, 3}}), QStringLiteral("BAR"),
                             ReplaceQuery{QStringLiteral("foo"), false, false, false});
        check(r.ok && r.text == QStringLiteral("BAR\n"), QStringLiteral("A7.case-insensitive"), r.error);
    }
    // A8: BOM keeps line-1 columns aligned and round-trips the BOM bytes.
    {
        const QString bom = QString(QChar(0xFEFF));
        const auto r = apply(bom + QStringLiteral("foo a;\nfoo b;\n"),
                             hits({ReplaceHit{QString(), 1, 2, 3}, ReplaceHit{QString(), 2, 1, 3}}),
                             QStringLiteral("BAR"), literal);
        const QByteArray bytes = r.text.toUtf8();
        check(r.ok && bytes == QByteArray::fromHex("EFBBBF") + QByteArray("BAR a;\nBAR b;\n"),
              QStringLiteral("A8.bom"), QString::fromLatin1(bytes.toHex()));
    }
    // A9: CRLF is preserved and columns exclude the '\r'.
    {
        const auto r = apply(QStringLiteral("foo\r\nfoo\r\n"),
                             hits({ReplaceHit{QString(), 1, 1, 3}, ReplaceHit{QString(), 2, 1, 3}}),
                             QStringLiteral("BAR"), literal);
        check(r.ok && r.text == QStringLiteral("BAR\r\nBAR\r\n"), QStringLiteral("A9.crlf"),
              QString::fromLatin1(r.text.toUtf8().toHex()));
    }
    // A10: astral character before the hit is one character, not two UTF-16 units.
    {
        const auto r = apply(QString::fromUtf8("\xF0\x9F\x98\x80 foo\n"), hits({ReplaceHit{QString(), 1, 3, 3}}),
                             QStringLiteral("BAR"), literal);
        check(r.ok && r.text == QString::fromUtf8("\xF0\x9F\x98\x80 BAR\n"), QStringLiteral("A10.astral"),
              r.error);
    }
    // A11: two hits on one line, second one after the first (normal all-matches case).
    {
        const auto r = apply(QStringLiteral("foo foo\n"),
                             hits({ReplaceHit{QString(), 1, 1, 3}, ReplaceHit{QString(), 1, 5, 3}}),
                             QStringLiteral("BAR"), literal);
        check(r.ok && r.text == QStringLiteral("BAR BAR\n"), QStringLiteral("A11.two-hits"), r.error);
    }
}

static void writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(bytes);
    }
}


// Part C: is the stale-location residual reachable through the real panel's
// single 「替换」 path? A fake bridge stands in for the edited, modified KTextEditor
// buffer: its save() writes the text the user now sees (line 1 deleted).
class ShiftedBridge final : public vedit::workspace::search::OpenDocumentBridge
{
public:
    ShiftedBridge(QString path, QByteArray savedText)
        : path_(QFileInfo(path).absoluteFilePath())
        , savedText_(std::move(savedText))
    {
    }
    QStringList modifiedPaths() const override { return {path_}; }
    bool isOpen(const QString &path) const override
    {
        return QFileInfo(path).absoluteFilePath() == path_;
    }
    bool save(const QString &path, QString *) override
    {
        if (QFileInfo(path).absoluteFilePath() != path_) {
            return true;
        }
        writeAll(path_, savedText_);
        ++saves;
        return true;
    }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    vedit::workspace::search::RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return vedit::workspace::search::RefreshStatus::NotOpen;
    }
    int saves = 0;

private:
    QString path_;
    QByteArray savedText_;
};

static bool waitIdle(vedit::workspace::search::SearchPanel &panel, int timeoutMs = 30000)
{
    QElapsedTimer timer;
    timer.start();
    int quiet = 0;
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
        if (!panel.isSearching() && !panel.isReplacing()) {
            if (++quiet >= 10) {
                return true;
            }
        } else {
            quiet = 0;
        }
    }
    return false;
}

static void partC()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("shift.v"));
    writeAll(path, "foo\nabc foo\nxyz foo\n");

    vedit::workspace::search::SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(vedit::search::SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("C1.search-idle"));
    int index = -1;
    for (int i = 0; i < panel.hits().size(); ++i) {
        if (panel.hits().at(i).line == 2 && panel.hits().at(i).column == 5) {
            index = i;
        }
    }
    check(index >= 0, QStringLiteral("C2.stale-hit-found"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    // The user deletes line 1 above the selected hit; the save-first step of
    // 「替换」 writes that edited text before the range is applied.
    ShiftedBridge bridge(path, QByteArray("abc foo\nxyz foo\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.activateHit(index);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(started, QStringLiteral("C3.replace-started"));
    check(waitIdle(panel), QStringLiteral("C3.replace-idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);
    QByteArray after;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            after = f.readAll();
        }
    }
    std::printf("PROBE C4.saved-buffer-writes=%d after='%s'\n", bridge.saves, after.constData());
    check(after == QByteArray("abc foo\nxyz BAR\n"), QStringLiteral("C4.wrong-occurrence-replaced"), after);
    check(panel.lastReplaceOutcome().ok && panel.lastReplaceOutcome().replacements == 1,
          QStringLiteral("C4.outcome-ok"), panel.lastReplaceOutcome().error);
    panel.hide();
}

static void partB()
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    check(editor != nullptr, QStringLiteral("B0.editor"));
    if (editor == nullptr) {
        return;
    }
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("doc.v"));
    writeAll(path, "foo foo\n");

    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    check(document != nullptr && view != nullptr, QStringLiteral("B0.document"));
    if (view == nullptr) {
        return;
    }
    view->resize(600, 300);
    view->show();
    document->setText(QStringLiteral("foo foo\n"));
    document->setModified(false);
    view->setProperty("vedit.openFilePath", path);

    auto *bridge = vedit::workspace::search::openDocumentBridge();
    check(bridge != nullptr, QStringLiteral("B0.bridge"));
    if (bridge == nullptr) {
        return;
    }
    check(bridge->isOpen(path), QStringLiteral("B1.open-detected"));

    // The user's unsaved edit; the save-first step writes it.
    document->setText(QStringLiteral("foo foo\n// typed-before-save\n"));
    document->setModified(true);
    QString error;
    check(bridge->save(path, &error), QStringLiteral("B2.saved"), error);
    check(!document->isModified() && QFileInfo(path).size() > 0, QStringLiteral("B2.saved-unmodified"));
    const QString savedToken = bridge->contentToken(path);
    check(!savedToken.isEmpty(), QStringLiteral("B2.token-captured"));

    // The worker writes the replaced content of the saved text.
    writeAll(path, "qux qux\n// typed-before-save\n");

    // The user keeps typing AFTER the save-first step, before the reload.
    document->setText(document->text() + QStringLiteral("// typed-after-save\n"));
    document->setModified(true);
    const auto status = bridge->refresh(path, savedToken, &error);
    check(status == vedit::workspace::search::RefreshStatus::Changed, QStringLiteral("B3.changed-status"));
    check(document->text().contains(QStringLiteral("// typed-after-save")),
          QStringLiteral("B3.edit-survives"), document->text());
    check(document->isModified(), QStringLiteral("B3.still-modified"));
    check(document->text().contains(QStringLiteral("// typed-before-save")),
          QStringLiteral("B3.saved-text-survives"));

    // A user who saves their typed text during the batch: the token no longer
    // matches, so refresh must not reload over it either.
    writeAll(path, "qux qux\n// typed-before-save\n// typed-after-save\n");
    check(bridge->save(path, &error), QStringLiteral("B4.user-save"), error);
    // The worker's write lands after the user's save (read -> save -> write race):
    writeAll(path, "qux qux\n// typed-before-save\n");
    const auto status2 = bridge->refresh(path, savedToken, &error);
    check(status2 == vedit::workspace::search::RefreshStatus::Changed, QStringLiteral("B4.token-mismatch-status"));
    check(document->text().contains(QStringLiteral("// typed-after-save")),
          QStringLiteral("B4.saved-edit-still-in-buffer"), document->text());
    std::printf("PROBE B4.after-state modified=%d disk='%s'\n", document->isModified() ? 1 : 0,
                QFile(path).readAll().constData());

    // Control: an unmodified buffer that still holds the saved (pre-replace)
    // text, with the replaced content on disk, IS reloaded.
    document->setText(QStringLiteral("foo foo\n// typed-before-save\n"));
    document->setModified(false);
    const auto status3 = bridge->refresh(path, savedToken, &error);
    check(status3 == vedit::workspace::search::RefreshStatus::Reloaded, QStringLiteral("B5.reload-control"),
          error);
    check(document->text() == QStringLiteral("qux qux\n// typed-before-save\n"),
          QStringLiteral("B5.reloaded-text"), document->text());

    // Not open at capture (empty token): unmodified -> reload, modified -> kept.
    document->setText(QStringLiteral("old\n"));
    document->setModified(true);
    check(bridge->refresh(path, QString(), &error) == vedit::workspace::search::RefreshStatus::Changed,
          QStringLiteral("B6.empty-token-modified-kept"));
    check(document->text() == QStringLiteral("old\n"), QStringLiteral("B6.text-kept"));
    document->setModified(false);
    check(bridge->refresh(path, QString(), &error) == vedit::workspace::search::RefreshStatus::Reloaded,
          QStringLiteral("B7.empty-token-unmodified-reloaded"));
    check(document->text() == QStringLiteral("qux qux\n// typed-before-save\n"),
          QStringLiteral("B7.reloaded-now"), document->text());

    view->hide();
    delete view;
    delete document;
}


// Part D: the H2 validator is built from the CURRENT query field at write time,
// not from the query that produced the hits. Clearing the field after the scan
// makes ReplaceQuery.pattern empty, which disables the range guard; the old
// length-only behaviour is reachable again through the real panel.
static void partD()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("bypass.v"));
    writeAll(path, "  assign bar = foo + foo;\n");

    vedit::workspace::search::SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(vedit::search::SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("D1.search-idle"));
    int index = -1;
    for (int i = 0; i < panel.hits().size(); ++i) {
        if (panel.hits().at(i).line == 1 && panel.hits().at(i).column == 16) {
            index = i;
        }
    }
    check(index >= 0, QStringLiteral("D2.hit-found"), QString::number(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);
    // The user edits the file (the hit's range now holds "fix"), then clears the
    // query field (no new search); the results stay. The panel's save-first step
    // writes the edited buffer through the bridge before the range is applied.
    ShiftedBridge bridge(path, QByteArray("  assign bar = fix + foo;\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setQuery(QString());
    check(panel.query().isEmpty() && !panel.hits().isEmpty(), QStringLiteral("D3.stale-hits-kept"));
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceCurrentHit(), QStringLiteral("D4.replace-started"));
    check(waitIdle(panel), QStringLiteral("D4.replace-idle"));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);
    QByteArray after;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            after = f.readAll();
        }
    }
    std::printf("PROBE D5.after='%s' ok=%d replacements=%d\n", after.constData(),
                panel.lastReplaceOutcome().ok ? 1 : 0, panel.lastReplaceOutcome().replacements);
    check(after == QByteArray("  assign bar = BAR + foo;\n"),
          QStringLiteral("D5.empty-query-guard-bypass-writes-stale-range"), after);
    panel.hide();
}


// Part E: the same mutable-query hole in the Replace All path. The counting
// scan produces the plan with "foo"; the query field is cleared before the
// write (a state the UI allows during the counting scan) and the file is saved
// with "fix" at the old ranges. The worker's ReplaceQuery is empty, so the
// stale ranges are applied without the text guard.
static void partE()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("batch.v"));
    writeAll(path, "foo foo\n");

    vedit::workspace::search::SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(vedit::search::SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("E1.search-idle"));

    panel.setReplaceConfirmHandler([&panel, path](const vedit::workspace::search::ReplacePlan &, const QStringList &) {
        // The user kept working during the counting scan: the query is cleared
        // and the editor saved "fix" over the scanned "foo" at the same ranges.
        panel.setQuery(QString());
        writeAll(path, "fix fix\n");
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceAllRequested(), QStringLiteral("E2.replace-started"));
    check(waitIdle(panel), QStringLiteral("E2.replace-idle"));
    QByteArray after;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            after = f.readAll();
        }
    }
    std::printf("PROBE E3.after='%s' ok=%d replacements=%d\n", after.constData(),
                panel.lastReplaceOutcome().ok ? 1 : 0, panel.lastReplaceOutcome().replacements);
    check(after == QByteArray("BAR BAR\n"),
          QStringLiteral("E3.replace-all-empty-query-writes-stale-range"), after);
    panel.hide();
}


// Part F: the strongest form of the mutable-query hole — a NON-EMPTY query whose
// match OPTIONS change between the counting scan and the write. The scan is
// whole-word (so "foo_bar" is not a hit); the whole-word toggle is cleared
// before the write (the dialog's text still says "foo"), the user saves
// "foo_bar" at the old range, and the looser literal validator accepts and
// replaces the "foo" inside the identifier. No dialog text reveals the change.
static void partF()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("whole.v"));
    writeAll(path, "foo\n");

    vedit::workspace::search::SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(vedit::search::SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.setWholeWords(true);
    panel.startSearch();
    check(waitIdle(panel), QStringLiteral("F1.search-idle"));
    check(panel.hits().size() == 1 && panel.hits().at(0).line == 1 && panel.hits().at(0).column == 1,
          QStringLiteral("F1.whole-word-hit"), QString::number(panel.hits().size()));

    panel.setReplaceConfirmHandler([&panel, path](const vedit::workspace::search::ReplacePlan &,
                                                   const QStringList &) {
        // The whole-word toggle is cleared during the counting scan, and the
        // user saves an identifier that the scanned whole-word matcher never
        // would have hit.
        panel.setWholeWords(false);
        writeAll(path, "foo_bar\n");
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(panel.replaceAllRequested(), QStringLiteral("F2.replace-started"));
    check(waitIdle(panel), QStringLiteral("F2.replace-idle"));
    QByteArray after;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            after = f.readAll();
        }
    }
    std::printf("PROBE F3.after='%s' ok=%d replacements=%d\n", after.constData(),
                panel.lastReplaceOutcome().ok ? 1 : 0, panel.lastReplaceOutcome().replacements);
    check(after == QByteArray("BAR_bar\n"),
          QStringLiteral("F3.replace-all-looser-options-write-stale-range"), after);
    panel.hide();
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("review-b2-r2-probe"));
    partA();
    partB();
    partC();
    partD();
    partE();
    partF();
    std::printf("PROBE SUMMARY failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
