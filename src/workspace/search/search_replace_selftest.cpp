// Lane self-check for ux-batch-2 B2: the shared search option model, the
// project search dock's replace row, the Ctrl+F whole-project hand-off and the
// unsaved-buffer rule.
//
//   vedit-search-replace-tests --selfcheck [--evidence <dir>]
//
// The harness is OFFSCREEN (ctest sets QT_QPA_PLATFORM=offscreen) and runs
// against a temporary fixture project; the real Rust search service and the
// real KTextEditor document/view are exercised (the bar checks report SKIP when
// the katepart plugin is unavailable, never a fake PASS).
//
// Acceptance covered (contract .mpd/plans/ux-batch-2/00-plan.md §B2):
//   (i)   both surfaces expose the same option set / round-trip the same model
//   (ii)  「替换」 changes exactly one occurrence and advances to the next
//   (iii) 「全部替换」 changes exactly the expected occurrences, reports the
//         right counts, and writes each file exactly once
//   (iv)  regex / whole-word / case behaviour
//   (v)   after the Ctrl+F whole-project hand-off the dock's query and options
//         match, both ways
//   (vi)  a modified editor buffer is saved before the replace (and a refused
//         save refuses the whole replace without writing anything)
//
// The fixture's before/after trees and their unified diff are written to
// --evidence (default: a temp directory printed at the end).
#include "document_sync.h"
#include "project_replace.h"
#include "search_panel.h"
#include "search_query.h"
#include "search_types.h"

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/document_io.h"
#include "kernel/find_replace.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#endif

#if defined(VEDIT_SEARCH_HAVE_SHELL)
#include "app_shell.h"
#include "panel_registry.h"
#include "search_integration.h"

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "editor_area.h"
#include "kernel/ktexteditor_provider.h"
#endif

#include <QMainWindow>
#endif

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QPushButton>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

using vedit::workspace::search::FileRewriter;
using vedit::workspace::search::GlobalSearch;
using vedit::workspace::search::OpenDocumentBridge;
using vedit::workspace::search::RefreshStatus;
using vedit::workspace::search::ReplaceOutcome;
using vedit::workspace::search::ReplacePlan;
using vedit::workspace::search::ReplaceWriteResult;
using vedit::workspace::search::SearchHit;
using vedit::workspace::search::SearchPanel;
using vedit::search::QueryOptions;
using vedit::search::SearchScope;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const QString &name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("CHECK %s PASS\n", qPrintable(name));
        std::fflush(stdout);
        return;
    }
    ++g_failures;
    std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
    std::fflush(stdout);
}

void skip(const QString &name, const QString &why)
{
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", qPrintable(name), qPrintable(why));
    std::fflush(stdout);
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs = 60000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(2);
    }
    return true;
}

// The panel is idle when neither the counting scan nor the replace worker is
// running — held for ~50 ms so the post-replace result refresh is included.
bool waitForPanelIdle(SearchPanel &panel, int timeoutMs = 60000)
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

bool waitForSearch(SearchPanel &panel, int timeoutMs = 60000)
{
    const bool done = waitUntil([&panel]() { return !panel.isSearching(); }, timeoutMs);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

void writeText(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(contents);
    }
}

int countOccurrences(const QString &text, const QString &needle, Qt::CaseSensitivity cs)
{
    if (needle.isEmpty()) {
        return 0;
    }
    return text.count(needle, cs);
}

QStringList distinctFiles(const SearchPanel &panel)
{
    QStringList files;
    for (const SearchHit &hit : panel.hits()) {
        if (!files.contains(hit.filePath)) {
            files.append(hit.filePath);
        }
    }
    files.sort();
    return files;
}

void copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator iterator(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString source = iterator.next();
        const QString relative = QDir(from).relativeFilePath(source);
        const QString target = QDir(to).filePath(relative);
        if (QFileInfo(source).isDir()) {
            QDir().mkpath(target);
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::remove(target);
            QFile::copy(source, target);
        }
    }
}

QString fixtureListing(const QString &root, const QStringList &files)
{
    QString listing;
    for (const QString &name : files) {
        listing += QStringLiteral("=== %1 ===\n").arg(name);
        listing += readText(QDir(root).filePath(name));
        listing += QStringLiteral("\n");
    }
    return listing;
}

// The temporary fixture project. The contents are chosen so every acceptance
// case has an unambiguous expected count; see the section checks below.
QStringList fixtureFileNames()
{
    return {QStringLiteral("a.v"),      QStringLiteral("b.sv"),   QStringLiteral("c.sv"),
            QStringLiteral("crlf.v"),   QStringLiteral("notes.md"), QStringLiteral("unicode.v")};
}

void writeFixture(const QString &root)
{
    QDir().mkpath(root);
    writeText(QDir(root).filePath(QStringLiteral("a.v")),
              "module a;\n"
              "  wire foo;\n"
              "  assign bar = foo + foo;\n"
              "endmodule\n");
    writeText(QDir(root).filePath(QStringLiteral("b.sv")),
              "assign foo = 1;\n"
              "assign foo = 2;\n");
    writeText(QDir(root).filePath(QStringLiteral("c.sv")),
              "FOO\n"
              "foo_bar\n"
              "foo\n");
    // CRLF: the matcher strips the '\r' before matching, the replacement must
    // keep the separator.
    writeText(QDir(root).filePath(QStringLiteral("crlf.v")), "foo\r\nfoo\r\n");
    writeText(QDir(root).filePath(QStringLiteral("notes.md")), "foo in markdown\n");
    writeText(QDir(root).filePath(QStringLiteral("unicode.v")),
              "// \xE4\xB8\xAD\xE6\x96\x87 foo \xE6\xB3\xA8\xE9\x87\x8A\n");
}

// --- shared-model parity (i) ----------------------------------------------

void testSharedOptionModel(const QString &root)
{
    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    check(QStringLiteral("model.dock.widgets.case"),
          panel.findChild<QToolButton *>(QStringLiteral("search-case")) != nullptr);
    check(QStringLiteral("model.dock.widgets.whole"),
          panel.findChild<QToolButton *>(QStringLiteral("search-whole")) != nullptr);
    check(QStringLiteral("model.dock.widgets.regex"),
          panel.findChild<QToolButton *>(QStringLiteral("search-regex")) != nullptr);
    check(QStringLiteral("model.dock.widgets.scope"), panel.scopeCombo() != nullptr);
    check(QStringLiteral("model.dock.widgets.replace"),
          panel.findChild<QLineEdit *>(QStringLiteral("search-replacement")) != nullptr &&
              panel.findChild<QPushButton *>(QStringLiteral("search-replace")) != nullptr &&
              panel.findChild<QPushButton *>(QStringLiteral("search-replace-all")) != nullptr);

    // Every one of the four options round-trips through the dock's widgets.
    bool roundTrip = true;
    for (bool caseSensitive : {false, true}) {
        for (bool wholeWords : {false, true}) {
            for (bool regex : {false, true}) {
                for (SearchScope scope : {SearchScope::CurrentFile, SearchScope::WholeProject}) {
                    QueryOptions options;
                    options.caseSensitive = caseSensitive;
                    options.wholeWords = wholeWords;
                    options.regex = regex;
                    options.scope = scope;
                    panel.setQueryOptions(options);
                    if (panel.queryOptions() != options) {
                        roundTrip = false;
                    }
                }
            }
        }
    }
    check(QStringLiteral("model.dock.options.round-trip"), roundTrip);
    check(QStringLiteral("model.dock.scope.keys"),
          panel.scopeCombo()->count() == 2 &&
              panel.scopeCombo()->itemData(0).toInt() ==
                  static_cast<int>(SearchScope::CurrentFile) &&
              panel.scopeCombo()->itemData(1).toInt() ==
                  static_cast<int>(SearchScope::WholeProject));

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("model.bar.options"), QStringLiteral("katepart not available"));
        return;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    if (view == nullptr) {
        skip(QStringLiteral("model.bar.options"), QStringLiteral("no KTextEditor view"));
        delete document;
        return;
    }
    view->resize(700, 300);
    view->show();
    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    check(QStringLiteral("model.bar.installed"), bar != nullptr);
    if (bar != nullptr) {
        check(QStringLiteral("model.bar.widgets.case"),
              bar->findChild<QToolButton *>(QStringLiteral("vedit-find-case")) != nullptr);
        check(QStringLiteral("model.bar.widgets.whole"),
              bar->findChild<QToolButton *>(QStringLiteral("vedit-find-whole")) != nullptr);
        check(QStringLiteral("model.bar.widgets.regex"),
              bar->findChild<QToolButton *>(QStringLiteral("vedit-find-regex")) != nullptr);
        check(QStringLiteral("model.bar.widgets.scope"),
              bar->findChild<QComboBox *>(QStringLiteral("vedit-find-scope")) != nullptr);

        // Same model, same values: the bar adopts the dock's option set and
        // reads back exactly the same four fields. `applyHandoff` is the
        // widget-level adoption (no project search is started by it).
        bool barRoundTrip = true;
        QueryOptions sample;
        sample.caseSensitive = false;
        sample.wholeWords = true;
        sample.regex = true;
        sample.scope = SearchScope::WholeProject;
        vedit::search::SearchHandoff handoff;
        handoff.text = QStringLiteral("foo");
        handoff.options = sample;
        bar->applyHandoff(handoff);
        if (bar->queryOptions() != sample) {
            barRoundTrip = false;
        }
        // The two surfaces agree field by field for the same model value.
        panel.setQueryOptions(sample);
        if (panel.queryOptions() != bar->queryOptions()) {
            barRoundTrip = false;
        }
        check(QStringLiteral("model.both-surfaces.same-options"), barRoundTrip,
              QStringLiteral("dock=%1,%2,%3,%4 bar=%5,%6,%7,%8")
                  .arg(panel.queryOptions().caseSensitive)
                  .arg(panel.queryOptions().wholeWords)
                  .arg(panel.queryOptions().regex)
                  .arg(static_cast<int>(panel.queryOptions().scope))
                  .arg(bar->queryOptions().caseSensitive)
                  .arg(bar->queryOptions().wholeWords)
                  .arg(bar->queryOptions().regex)
                  .arg(static_cast<int>(bar->queryOptions().scope)));
    }
    delete view;
    delete document;
#else
    skip(QStringLiteral("model.bar.options"), QStringLiteral("no editor kernel in this build"));
#endif
}

// --- option semantics (iv) -------------------------------------------------

void setSearch(SearchPanel &panel, const QString &query, bool caseSensitive, bool wholeWords,
               bool regex)
{
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(query);
    panel.setCaseSensitive(caseSensitive);
    panel.setWholeWords(wholeWords);
    panel.setRegex(regex);
    panel.startSearch();
}

void testOptionSemantics(const QString &root)
{
    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    // Case-insensitive literal: 11 hits over 6 files (one row per line).
    setSearch(panel, QStringLiteral("foo"), false, false, false);
    check(QStringLiteral("options.search.finished"), waitForSearch(panel));
    check(QStringLiteral("options.case-insensitive.hits"), panel.hits().size() == 11,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    check(QStringLiteral("options.case-insensitive.files"), distinctFiles(panel).size() == 6);

    // Case-sensitive: c.sv line 1 "FOO" drops out → 10 hits.
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("options.case-sensitive.finished"), waitForSearch(panel));
    check(QStringLiteral("options.case-sensitive.hits"), panel.hits().size() == 10,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    // Case-sensitive ALL CAPS: only c.sv:1.
    setSearch(panel, QStringLiteral("FOO"), true, false, false);
    check(QStringLiteral("options.uppercase.finished"), waitForSearch(panel));
    check(QStringLiteral("options.uppercase.hits"), panel.hits().size() == 1,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    // Whole word (case-insensitive): "foo_bar" no longer matches → 10 hits.
    setSearch(panel, QStringLiteral("foo"), false, true, false);
    check(QStringLiteral("options.whole-word.finished"), waitForSearch(panel));
    check(QStringLiteral("options.whole-word.hits"), panel.hits().size() == 10,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    check(QStringLiteral("options.whole-word.excludes-underscore"),
          distinctFiles(panel).size() == 6);

    // Regex, case-sensitive: only the two b.sv lines start with "assign foo".
    setSearch(panel, QStringLiteral("^assign foo"), true, false, true);
    check(QStringLiteral("options.regex.finished"), waitForSearch(panel));
    check(QStringLiteral("options.regex.hits"),
          panel.hits().size() == 2 &&
              panel.hits().at(0).relativePath.endsWith(QStringLiteral("b.sv")),
          QStringLiteral("hits=%1 first=%2")
              .arg(panel.hits().size())
              .arg(panel.hits().isEmpty() ? QStringLiteral("-") : panel.hits().at(0).relativePath));

    // Character columns, not bytes: "// 中文 foo" puts foo at column 7.
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("options.unicode.finished"), waitForSearch(panel));
    int unicodeColumn = -1;
    for (const SearchHit &hit : panel.hits()) {
        if (hit.relativePath.endsWith(QStringLiteral("unicode.v"))) {
            unicodeColumn = hit.column;
        }
    }
    check(QStringLiteral("options.unicode.column"), unicodeColumn == 7,
          QStringLiteral("column=%1").arg(unicodeColumn));
}

// --- 「替换」: one occurrence, then advance (ii) ---------------------------

void testReplaceOne(const QString &root)
{
    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("replace-one.search.finished"), waitForSearch(panel));
    check(QStringLiteral("replace-one.hit.count"), panel.hits().size() == 10,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    // Hit 1 is a.v line 3 ("  assign bar = foo + foo;", first foo at column 16).
    check(QStringLiteral("replace-one.hit1.position"),
          panel.hits().size() > 1 &&
              panel.hits().at(1).relativePath.endsWith(QStringLiteral("a.v")) &&
              panel.hits().at(1).line == 3 && panel.hits().at(1).column == 16,
          panel.hits().size() > 1
              ? QStringLiteral("%1:%2:%3")
                    .arg(panel.hits().at(1).relativePath)
                    .arg(panel.hits().at(1).line)
                    .arg(panel.hits().at(1).column)
              : QStringLiteral("no hit"));

    panel.activateHit(1);
    panel.setReplaceText(QStringLiteral("BAR"));
    const QString aPath = QDir(root).filePath(QStringLiteral("a.v"));
    const QString before = readText(aPath);
    const int beforeFoo = countOccurrences(before, QStringLiteral("foo"), Qt::CaseSensitive);

    check(QStringLiteral("replace-one.start"), panel.replaceCurrentHit());
    check(QStringLiteral("replace-one.idle"), waitForPanelIdle(panel));

    const QString after = readText(aPath);
    check(QStringLiteral("replace-one.one-write"),
          countOccurrences(after, QStringLiteral("BAR"), Qt::CaseSensitive) == 1,
          QStringLiteral("BAR=%1")
              .arg(countOccurrences(after, QStringLiteral("BAR"), Qt::CaseSensitive)));
    check(QStringLiteral("replace-one.one-occurrence-removed"),
          countOccurrences(after, QStringLiteral("foo"), Qt::CaseSensitive) == beforeFoo - 1);
    check(QStringLiteral("replace-one.line-content"),
          after.contains(QStringLiteral("  assign bar = BAR + foo;")),
          after.section(QLatin1Char('\n'), 2, 2));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("replace-one.outcome"),
          outcome.ok && outcome.replacements == 1 && outcome.filesChanged == 1,
          QStringLiteral("ok=%1 replaced=%2 files=%3 error=%4")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.filesChanged)
              .arg(outcome.error));

    // The refreshed result list advanced to the NEXT match: a.v line 3 again,
    // now the second foo (column 22 after the 3-character replacement).
    check(QStringLiteral("replace-one.advances"),
          panel.currentHitIndex() == 1 && panel.hits().size() > 1 &&
              panel.hits().at(1).relativePath.endsWith(QStringLiteral("a.v")) &&
              panel.hits().at(1).line == 3 && panel.hits().at(1).column == 22,
          QStringLiteral("index=%1 hits=%2 %3:%4:%5")
              .arg(panel.currentHitIndex())
              .arg(panel.hits().size())
              .arg(panel.hits().size() > 1 ? panel.hits().at(1).relativePath : QStringLiteral("-"))
              .arg(panel.hits().size() > 1 ? panel.hits().at(1).line : -1)
              .arg(panel.hits().size() > 1 ? panel.hits().at(1).column : -1));
}

// --- 「全部替换」: counts, every file written once (iii) --------------------

void testReplaceAll(const QString &root, const QString &evidenceRoot)
{
    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    // Independent expected counts straight from the fixture bytes.
    int expectedHits = 0;
    for (const QString &name : fixtureFileNames()) {
        expectedHits += countOccurrences(readText(QDir(root).filePath(name)),
                                         QStringLiteral("foo"), Qt::CaseInsensitive);
    }
    check(QStringLiteral("replace-all.expected-count"), expectedHits == 12,
          QStringLiteral("expected=%1").arg(expectedHits));
    check(QStringLiteral("replace-all.file-count"), fixtureFileNames().size() == 6);

    if (!evidenceRoot.isEmpty()) {
        copyTree(root, QDir(evidenceRoot).filePath(QStringLiteral("before")));
        writeText(QDir(evidenceRoot).filePath(QStringLiteral("fixture-before.txt")),
                  fixtureListing(root, fixtureFileNames()).toUtf8());
    }

    setSearch(panel, QStringLiteral("foo"), false, false, false);
    check(QStringLiteral("replace-all.search.finished"), waitForSearch(panel));
    check(QStringLiteral("replace-all.browse-hits"), panel.hits().size() == 11,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    // Counting file writes: the injected rewriter wraps the REAL atomic save,
    // so the acceptance proves both "each file once" and the product path.
    struct WriteCounter {
        QMutex mutex;
        QHash<QString, int> perFile;
        int total = 0;
    };
    auto counter = std::make_shared<WriteCounter>();
    const FileRewriter realWriter = vedit::workspace::search::atomicSaveRewriter();
    check(QStringLiteral("replace-all.atomic-writer-available"), bool(realWriter));
    panel.setFileRewriter([counter, realWriter](const QString &path,
                                                const QByteArray &contents) {
        {
            QMutexLocker locker(&counter->mutex);
            counter->perFile[path] += 1;
            counter->total += 1;
        }
        return realWriter(path, contents);
    });

    ReplacePlan plan;
    bool confirmed = false;
    panel.setReplaceConfirmHandler([&plan, &confirmed](const ReplacePlan &candidate,
                                                       const QStringList &saved) {
        plan = candidate;
        confirmed = true;
        return saved.isEmpty();
    });
    panel.setReplaceText(QStringLiteral("qux"));

    check(QStringLiteral("replace-all.start"), panel.replaceAllRequested());
    check(QStringLiteral("replace-all.idle"), waitForPanelIdle(panel));
    check(QStringLiteral("replace-all.confirmed"), confirmed);
    check(QStringLiteral("replace-all.plan.counts"),
          plan.fileCount == 6 && plan.hitCount == expectedHits,
          QStringLiteral("files=%1 hits=%2").arg(plan.fileCount).arg(plan.hitCount));
    check(QStringLiteral("replace-all.plan.files"), plan.files.size() == 6);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("replace-all.outcome"),
          outcome.ok && outcome.replacements == expectedHits &&
              outcome.filesChanged == 6 && outcome.skippedFiles.isEmpty(),
          QStringLiteral("ok=%1 replaced=%2 files=%3 skipped=%4 error=%5")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.filesChanged)
              .arg(outcome.skippedFiles.size())
              .arg(outcome.error));

    int writtenOnce = 0;
    QStringList wrongCounts;
    {
        QMutexLocker locker(&counter->mutex);
        for (auto entry = counter->perFile.constBegin(); entry != counter->perFile.constEnd();
             ++entry) {
            if (entry.value() == 1) {
                ++writtenOnce;
            } else {
                wrongCounts.append(QStringLiteral("%1=%2")
                                       .arg(QFileInfo(entry.key()).fileName())
                                       .arg(entry.value()));
            }
        }
    }
    check(QStringLiteral("replace-all.each-file-once"),
          writtenOnce == 6 && counter->total == 6 && wrongCounts.isEmpty(),
          QStringLiteral("once=%1 total=%2 wrong=%3")
              .arg(writtenOnce)
              .arg(counter->total)
              .arg(wrongCounts.join(QLatin1Char(','))));

    // Disk: every occurrence replaced, nothing else changed.
    int remaining = 0;
    int replacementsOnDisk = 0;
    for (const QString &name : fixtureFileNames()) {
        const QString text = readText(QDir(root).filePath(name));
        remaining += countOccurrences(text, QStringLiteral("foo"), Qt::CaseInsensitive);
        replacementsOnDisk += countOccurrences(text, QStringLiteral("qux"), Qt::CaseSensitive);
    }
    check(QStringLiteral("replace-all.no-old-matches"), remaining == 0,
          QStringLiteral("remaining=%1").arg(remaining));
    check(QStringLiteral("replace-all.new-occurrences"),
          replacementsOnDisk == expectedHits,
          QStringLiteral("qux=%1").arg(replacementsOnDisk));
    check(QStringLiteral("replace-all.crlf-preserved"),
          readText(QDir(root).filePath(QStringLiteral("crlf.v"))) ==
              QStringLiteral("qux\r\nqux\r\n"));
    check(QStringLiteral("replace-all.unicode-preserved"),
          readText(QDir(root).filePath(QStringLiteral("unicode.v")))
              .contains(QString::fromUtf8("// \xE4\xB8\xAD\xE6\x96\x87 qux \xE6\xB3\xA8\xE9\x87\x8A")));

    // The result view was refreshed with the normal query: nothing left.
    check(QStringLiteral("replace-all.refreshed"), panel.hits().isEmpty(),
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    if (!evidenceRoot.isEmpty()) {
        copyTree(root, QDir(evidenceRoot).filePath(QStringLiteral("after")));
        writeText(QDir(evidenceRoot).filePath(QStringLiteral("fixture-after.txt")),
                  fixtureListing(root, fixtureFileNames()).toUtf8());
        QProcess diff;
        diff.setWorkingDirectory(evidenceRoot);
        diff.start(QStringLiteral("diff"),
                   {QStringLiteral("-ruN"), QStringLiteral("before"), QStringLiteral("after")});
        if (diff.waitForFinished(30000)) {
            writeText(QDir(evidenceRoot).filePath(QStringLiteral("before-after.diff")),
                      diff.readAllStandardOutput());
        }
    }
}

// --- Ctrl+F whole-project hand-off (v) -------------------------------------

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
void testHandoff(const QString &root)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("handoff.whole-project"), QStringLiteral("katepart not available"));
        return;
    }
    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        skip(QStringLiteral("handoff.whole-project"), QStringLiteral("no document"));
        return;
    }
    document->setText(QStringLiteral("foo foo foo\nsecond foo line\n"));
    KTextEditor::View *view = document->createView(nullptr);
    if (view == nullptr) {
        skip(QStringLiteral("handoff.whole-project"), QStringLiteral("no view"));
        delete document;
        return;
    }
    view->resize(700, 300);
    view->show();
    QCoreApplication::processEvents();
    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    if (bar == nullptr) {
        skip(QStringLiteral("handoff.whole-project"), QStringLiteral("no bar"));
        delete view;
        delete document;
        return;
    }
    // Ctrl+F: the bar renders matches only while it is open (the same path the
    // product takes); the in-file count proves it searched THIS document first.
    bar->openFind();
    QCoreApplication::processEvents();
    bar->setPattern(QStringLiteral("foo"));
    bar->setCaseSensitive(true);
    bar->setWholeWords(false);
    bar->setRegex(false);
    check(QStringLiteral("handoff.bar.in-file-first"), bar->matchCount() == 4,
          QStringLiteral("in-file matches=%1").arg(bar->matchCount()));

    // The USER path: choose "Whole Project" in the bar's selector.
    auto *scopeCombo = bar->findChild<QComboBox *>(QStringLiteral("vedit-find-scope"));
    check(QStringLiteral("handoff.bar.scope-widget"), scopeCombo != nullptr);
    if (scopeCombo != nullptr) {
        scopeCombo->setCurrentIndex(
            scopeCombo->findData(static_cast<int>(SearchScope::WholeProject)));
    }
    check(QStringLiteral("handoff.dock.finished"), waitForPanelIdle(panel));
    check(QStringLiteral("handoff.dock.query"), panel.query() == bar->pattern(),
          QStringLiteral("dock='%1' bar='%2'").arg(panel.query(), bar->pattern()));
    check(QStringLiteral("handoff.dock.options"), panel.queryOptions() == bar->queryOptions(),
          QStringLiteral("dock.case=%1 bar.case=%2 dock.scope=%3 bar.scope=%4")
              .arg(panel.queryOptions().caseSensitive)
              .arg(bar->queryOptions().caseSensitive)
              .arg(static_cast<int>(panel.queryOptions().scope))
              .arg(static_cast<int>(bar->queryOptions().scope)));
    check(QStringLiteral("handoff.dock.scope"), panel.scope() == SearchScope::WholeProject);
    // The dock really searched the PROJECT (11 case-insensitive hits; the
    // document alone only has 2 in-file matches).
    check(QStringLiteral("handoff.dock.project-results"), panel.hits().size() == 10,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    // The bar does not pretend to search the project inside one document.
    check(QStringLiteral("handoff.bar.no-in-file-project"),
          bar->matchCount() == 0 &&
              bar->statusText().contains(QStringLiteral("Whole project")),
          bar->statusText());

    // Two-way sync: a change in the dock reaches the bar's widgets.
    panel.setQuery(QStringLiteral("bar"));
    panel.setCaseSensitive(false);
    panel.setWholeWords(true);
    QCoreApplication::processEvents();
    check(QStringLiteral("handoff.sync.dock-to-bar.query"), bar->pattern() == QStringLiteral("bar"),
          bar->pattern());
    check(QStringLiteral("handoff.sync.dock-to-bar.options"),
          !bar->caseSensitive() && bar->wholeWords() && !bar->regex());

    delete view;
    delete document;
}
#else
void testHandoff(const QString &)
{
    skip(QStringLiteral("handoff.whole-project"), QStringLiteral("no editor kernel"));
}
#endif

// --- the PRODUCT wiring: the hand-off through installSearch() --------------
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE) && defined(VEDIT_SEARCH_HAVE_SHELL)
void testShellHandoff(const QString &root)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("shell-handoff.dock-created"),
             QStringLiteral("katepart not available"));
        return;
    }
    QTemporaryDir stateDir;
    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = stateDir.path();
    shellOptions.projectRoot = root;
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    vedit::app::ApplicationShell shell(shellOptions);
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = root;
    integration.stateDir = stateDir.path();
    vedit::workspace::installSearch(shell, integration);

    check(QStringLiteral("shell-handoff.dock-absent-before"),
          shell.findChild<SearchPanel *>() == nullptr);

    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    if (view == nullptr) {
        skip(QStringLiteral("shell-handoff.dock-created"), QStringLiteral("no view"));
        vedit::search::SearchHub::instance().removeProjectSearchHandler(&shell);
        delete document;
        return;
    }
    view->resize(600, 240);
    view->show();
    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    if (bar == nullptr) {
        skip(QStringLiteral("shell-handoff.dock-created"), QStringLiteral("no bar"));
        vedit::search::SearchHub::instance().removeProjectSearchHandler(&shell);
        delete view;
        delete document;
        return;
    }
    bar->setPattern(QStringLiteral("foo"));
    bar->setCaseSensitive(true);
    auto *scopeCombo = bar->findChild<QComboBox *>(QStringLiteral("vedit-find-scope"));
    if (scopeCombo != nullptr) {
        scopeCombo->setCurrentIndex(
            scopeCombo->findData(static_cast<int>(SearchScope::WholeProject)));
    }

    SearchPanel *dockPanel = nullptr;
    const bool created = waitUntil([&shell, &dockPanel]() {
        dockPanel = shell.findChild<SearchPanel *>();
        return dockPanel != nullptr;
    });
    check(QStringLiteral("shell-handoff.dock-created"), created && dockPanel != nullptr);
    if (dockPanel != nullptr) {
        waitForPanelIdle(*dockPanel);
        check(QStringLiteral("shell-handoff.dock.query-synced"),
              dockPanel->query() == bar->pattern(),
              QStringLiteral("dock='%1' bar='%2'").arg(dockPanel->query(), bar->pattern()));
        check(QStringLiteral("shell-handoff.dock.options-synced"),
              dockPanel->queryOptions() == bar->queryOptions());
        check(QStringLiteral("shell-handoff.dock.project-results"),
              !dockPanel->hits().isEmpty(),
              QStringLiteral("hits=%1").arg(dockPanel->hits().size()));
    }
    vedit::search::SearchHub::instance().removeProjectSearchHandler(&shell);
    delete view;
    delete document;
}
#else
void testShellHandoff(const QString &)
{
    skip(QStringLiteral("shell-handoff.dock-created"),
         QStringLiteral("no editor kernel or shell in this build"));
}
#endif

// --- unsaved editor buffer (vi) --------------------------------------------

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
namespace {

// A bridge that claims the document is open and modified but refuses to save
// it — the "cannot save, therefore refuse the replace" branch.
class RefusingBridge final : public OpenDocumentBridge
{
public:
    explicit RefusingBridge(QString path)
        : path_(std::move(path))
    {
    }

    QStringList modifiedPaths() const override { return {path_}; }
    bool isOpen(const QString &absolutePath) const override
    {
        return QFileInfo(absolutePath).absoluteFilePath() == path_;
    }
    bool save(const QString &, QString *error) override
    {
        if (error != nullptr) {
            *error = QStringLiteral("simulated write-protected buffer");
        }
        return false;
    }
    QString contentToken(const QString &) const override { return QString(); }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }

private:
    QString path_;
};

} // namespace

void testUnsavedBuffer(const QString &root)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("unsaved-buffer.saved-first"),
             QStringLiteral("katepart not available"));
        skip(QStringLiteral("unsaved-buffer.refused"), QStringLiteral("katepart not available"));
        return;
    }
    const QString aPath = QDir(root).filePath(QStringLiteral("a.v"));
    const QString original = readText(aPath);

    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    if (view == nullptr) {
        skip(QStringLiteral("unsaved-buffer.saved-first"), QStringLiteral("no view"));
        delete document;
        return;
    }
    view->resize(700, 300);
    view->show();
    check(QStringLiteral("unsaved-buffer.loaded"),
          vedit::editor::loadFileIntoDocument(document, aPath));
    document->setModified(false);
    view->setProperty("vedit.openFilePath", aPath);

    // The user's unsaved edit: a new line that only exists in the buffer.
    document->setText(original + QStringLiteral("// unsaved marker\n"));
    document->setModified(true);
    check(QStringLiteral("unsaved-buffer.modified"), document->isModified());

    SearchPanel panel;
    panel.setRootPath(root);
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), false, false, false);
    check(QStringLiteral("unsaved-buffer.search.finished"), waitForSearch(panel));

    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &saved) {
        return !saved.isEmpty();
    });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("unsaved-buffer.replace-started"), panel.replaceAllRequested());
    check(QStringLiteral("unsaved-buffer.idle"), waitForPanelIdle(panel));

    const QString saved = readText(aPath);
    check(QStringLiteral("unsaved-buffer.saved-before-replace"),
          saved.contains(QStringLiteral("// unsaved marker")),
          QStringLiteral("marker present=%1")
              .arg(saved.contains(QStringLiteral("// unsaved marker"))));
    check(QStringLiteral("unsaved-buffer.replacement-on-saved-text"),
          !saved.contains(QStringLiteral("foo"), Qt::CaseInsensitive) &&
              saved.contains(QStringLiteral("// unsaved marker")));
    check(QStringLiteral("unsaved-buffer.outcome-ok"), panel.lastReplaceOutcome().ok,
          panel.lastReplaceOutcome().error);
    check(QStringLiteral("unsaved-buffer.buffer-reloaded"),
          !document->isModified() && document->text().contains(QStringLiteral("qux")) &&
              document->text().contains(QStringLiteral("// unsaved marker")),
          QStringLiteral("modified=%1").arg(document->isModified()));

    // --- refusal branch: the modified buffer cannot be saved -----------------
    writeFixture(root); // restore the fixture
    document->setText(original + QStringLiteral("// unsaved marker 2\n"));
    document->setModified(true);
    RefusingBridge refusing(aPath);
    vedit::workspace::search::setOpenDocumentBridge(&refusing);
    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return true; });
    const QString diskBefore = readText(aPath);
    // The affected-buffer save now runs after the first counting scan (only
    // the documents the replace will touch are saved), so the refusal is
    // asynchronous; the bridge must stay installed until the operation ends.
    panel.replaceAllRequested();
    waitForPanelIdle(panel);
    vedit::workspace::search::setOpenDocumentBridge(nullptr);
    check(QStringLiteral("unsaved-buffer.refused"), !panel.lastReplaceOutcome().ok,
          panel.lastReplaceOutcome().error);
    check(QStringLiteral("unsaved-buffer.refused-error"),
          panel.lastReplaceOutcome().error.contains(QStringLiteral("refused")),
          panel.lastReplaceOutcome().error);
    check(QStringLiteral("unsaved-buffer.refused-no-write"), readText(aPath) == diskBefore);

    delete view;
    delete document;
}
#else
void testUnsavedBuffer(const QString &)
{
    skip(QStringLiteral("unsaved-buffer.saved-first"), QStringLiteral("no editor kernel"));
    skip(QStringLiteral("unsaved-buffer.refused"), QStringLiteral("no editor kernel"));
}
#endif

// --- regression tests for the review findings (b2-repair) -------------------
//
// Each of these FAILS on the reviewed tree and passes after the repair; they
// cover the data-safety edges the original 81 checks never exercised.

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

// High 3: a counting scan that stopped at the time budget must refuse
// 「全部替换」 instead of replacing the scanned prefix and presenting its count
// as the total.
void testReplaceAllTimeout()
{
    QTemporaryDir dir;
    check(QStringLiteral("replace-timeout.fixture"), dir.isValid());
    // "a.v" is scanned first (entries are sorted) and contributes two hits; the
    // big file then expires a 50 ms budget MID-SCAN, so the counting pass really
    // is a partial subset with a non-zero count.
    writeText(QDir(dir.path()).filePath(QStringLiteral("a.v")), QByteArray("foo\nfoo\n"));
    QByteArray big;
    big.reserve(1500000 * 4);
    for (int line = 0; line < 1500000; ++line) {
        big += "foo\n";
    }
    writeText(QDir(dir.path()).filePath(QStringLiteral("z-big.v")), big);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setResultLimit(2000000);
    panel.setTimeBudgetMs(50);
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    bool confirmed = false;
    panel.setReplaceConfirmHandler([&confirmed](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        return true;
    });
    panel.setReplaceText(QStringLiteral("qux"));

    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString path = QDir(dir.path()).filePath(QStringLiteral("z-big.v"));
    const qint64 before = QFileInfo(path).size();
    const QString aBefore = readText(aPath);
    check(QStringLiteral("replace-timeout.start"), panel.replaceAllRequested());
    check(QStringLiteral("replace-timeout.idle"), waitForPanelIdle(panel));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("replace-timeout.refused"), !outcome.ok && !confirmed,
          QStringLiteral("ok=%1 confirmed=%2 error=%3")
              .arg(outcome.ok)
              .arg(confirmed)
              .arg(outcome.error));
    check(QStringLiteral("replace-timeout.message"),
          outcome.error.contains(QStringLiteral("time budget"), Qt::CaseInsensitive) ||
              outcome.error.contains(QStringLiteral("timed out"), Qt::CaseInsensitive),
          outcome.error);
    check(QStringLiteral("replace-timeout.no-write"),
          QFileInfo(path).size() == before &&
              !readText(path).contains(QStringLiteral("qux")) &&
              readText(aPath) == aBefore);
}

// High 2 (single): an out-of-date range must be refused, never applied to the
// characters that now happen to sit at that offset.
void testStaleRangeOne()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("stale.v"));
    writeText(path, QByteArray("  assign bar = foo + foo;\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("stale-one.search"), waitForSearch(panel));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.relativePath.endsWith(QStringLiteral("stale.v")) && hit.line == 1 &&
            hit.column == 16) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("stale-one.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    // The file changes between the browse scan and 「替换」: the old range now
    // covers different text of the SAME length, so a bounds-only check passes.
    writeText(path, QByteArray("  assign bar = fix + foo;\n"));

    panel.activateHit(index);
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("stale-one.start"), panel.replaceCurrentHit());
    check(QStringLiteral("stale-one.idle"), waitForPanelIdle(panel));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("stale-one.refused"), !outcome.ok,
          QStringLiteral("ok=%1 replaced=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
    check(QStringLiteral("stale-one.error-names-file"),
          outcome.error.contains(QStringLiteral("stale.v")), outcome.error);
    check(QStringLiteral("stale-one.disk-unchanged"),
          readText(path) == QStringLiteral("  assign bar = fix + foo;\n"), readText(path));
}

// High 2 (batch): a file that changed between the counting scan and the write
// must be refused; files already written are reported, never hidden.
void testStaleRangeBatch()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString bPath = QDir(dir.path()).filePath(QStringLiteral("b.v"));
    writeText(aPath, QByteArray("foo foo\n"));
    writeText(bPath, QByteArray("foo foo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("stale-batch.search"), waitForSearch(panel));
    check(QStringLiteral("stale-batch.browse-hits"), panel.hits().size() == 2,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    bool edited = false;
    panel.setReplaceConfirmHandler([&edited, bPath](const ReplacePlan &, const QStringList &) {
        if (!edited) {
            edited = true;
            // External editor: same byte length, different text.
            writeText(bPath, QByteArray("bar bar\n"));
        }
        return true;
    });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("stale-batch.start"), panel.replaceAllRequested());
    check(QStringLiteral("stale-batch.idle"), waitForPanelIdle(panel));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("stale-batch.refused"), !outcome.ok && edited,
          QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
    check(QStringLiteral("stale-batch.error-names-file"),
          outcome.error.contains(QStringLiteral("b.v")) &&
              outcome.error.contains(QStringLiteral("line 1")),
          outcome.error);
    check(QStringLiteral("stale-batch.first-file-written"),
          readText(aPath) == QStringLiteral("qux qux\n"), readText(aPath));
    check(QStringLiteral("stale-batch.stale-file-untouched"),
          readText(bPath) == QStringLiteral("bar bar\n"), readText(bPath));
    check(QStringLiteral("stale-batch.counts-reported"),
          outcome.filesChanged == 1 && outcome.replacements == 2,
          QStringLiteral("files=%1 replacements=%2")
              .arg(outcome.filesChanged)
              .arg(outcome.replacements));
}

// High 4: a UTF-8 BOM is counted by the Rust matcher; the C++ replace path must
// use the SAME text (BOM kept) or line-1 columns land one character late.
void testUnicodeBom()
{
    QTemporaryDir dir;
    const QByteArray bom = QByteArray::fromHex("EFBBBF");
    const QString startPath = QDir(dir.path()).filePath(QStringLiteral("bom-start.v"));
    // The reviewer's corruption probe: a line-1 match at the very start.
    writeText(startPath, bom + QByteArray("foo a;\nfoo b;\n"));
    const QString midPath = QDir(dir.path()).filePath(QStringLiteral("bom-mid.v"));
    writeText(midPath, bom + QByteArray("module foo; // \xE4\xB8\xAD\n"));
    // Astral (non-BMP) text before the match: columns are CHARACTERS, not
    // UTF-16 code units.
    const QString astralPath = QDir(dir.path()).filePath(QStringLiteral("astral.v"));
    writeText(astralPath, QString::fromUtf8("\xF0\x9F\x98\x80 foo\n").toUtf8());

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("bom.search"), waitForSearch(panel));
    check(QStringLiteral("bom.hits"), panel.hits().size() == 4,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return true; });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("bom.start"), panel.replaceAllRequested());
    check(QStringLiteral("bom.idle"), waitForPanelIdle(panel));

    check(QStringLiteral("bom.start-bytes"),
          readBytes(startPath) == bom + QByteArray("BAR a;\nBAR b;\n"),
          QString::fromLatin1(readBytes(startPath).toHex()));
    check(QStringLiteral("bom.mid-bytes"),
          readBytes(midPath) == bom + QByteArray("module BAR; // \xE4\xB8\xAD\n"),
          QString::fromLatin1(readBytes(midPath).toHex()));
    check(QStringLiteral("bom.astral-bytes"),
          readBytes(astralPath) == QString::fromUtf8("\xF0\x9F\x98\x80 BAR\n").toUtf8(),
          QString::fromLatin1(readBytes(astralPath).toHex()));
    check(QStringLiteral("bom.outcome"),
          panel.lastReplaceOutcome().ok && panel.lastReplaceOutcome().filesChanged == 3,
          QStringLiteral("ok=%1 files=%2 error=%3")
              .arg(panel.lastReplaceOutcome().ok)
              .arg(panel.lastReplaceOutcome().filesChanged)
              .arg(panel.lastReplaceOutcome().error));
}

// Medium 6: a mid-batch failure must still report the counts already written
// (and the range errors must name their file).
void testMidBatchFailure()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString bPath = QDir(dir.path()).filePath(QStringLiteral("b.v"));
    writeText(aPath, QByteArray("foo foo\n"));
    writeText(bPath, QByteArray("foo foo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("mid-batch.search"), waitForSearch(panel));

    const FileRewriter realWriter = vedit::workspace::search::atomicSaveRewriter();
    check(QStringLiteral("mid-batch.atomic-writer"), bool(realWriter));
    panel.setFileRewriter([realWriter](const QString &path, const QByteArray &contents) {
        if (path.endsWith(QStringLiteral("b.v"))) {
            return ReplaceWriteResult{false, QStringLiteral("simulated write failure")};
        }
        return realWriter(path, contents);
    });
    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return true; });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("mid-batch.start"), panel.replaceAllRequested());
    check(QStringLiteral("mid-batch.idle"), waitForPanelIdle(panel));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("mid-batch.partial-outcome"),
          !outcome.ok && outcome.filesChanged == 1 && outcome.replacements == 2,
          QStringLiteral("ok=%1 files=%2 replacements=%3 error=%4")
              .arg(outcome.ok)
              .arg(outcome.filesChanged)
              .arg(outcome.replacements)
              .arg(outcome.error));
    const QString status = panel.replaceStatusLabel()->text();
    check(QStringLiteral("mid-batch.status-counts"),
          status.contains(QStringLiteral("2 occurrence")) &&
              status.contains(QStringLiteral("1 file")),
          status);
    check(QStringLiteral("mid-batch.status-file"),
          status.contains(QStringLiteral("b.v")), status);

    // The range error itself names the file, not only a line number.
    const vedit::workspace::search::TextReplaceResult applied =
        vedit::workspace::search::applyReplaceRanges(
            QStringLiteral("foo\n"),
            QList<vedit::workspace::search::ReplaceHit>{
                vedit::workspace::search::ReplaceHit{QStringLiteral("/tmp/direct.v"), 1, 5, 3, 0,
                                                     QByteArray()}},
            QStringLiteral("x"));
    check(QStringLiteral("mid-batch.short-line-names-file"),
          !applied.ok && applied.error.contains(QStringLiteral("direct.v")), applied.error);
}

// Medium 7: 「替换」 acts on the row the user SELECTED (keyboard navigation),
// not on the last clicked hit / the first one.
void testSelectedRowReplace()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("rows.v"));
    writeText(path, QByteArray("foo\nfoo\nfoo\nfoo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("selected-row.search"), waitForSearch(panel));

    QTreeWidget *tree = panel.resultsView();
    check(QStringLiteral("selected-row.tree-count"),
          tree != nullptr && tree->topLevelItemCount() == 1,
          QStringLiteral("top=%1").arg(tree != nullptr ? tree->topLevelItemCount() : -1));
    QTreeWidgetItem *row = tree != nullptr && tree->topLevelItemCount() > 0
                               ? tree->topLevelItem(0)->child(2)
                               : nullptr;
    check(QStringLiteral("selected-row.third-item"), row != nullptr);
    if (row == nullptr) {
        return;
    }
    // What arrow-key navigation leaves behind: a current item, no click.
    tree->setCurrentItem(row);
    QCoreApplication::processEvents();

    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("selected-row.start"), panel.replaceCurrentHit());
    check(QStringLiteral("selected-row.idle"), waitForPanelIdle(panel));
    check(QStringLiteral("selected-row.third-line"),
          readText(path) == QStringLiteral("foo\nfoo\nBAR\nfoo\n"), readText(path));
}

// High 2 coverage for the validator's two non-literal branches: a regex
// replacement and a whole-word replacement pass the same range re-check the
// literal path uses (never a false refusal on valid ranges).
void testRegexWholeWordReplace()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("regex.v"));
    writeText(path, QByteArray("abc 123 abc 456\nfoo_bar foo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();

    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("a.c"));
    panel.setCaseSensitive(false);
    panel.setWholeWords(false);
    panel.setRegex(true);
    panel.startSearch();
    check(QStringLiteral("regex-replace.search"), waitForSearch(panel));
    // The browse path keeps one row per LINE (both matches share line 1); the
    // all-matches counting pass below sees both occurrences.
    check(QStringLiteral("regex-replace.browse-hits"), panel.hits().size() == 1,
          QStringLiteral("hits=%1").arg(panel.hits().size()));

    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return true; });
    panel.setReplaceText(QStringLiteral("Z"));
    check(QStringLiteral("regex-replace.start"), panel.replaceAllRequested());
    check(QStringLiteral("regex-replace.idle"), waitForPanelIdle(panel));
    check(QStringLiteral("regex-replace.outcome"),
          panel.lastReplaceOutcome().ok && panel.lastReplaceOutcome().replacements == 2,
          panel.lastReplaceOutcome().error);
    check(QStringLiteral("regex-replace.content"),
          readText(path) == QStringLiteral("Z 123 Z 456\nfoo_bar foo\n"), readText(path));

    // Whole word: only the standalone "foo" is a match, never "foo_bar".
    panel.setRegex(false);
    panel.setWholeWords(true);
    panel.setCaseSensitive(true);
    panel.setQuery(QStringLiteral("foo"));
    panel.startSearch();
    check(QStringLiteral("whole-word-replace.search"), waitForSearch(panel));
    check(QStringLiteral("whole-word-replace.hits"), panel.hits().size() == 1,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("whole-word-replace.start"), panel.replaceAllRequested());
    check(QStringLiteral("whole-word-replace.idle"), waitForPanelIdle(panel));
    check(QStringLiteral("whole-word-replace.outcome"),
          panel.lastReplaceOutcome().ok && panel.lastReplaceOutcome().replacements == 1,
          panel.lastReplaceOutcome().error);
    check(QStringLiteral("whole-word-replace.content"),
          readText(path) == QStringLiteral("Z 123 Z 456\nfoo_bar BAR\n"), readText(path));
}

// Medium 8: opening the dock must not publish its defaults over a live Ctrl+F
// bar; a newly created surface adopts the hub's current snapshot instead.
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
void testDockOpenKeepsBar()
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("dock-open.bar-query-preserved"), QStringLiteral("katepart not available"));
        return;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    if (view == nullptr) {
        skip(QStringLiteral("dock-open.bar-query-preserved"), QStringLiteral("no KTextEditor view"));
        delete document;
        return;
    }
    view->resize(700, 300);
    view->show();
    document->setText(QStringLiteral("foo foo\n"));
    vedit::editor::FindReplaceBar *bar = vedit::editor::FindReplaceBar::install(view);
    if (bar == nullptr) {
        skip(QStringLiteral("dock-open.bar-query-preserved"), QStringLiteral("no bar"));
        delete view;
        delete document;
        return;
    }
    bar->openFind();
    QCoreApplication::processEvents();
    auto *queryEdit = bar->findChild<QLineEdit *>(QStringLiteral("vedit-find-pattern"));
    check(QStringLiteral("dock-open.bar-query-widget"), queryEdit != nullptr);
    if (queryEdit == nullptr) {
        delete view;
        delete document;
        return;
    }
    queryEdit->setText(QStringLiteral("foo"));
    bar->setCaseSensitive(true);
    bar->setWholeWords(true);
    QCoreApplication::processEvents();
    const QString liveQuery = bar->pattern();
    const QueryOptions liveOptions = bar->queryOptions();
    const int liveMatches = bar->matchCount();
    check(QStringLiteral("dock-open.bar-prepared"),
          liveQuery == QStringLiteral("foo") && liveMatches == 2,
          QStringLiteral("query='%1' matches=%2").arg(liveQuery).arg(liveMatches));

    QTemporaryDir dir;
    // Opening the dock is exactly "construct the panel"; the old constructor
    // published its defaults and wiped the live bar.
    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.show();
    QCoreApplication::processEvents();

    check(QStringLiteral("dock-open.bar-query-preserved"), bar->pattern() == liveQuery,
          QStringLiteral("bar='%1' dock='%2'").arg(bar->pattern(), panel.query()));
    check(QStringLiteral("dock-open.bar-options-preserved"), bar->queryOptions() == liveOptions);
    check(QStringLiteral("dock-open.bar-matches-preserved"), bar->matchCount() == liveMatches,
          QStringLiteral("matches=%1").arg(bar->matchCount()));
    check(QStringLiteral("dock-open.dock-adopts-bar"),
          panel.query() == liveQuery && panel.queryOptions() == liveOptions,
          QStringLiteral("dock='%1'").arg(panel.query()));

    delete view;
    delete document;
}
#else
void testDockOpenKeepsBar()
{
    skip(QStringLiteral("dock-open.bar-query-preserved"), QStringLiteral("no editor kernel"));
}
#endif

// Medium 9: save-first saves only the modified documents the replace will
// actually touch, and a refused save states that earlier saves persist.
QStringList absolutePaths(const QStringList &paths)
{
    QStringList absolute;
    for (const QString &path : paths) {
        absolute.append(QFileInfo(path).absoluteFilePath());
    }
    absolute.sort();
    return absolute;
}

class RecordingBridge final : public OpenDocumentBridge
{
public:
    RecordingBridge(QStringList modified, QString failOn)
        : modified_(absolutePaths(modified))
        , failOn_(failOn.isEmpty() ? QString() : QFileInfo(failOn).absoluteFilePath())
    {
    }

    QStringList modifiedPaths() const override { return modified_; }
    bool isOpen(const QString &absolutePath) const override
    {
        return modified_.contains(QFileInfo(absolutePath).absoluteFilePath());
    }
    bool save(const QString &absolutePath, QString *error) override
    {
        const QString path = QFileInfo(absolutePath).absoluteFilePath();
        saved_.append(path);
        if (!failOn_.isEmpty() && path == failOn_) {
            if (error != nullptr) {
                *error = QStringLiteral("simulated write-protected buffer");
            }
            return false;
        }
        return true;
    }
    QString contentToken(const QString &) const override { return QString(); }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }

    QStringList saved() const { return saved_; }

private:
    QStringList modified_;
    QString failOn_;
    QStringList saved_;
};

void testSaveOnlyAffectedDocuments()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString bPath = QDir(dir.path()).filePath(QStringLiteral("b.v"));
    writeText(aPath, QByteArray("foo foo\n"));
    writeText(bPath, QByteArray("bar\n"));

    RecordingBridge bridge({aPath, bPath}, QString());
    vedit::workspace::search::setOpenDocumentBridge(&bridge);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("save-only.search"), waitForSearch(panel));

    QStringList confirmedSaved;
    panel.setReplaceConfirmHandler(
        [&confirmedSaved](const ReplacePlan &, const QStringList &saved) {
            confirmedSaved = saved;
            return true;
        });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("save-only.start"), panel.replaceAllRequested());
    check(QStringLiteral("save-only.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("save-only.outcome"),
          outcome.ok && outcome.filesChanged == 1, outcome.error);
    check(QStringLiteral("save-only.only-affected-saved"),
          bridge.saved() == QStringList{QFileInfo(aPath).absoluteFilePath()},
          bridge.saved().join(QStringLiteral(",")));
    check(QStringLiteral("save-only.confirm-list"),
          confirmedSaved == QStringList{QFileInfo(aPath).absoluteFilePath()},
          confirmedSaved.join(QStringLiteral(",")));
    check(QStringLiteral("save-only.unaffected-untouched"),
          readText(bPath) == QStringLiteral("bar\n"), readText(bPath));
    check(QStringLiteral("save-only.replaced"),
          readText(aPath) == QStringLiteral("qux qux\n"), readText(aPath));
}

void testSaveFailureWording()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString cPath = QDir(dir.path()).filePath(QStringLiteral("c.v"));
    writeText(aPath, QByteArray("foo\n"));
    writeText(cPath, QByteArray("foo\n"));

    RecordingBridge bridge({aPath, cPath}, cPath);
    vedit::workspace::search::setOpenDocumentBridge(&bridge);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("save-failure.search"), waitForSearch(panel));

    bool confirmed = false;
    panel.setReplaceConfirmHandler([&confirmed](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        return true;
    });
    panel.setReplaceText(QStringLiteral("qux"));
    const bool started = panel.replaceAllRequested();
    check(QStringLiteral("save-failure.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("save-failure.refused"), !confirmed && !outcome.ok,
          QStringLiteral("started=%1 ok=%2 confirmed=%3 error=%4")
              .arg(started)
              .arg(outcome.ok)
              .arg(confirmed)
              .arg(outcome.error));
    check(QStringLiteral("save-failure.names-file"), outcome.error.contains(QStringLiteral("c.v")),
          outcome.error);
    check(QStringLiteral("save-failure.earlier-saves-remain"),
          outcome.error.contains(QStringLiteral("remain on disk")), outcome.error);
    check(QStringLiteral("save-failure.no-replace"),
          readText(aPath) == QStringLiteral("foo\n") && readText(cPath) == QStringLiteral("foo\n"));
}

// Blocker 1: edits typed after the save-first step survive the batch; the
// buffer is NOT reloaded and the user is told.
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
void testEditDuringReplace()
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("edit-during-replace.edit-survives"),
             QStringLiteral("katepart not available"));
        return;
    }
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("live.v"));
    writeText(path, QByteArray("foo foo\n"));

    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document != nullptr ? document->createView(nullptr) : nullptr;
    if (view == nullptr) {
        skip(QStringLiteral("edit-during-replace.edit-survives"), QStringLiteral("no view"));
        delete document;
        return;
    }
    view->resize(700, 300);
    view->show();
    check(QStringLiteral("edit-during-replace.loaded"),
          vedit::editor::loadFileIntoDocument(document, path));
    document->setModified(false);
    view->setProperty("vedit.openFilePath", path);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("edit-during-replace.search"), waitForSearch(panel));

    bool typed = false;
    panel.setReplaceConfirmHandler(
        [&typed, document](const ReplacePlan &, const QStringList &) {
            // The user keeps typing while the batch is running: after the
            // save-first step, before the reload.
            document->setText(document->text() + QStringLiteral("// typed during replace\n"));
            document->setModified(true);
            typed = true;
            return true;
        });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("edit-during-replace.start"), panel.replaceAllRequested());
    check(QStringLiteral("edit-during-replace.idle"), waitForPanelIdle(panel));
    check(QStringLiteral("edit-during-replace.typed"), typed);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("edit-during-replace.disk-replaced"),
          outcome.ok && readText(path) == QStringLiteral("qux qux\n"),
          QStringLiteral("ok=%1 disk='%2' error=%3")
              .arg(outcome.ok)
              .arg(readText(path))
              .arg(outcome.error));
    check(QStringLiteral("edit-during-replace.edit-survives"),
          document->isModified() &&
              document->text().contains(QStringLiteral("// typed during replace")),
          QStringLiteral("modified=%1 text='%2'")
              .arg(document->isModified())
              .arg(document->text().left(80)));
    const QString status = panel.replaceStatusLabel()->text();
    check(QStringLiteral("edit-during-replace.reported"),
          status.contains(QStringLiteral("not reloaded"), Qt::CaseInsensitive), status);
    check(QStringLiteral("edit-during-replace.outcome-lists-file"),
          outcome.notReloadedFiles.contains(QFileInfo(path).absoluteFilePath()),
          outcome.notReloadedFiles.join(QStringLiteral(",")));

    delete view;
    delete document;
}
#else
void testEditDuringReplace()
{
    skip(QStringLiteral("edit-during-replace.edit-survives"), QStringLiteral("no editor kernel"));
}
#endif

// --- b2-repair-r2: the operation's query is frozen at scan time (High 2) ----
//
// The four checks below are the re-review's F3/E3/D5/C4 probes at panel level:
// the counting scan is produced with one query/options snapshot, then the user
// changes the query or the options while the operation is still in flight. The
// operation must refuse and must never write a range that belongs to an older
// query or an older document state.

// A fake open buffer whose save() writes the text the user now sees (the
// reviewer's ShiftedBridge). Used to prove that a line-shift save between the
// scan and 「替换」 is refused instead of replacing a different occurrence.
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
        writeText(path_, savedText_);
        ++saves;
        return true;
    }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    QString bufferText(const QString &absolutePath) const override
    {
        // The text the user sees (what a real KTextEditor document holds before
        // the panel saves it), so the pre-save identity check can inspect it.
        return QFileInfo(absolutePath).absoluteFilePath() == path_
                   ? QString::fromUtf8(savedText_)
                   : QString();
    }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }

    int saves = 0;

private:
    QString path_;
    QByteArray savedText_;
};

// (a) F3: the whole-word toggle is cleared while the counting scan runs, then
// the user saves "foo_bar" at the scanned range. The scanned snapshot was
// whole-word "foo", so the operation must refuse and leave the file untouched;
// validating against the live literal option would write "BAR_bar".
void testFrozenOptionDuringCountingScan()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("whole.v"));
    const QByteArray before = QByteArray("foo\n");
    writeText(path, before);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.setWholeWords(true);

    bool changed = false;
    bool controlsDisabled = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&panel, &changed, &controlsDisabled](const QList<SearchHit> &) {
            if (changed) {
                return;
            }
            changed = true;
            // Item 3: the window must not be widened while the count runs.
            controlsDisabled =
                !panel.queryEdit()->isEnabled() && !panel.wholeWordButton()->isEnabled();
            // Programmatic, so it works even though the widget is disabled for
            // the duration of the counting scan.
            panel.setWholeWords(false);
        });
    bool confirmed = false;
    panel.setReplaceConfirmHandler([&confirmed, path](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        // The user's save during the count: F3's "foo_bar" input.
        writeText(path, QByteArray("foo_bar\n"));
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("frozen-option.start"), panel.replaceAllRequested());
    check(QStringLiteral("frozen-option.idle"), waitForPanelIdle(panel));
    QObject::disconnect(hook);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("frozen-option.changed-during-scan"), changed);
    check(QStringLiteral("frozen-option.controls-disabled-during-scan"), controlsDisabled);
    check(QStringLiteral("frozen-option.refused"), !outcome.ok && !confirmed,
          QStringLiteral("ok=%1 confirmed=%2 error=%3")
              .arg(outcome.ok)
              .arg(confirmed)
              .arg(outcome.error));
    check(QStringLiteral("frozen-option.no-write"), readBytes(path) == before,
          QString::fromLatin1(readBytes(path)));
    check(QStringLiteral("frozen-option.query-controls-restored"), panel.queryEdit()->isEnabled(),
          QStringLiteral("enabled=%1").arg(panel.queryEdit()->isEnabled()));
}

// (b) E3: the query is cleared while the counting scan runs and the user saves
// "fix fix" at the scanned ranges. An empty live query must not mean "no range
// validation"; the operation refuses before anything is written.
void testFrozenEmptyQueryDuringCountingScan()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("batch.v"));
    const QByteArray before = QByteArray("foo foo\n");
    writeText(path, before);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    bool changed = false;
    bool controlsDisabled = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&panel, &changed, &controlsDisabled](const QList<SearchHit> &) {
            if (changed) {
                return;
            }
            changed = true;
            controlsDisabled = !panel.queryEdit()->isEnabled();
            panel.setQuery(QString());
        });
    bool confirmed = false;
    panel.setReplaceConfirmHandler([&confirmed, path](const ReplacePlan &, const QStringList &) {
        confirmed = true;
        writeText(path, QByteArray("fix fix\n"));
        return true;
    });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("frozen-empty.start"), panel.replaceAllRequested());
    check(QStringLiteral("frozen-empty.idle"), waitForPanelIdle(panel));
    QObject::disconnect(hook);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("frozen-empty.changed-during-scan"), changed);
    check(QStringLiteral("frozen-empty.query-disabled-during-scan"), controlsDisabled);
    check(QStringLiteral("frozen-empty.refused"), !outcome.ok && !confirmed,
          QStringLiteral("ok=%1 confirmed=%2 error=%3")
              .arg(outcome.ok)
              .arg(confirmed)
              .arg(outcome.error));
    check(QStringLiteral("frozen-empty.no-write"), readBytes(path) == before,
          QString::fromLatin1(readBytes(path)));
}

// (c) D5: after the query is cleared (no new search), a single 「替换」 must not
// act on the stale row and must not even save the edited buffer.
void testSingleReplaceAfterQueryCleared()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("stale.v"));
    const QByteArray before = QByteArray("  assign bar = foo + foo;\n");
    writeText(path, before);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("frozen-single.search"), waitForSearch(panel));
    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 1 && hit.column == 16) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("frozen-single.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    SavingBridge bridge(path, QByteArray("  assign bar = fix + foo;\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setQuery(QString());
    check(QStringLiteral("frozen-single.query-cleared"),
          panel.query().isEmpty() && panel.hits().isEmpty(),
          QStringLiteral("query='%1' hits=%2").arg(panel.query()).arg(panel.hits().size()));
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(QStringLiteral("frozen-single.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    check(QStringLiteral("frozen-single.no-write"), readBytes(path) == before,
          QString::fromLatin1(readBytes(path)));
    check(QStringLiteral("frozen-single.no-save"), bridge.saves == 0,
          QStringLiteral("saves=%1").arg(bridge.saves));
    check(QStringLiteral("frozen-single.refused"),
          !started || !panel.lastReplaceOutcome().ok,
          QStringLiteral("started=%1 ok=%2").arg(started).arg(panel.lastReplaceOutcome().ok));
}

// (d) C4: a line-shift edit between the scan and 「替换」 must refuse, not
// replace the different occurrence that now sits at the old line/column.
void testLineShiftRefused()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("shift.v"));
    const QByteArray before = QByteArray("foo\nabc foo\nxyz foo\n");
    writeText(path, before);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    setSearch(panel, QStringLiteral("foo"), true, false, false);
    check(QStringLiteral("frozen-shift.search"), waitForSearch(panel));
    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 2 && hit.column == 5) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("frozen-shift.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The user deletes line 1 above the selected hit; the save-first step
    // writes that edited text before the range is applied.
    SavingBridge bridge(path, QByteArray("abc foo\nxyz foo\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(QStringLiteral("frozen-shift.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("frozen-shift.started"), started);
    check(QStringLiteral("frozen-shift.refused"), !outcome.ok,
          QStringLiteral("ok=%1 replacements=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
    check(QStringLiteral("frozen-shift.no-wrong-occurrence"),
          !readText(path).contains(QStringLiteral("BAR")),
          readText(path));
    check(QStringLiteral("frozen-shift.error-names-file"),
          outcome.error.contains(QStringLiteral("shift.v")), outcome.error);
}

// Residual (Medium 9 scope): the post-save re-scan must keep the scope and file
// filter frozen at the request; it must not re-resolve CurrentFile and silently
// widen to the whole project.
void testPostSaveScopeFrozen()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    const QString bPath = QDir(dir.path()).filePath(QStringLiteral("b.v"));
    writeText(aPath, QByteArray("foo\n"));
    writeText(bPath, QByteArray("foo\n"));

    RecordingBridge bridge({aPath}, QString());
    vedit::workspace::search::setOpenDocumentBridge(&bridge);

    QString activeFile = aPath;
    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setCurrentFileProvider([&activeFile]() { return activeFile; });
    panel.setScope(SearchScope::CurrentFile);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    bool lostOnce = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel, [&activeFile, &lostOnce]() {
            if (lostOnce) {
                return;
            }
            lostOnce = true;
            // The active pane loses its document between the initial scan and
            // the post-save scan.
            activeFile.clear();
        });
    QStringList plannedFiles;
    panel.setReplaceConfirmHandler(
        [&plannedFiles, &activeFile, aPath](const ReplacePlan &plan, const QStringList &) {
            plannedFiles = plan.files;
            activeFile = aPath; // the pane is back before the write gate
            return true;
        });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("post-save-scope.start"), panel.replaceAllRequested());
    check(QStringLiteral("post-save-scope.idle"), waitForPanelIdle(panel));
    QObject::disconnect(hook);
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    check(QStringLiteral("post-save-scope.lost-active"), lostOnce);
    check(QStringLiteral("post-save-scope.plan"),
          plannedFiles == QStringList{QFileInfo(aPath).absoluteFilePath()},
          plannedFiles.join(QStringLiteral(",")));
    check(QStringLiteral("post-save-scope.no-widen"),
          readText(bPath) == QStringLiteral("foo\n"), readText(bPath));
    check(QStringLiteral("post-save-scope.written"),
          readText(aPath) == QStringLiteral("qux\n"), readText(aPath));
}

// Residual (Medium 9 wording): cancelling after the affected buffers were saved
// must say so; "nothing was written" contradicts the disk state.
void testCancelWordingAfterSave()
{
    QTemporaryDir dir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("a.v"));
    writeText(aPath, QByteArray("foo\n"));

    RecordingBridge bridge({aPath}, QString());
    vedit::workspace::search::setOpenDocumentBridge(&bridge);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return false; });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("cancel-wording.start"), panel.replaceAllRequested());
    check(QStringLiteral("cancel-wording.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const QString status = panel.replaceStatusLabel()->text();
    check(QStringLiteral("cancel-wording.cancelled"), panel.lastReplaceOutcome().cancelled);
    check(QStringLiteral("cancel-wording.saved-named"),
          status.contains(QStringLiteral("saved"), Qt::CaseInsensitive), status);
    check(QStringLiteral("cancel-wording.nothing-replaced"),
          status.contains(QStringLiteral("nothing was replaced"), Qt::CaseInsensitive), status);
    check(QStringLiteral("cancel-wording.no-replace"),
          readText(aPath) == QStringLiteral("foo\n"), readText(aPath));
}

// Residual: a match that exists only in the unsaved buffer of a modified open
// document must be saved and re-scanned, not silently skipped because its disk
// copy has no hit.
class BufferOnlyBridge final : public OpenDocumentBridge
{
public:
    BufferOnlyBridge(const QString &path, const QByteArray &buffer)
        : path_(QFileInfo(path).absoluteFilePath())
        , buffer_(QString::fromUtf8(buffer))
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
        writeText(path_, buffer_.toUtf8());
        ++saves;
        return true;
    }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    QString bufferText(const QString &absolutePath) const override
    {
        return QFileInfo(absolutePath).absoluteFilePath() == path_ ? buffer_ : QString();
    }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }

    int saves = 0;

private:
    QString path_;
    QString buffer_;
};

void testBufferOnlyMatchIsSavedAndReplaced()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("buffer-only.v"));
    writeText(path, QByteArray("no match on disk\n"));
    BufferOnlyBridge bridge(path, QByteArray("assign foo = 1;\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    int plannedHits = -1;
    panel.setReplaceConfirmHandler(
        [&plannedHits](const ReplacePlan &plan, const QStringList &) {
            plannedHits = plan.hitCount;
            return true;
        });
    panel.setReplaceText(QStringLiteral("qux"));
    check(QStringLiteral("buffer-only.start"), panel.replaceAllRequested());
    check(QStringLiteral("buffer-only.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("buffer-only.saved"), bridge.saves == 1,
          QStringLiteral("saves=%1").arg(bridge.saves));
    check(QStringLiteral("buffer-only.counted"), plannedHits == 1,
          QStringLiteral("planned=%1").arg(plannedHits));
    check(QStringLiteral("buffer-only.replaced"),
          readText(path) == QStringLiteral("assign qux = 1;\n"), readText(path));
    check(QStringLiteral("buffer-only.outcome"), outcome.ok && outcome.replacements == 1,
          QStringLiteral("ok=%1 replacements=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
}

// The reviewer's F3/E3 probes change the query/option in the CONFIRMATION
// handler (after both counting scans). That is a later code point than the
// hitsReady hooks above; the post-confirmation identity gate must refuse there
// too, with the user's own save kept and no replacement written.
void testConfirmWindowChangeRefused()
{
    // E3 timing: query cleared in the confirmation.
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("confirm-empty.v"));
        writeText(path, QByteArray("foo foo\n"));
        SearchPanel panel;
        panel.setRootPath(dir.path());
        panel.resize(900, 500);
        panel.show();
        QCoreApplication::processEvents();
        panel.setScope(SearchScope::WholeProject);
        panel.setQuery(QStringLiteral("foo"));
        panel.setCaseSensitive(true);
        panel.setReplaceConfirmHandler(
            [&panel, path](const ReplacePlan &, const QStringList &) {
                panel.setQuery(QString());
                writeText(path, QByteArray("fix fix\n")); // the user's save
                return true;
            });
        panel.setReplaceText(QStringLiteral("BAR"));
        check(QStringLiteral("confirm-window.empty.start"), panel.replaceAllRequested());
        check(QStringLiteral("confirm-window.empty.idle"), waitForPanelIdle(panel));

        const ReplaceOutcome outcome = panel.lastReplaceOutcome();
        check(QStringLiteral("confirm-window.empty.refused"), !outcome.ok,
              QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
        check(QStringLiteral("confirm-window.empty.user-save-kept"),
              readText(path) == QStringLiteral("fix fix\n"), readText(path));
        check(QStringLiteral("confirm-window.empty.no-replacement"),
              !readText(path).contains(QStringLiteral("BAR")), readText(path));
    }
    // F3 timing: whole-word cleared in the confirmation.
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("confirm-option.v"));
        writeText(path, QByteArray("foo\n"));
        SearchPanel panel;
        panel.setRootPath(dir.path());
        panel.resize(900, 500);
        panel.show();
        QCoreApplication::processEvents();
        panel.setScope(SearchScope::WholeProject);
        panel.setQuery(QStringLiteral("foo"));
        panel.setCaseSensitive(true);
        panel.setWholeWords(true);
        panel.setReplaceConfirmHandler(
            [&panel, path](const ReplacePlan &, const QStringList &) {
                panel.setWholeWords(false);
                writeText(path, QByteArray("foo_bar\n")); // the user's save
                return true;
            });
        panel.setReplaceText(QStringLiteral("BAR"));
        check(QStringLiteral("confirm-window.option.start"), panel.replaceAllRequested());
        check(QStringLiteral("confirm-window.option.idle"), waitForPanelIdle(panel));

        const ReplaceOutcome outcome = panel.lastReplaceOutcome();
        check(QStringLiteral("confirm-window.option.refused"), !outcome.ok,
              QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
        check(QStringLiteral("confirm-window.option.user-save-kept"),
              readText(path) == QStringLiteral("foo_bar\n"), readText(path));
        check(QStringLiteral("confirm-window.option.no-replacement"),
              !readText(path).contains(QStringLiteral("BAR")), readText(path));
    }
}

// --- b2-repair-r3: the scan window, the hand-off and the save-first step -----

// (1) Blocker (review probes N1/N1b): the per-file identity used to be captured
// when the scan FINISHED, so a save that landed after the walk but before that
// capture was baked into the baseline — the hits described the pre-save file
// while the stamps described the post-save one, and the replacement went to the
// wrong occurrence with a success status. The panel must refuse and leave the
// just-saved file byte-identical.
void testScanWindowSingleReplace()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("freeze.v"));
    writeText(path, QByteArray("foo\nabc foo\nxyz foo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    bool savedDuringScan = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&savedDuringScan, path](const QList<SearchHit> &) {
            if (savedDuringScan) {
                return;
            }
            savedDuringScan = true;
            // The user's save while the scan is still in flight (after the walk,
            // before the panel freezes the per-file stamps): the hit that was on
            // line 2 (`abc foo`) has moved to line 1.
            writeText(path, QByteArray("abc foo\nxyz foo\n"));
        });
    panel.startSearch();
    check(QStringLiteral("scan-window.single.search"), waitForSearch(panel));
    QObject::disconnect(hook);
    check(QStringLiteral("scan-window.single.saved-during-scan"), savedDuringScan);
    // The user's save during the scan is the only content the file may hold.
    const QByteArray saved = QByteArray("abc foo\nxyz foo\n");
    check(QStringLiteral("scan-window.single.save-on-disk"), readBytes(path) == saved,
          QString::fromLatin1(readBytes(path)));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 2 && hit.column == 5) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("scan-window.single.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);
    panel.setReplaceText(QStringLiteral("BAR"));
    panel.replaceCurrentHit();
    check(QStringLiteral("scan-window.single.idle"), waitForPanelIdle(panel));

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("scan-window.single.refused"), !outcome.ok,
          QStringLiteral("ok=%1 replacements=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
    check(QStringLiteral("scan-window.single.file-untouched"), readBytes(path) == saved,
          QString::fromLatin1(readBytes(path)));
    check(QStringLiteral("scan-window.single.no-wrong-occurrence"),
          !readBytes(path).contains("BAR"), QString::fromLatin1(readBytes(path)));
}

// (1b) The same scan-window race through 「全部替换」: the counting scan saw
// `foo a`/`foo b`, the user saved a line inserted at the top, and the old ranges
// still hold `foo` at their coordinates. Refusing means the file keeps the
// user's save and no occurrence is replaced.
void testScanWindowReplaceAll()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("freeze-all.v"));
    writeText(path, QByteArray("foo a\nfoo b\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);

    bool savedDuringScan = false;
    const QMetaObject::Connection hook = QObject::connect(
        panel.search(), &GlobalSearch::hitsReady, &panel,
        [&savedDuringScan, path](const QList<SearchHit> &) {
            if (savedDuringScan) {
                return;
            }
            savedDuringScan = true;
            writeText(path, QByteArray("foo c\nfoo a\nfoo b\n"));
        });
    panel.setReplaceConfirmHandler([](const ReplacePlan &, const QStringList &) { return true; });
    panel.setReplaceText(QStringLiteral("BAR"));
    check(QStringLiteral("scan-window.all.start"), panel.replaceAllRequested());
    check(QStringLiteral("scan-window.all.idle"), waitForPanelIdle(panel));
    QObject::disconnect(hook);
    check(QStringLiteral("scan-window.all.saved-during-scan"), savedDuringScan);
    // The user's save during the scan is the only content the file may hold.
    const QByteArray saved = QByteArray("foo c\nfoo a\nfoo b\n");

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("scan-window.all.refused"), !outcome.ok,
          QStringLiteral("ok=%1 replacements=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
    check(QStringLiteral("scan-window.all.file-untouched"), readBytes(path) == saved,
          QString::fromLatin1(readBytes(path)));
    check(QStringLiteral("scan-window.all.no-wrong-occurrence"),
          !readBytes(path).contains("BAR"), QString::fromLatin1(readBytes(path)));
}

// (2) High (review probe N2): a Ctrl+F whole-project hand-off that arrives while
// a Replace All is counting must neither abandon the operation nor strand the
// dock: one route refuses the hand-off, the other routes it through the same
// refusal/cleanup as a cancel. Either way nothing is written and every control
// is usable again afterwards.
void testHandoffDuringOperation()
{
    const auto makePanel = [](const QString &root) {
        auto *panel = new SearchPanel();
        panel->setRootPath(root);
        panel->resize(900, 500);
        panel->show();
        QCoreApplication::processEvents();
        panel->setScope(SearchScope::WholeProject);
        panel->setQuery(QStringLiteral("foo"));
        panel->setCaseSensitive(true);
        // Keep the abandoned operation offscreen: declining the confirmation is
        // the same cleanup a cancel performs.
        panel->setReplaceConfirmHandler(
            [](const ReplacePlan &, const QStringList &) { return false; });
        return panel;
    };

    // (a) the bar's hand-off tunnel (applyHandoff).
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("handoff.v"));
        writeText(path, QByteArray("foo\n"));
        SearchPanel *panel = makePanel(dir.path());
        bool handedOff = false;
        const QMetaObject::Connection hook = QObject::connect(
            panel->search(), &GlobalSearch::hitsReady, panel,
            [panel, &handedOff](const QList<SearchHit> &) {
                if (handedOff) {
                    return;
                }
                handedOff = true;
                vedit::search::SearchHandoff handoff;
                handoff.text = QStringLiteral("bar");
                handoff.options.caseSensitive = true;
                handoff.options.scope = SearchScope::WholeProject;
                panel->applyHandoff(handoff);
            });
        check(QStringLiteral("handoff-op.apply.start"), panel->replaceAllRequested());
        check(QStringLiteral("handoff-op.apply.idle"), waitForPanelIdle(*panel));
        QObject::disconnect(hook);
        check(QStringLiteral("handoff-op.apply.delivered"), handedOff);
        check(QStringLiteral("handoff-op.apply.no-write"),
              readText(path) == QStringLiteral("foo\n"), readText(path));
        check(QStringLiteral("handoff-op.apply.query-enabled"), panel->queryEdit()->isEnabled());
        auto *replaceAll = panel->findChild<QPushButton *>(QStringLiteral("search-replace-all"));
        check(QStringLiteral("handoff-op.apply.replace-all-enabled"),
              replaceAll != nullptr && replaceAll->isEnabled());
        check(QStringLiteral("handoff-op.apply.replace-edit-enabled"),
              panel->replaceEdit()->isEnabled());
        check(QStringLiteral("handoff-op.apply.message"),
              !panel->replaceStatusLabel()->text().isEmpty(),
              panel->replaceStatusLabel()->text());
        panel->hide();
        delete panel;
    }

    // (b) a programmatic search request (the dock's own Search path) while the
    // counting scan runs: same rule — refused, and the dock recovers.
    {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("search-again.v"));
        writeText(path, QByteArray("foo\n"));
        SearchPanel *panel = makePanel(dir.path());
        bool restarted = false;
        const QMetaObject::Connection hook = QObject::connect(
            panel->search(), &GlobalSearch::hitsReady, panel,
            [panel, &restarted](const QList<SearchHit> &) {
                if (restarted) {
                    return;
                }
                restarted = true;
                panel->startSearch();
            });
        check(QStringLiteral("handoff-op.search.start"), panel->replaceAllRequested());
        check(QStringLiteral("handoff-op.search.idle"), waitForPanelIdle(*panel));
        QObject::disconnect(hook);
        check(QStringLiteral("handoff-op.search.delivered"), restarted);
        check(QStringLiteral("handoff-op.search.no-write"),
              readText(path) == QStringLiteral("foo\n"), readText(path));
        check(QStringLiteral("handoff-op.search.query-enabled"), panel->queryEdit()->isEnabled());
        auto *replaceAll = panel->findChild<QPushButton *>(QStringLiteral("search-replace-all"));
        check(QStringLiteral("handoff-op.search.replace-all-enabled"),
              replaceAll != nullptr && replaceAll->isEnabled());
        panel->hide();
        delete panel;
    }
}

// A fake open buffer that exposes BOTH the text the user sees (bufferText) and a
// save() that writes it — what the panel must inspect BEFORE saving a dirty
// buffer for a single 「替换」.
class BufferTextBridge final : public OpenDocumentBridge
{
public:
    BufferTextBridge(const QString &path, const QByteArray &bufferText)
        : path_(QFileInfo(path).absoluteFilePath())
        , bufferText_(QString::fromUtf8(bufferText))
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
        writeText(path_, bufferText_.toUtf8());
        ++saves;
        return true;
    }
    QString bufferText(const QString &absolutePath) const override
    {
        return QFileInfo(absolutePath).absoluteFilePath() == path_ ? bufferText_ : QString();
    }
    QString contentToken(const QString &) const override { return QStringLiteral("token"); }
    RefreshStatus refresh(const QString &, const QString &, QString *) override
    {
        return RefreshStatus::NotOpen;
    }

    int saves = 0;

private:
    QString path_;
    QString bufferText_;
};

// (3) Medium (review probe N3): a single 「替换」 on a dirty buffer whose edit
// does NOT disturb the selected line. The panel's own save used to invalidate
// the frozen stamps, so it saved the buffer and then refused with a message
// blaming the document. The identity is re-captured after that save, so the
// replacement is applied to the saved content.
void testDirtyBufferSingleReplace()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dirty.v"));
    writeText(path, QByteArray("foo\nabc foo\n"));

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(QStringLiteral("dirty-single.search"), waitForSearch(panel));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 1 && hit.column == 1) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("dirty-single.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The unsaved edit is on line 2; the selected range on line 1 is untouched.
    SavingBridge bridge(path, QByteArray("foo\nabc foo edited\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(QStringLiteral("dirty-single.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("dirty-single.save-ran"), bridge.saves == 1,
          QStringLiteral("saves=%1").arg(bridge.saves));
    check(QStringLiteral("dirty-single.started"), started);
    check(QStringLiteral("dirty-single.ok"), outcome.ok,
          QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
    check(QStringLiteral("dirty-single.replaced-on-saved-text"),
          readText(path) == QStringLiteral("BAR\nabc foo edited\n"), readText(path));
}

// (3b) A dirty buffer whose own edit moved the selected match: the stale check
// runs BEFORE the save, so the buffer is not written and the disk file keeps the
// content the scan saw (nothing is saved, nothing is replaced).
void testDirtyBufferStaleEditNotSaved()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dirty-stale.v"));
    const QByteArray before = QByteArray("foo\nabc foo\nxyz foo\n");
    writeText(path, before);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(QStringLiteral("dirty-stale.search"), waitForSearch(panel));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 2 && hit.column == 5) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("dirty-stale.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The buffer deleted line 1, so the scanned line-2 match is now line 1.
    BufferTextBridge bridge(path, QByteArray("abc foo\nxyz foo\n"));
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    panel.replaceCurrentHit();
    check(QStringLiteral("dirty-stale.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("dirty-stale.refused"), !outcome.ok,
          QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
    check(QStringLiteral("dirty-stale.no-save"), bridge.saves == 0,
          QStringLiteral("saves=%1").arg(bridge.saves));
    check(QStringLiteral("dirty-stale.disk-unchanged"), readBytes(path) == before,
          QString::fromLatin1(readBytes(path)));
    check(QStringLiteral("dirty-stale.error-names-file"),
          outcome.error.contains(QStringLiteral("dirty-stale.v")), outcome.error);
}

// b2-repair-r4: the cap detector the whole-line proof keys on. A preview is
// truncated exactly when the scanner's `snippet_of` appended its ellipsis after
// the 400 kept characters; a line that legitimately ends with '…' but is not
// capped must not be mistaken for a truncated one.
void testPreviewCapTruncationDetector()
{
    const QString capped = QString(400, QLatin1Char('x')) + QChar(0x2026);
    check(QStringLiteral("preview-cap.detector-truncated"),
          vedit::workspace::search::matchedLinePreviewTruncated(capped));
    const QString atCap = QString(400, QLatin1Char('x')) + QStringLiteral(" y");
    check(QStringLiteral("preview-cap.detector-at-cap"),
          !vedit::workspace::search::matchedLinePreviewTruncated(atCap));
    check(QStringLiteral("preview-cap.detector-short-ellipsis"),
          !vedit::workspace::search::matchedLinePreviewTruncated(
              QStringLiteral("module a; // …")));
    check(QStringLiteral("preview-cap.detector-empty"),
          !vedit::workspace::search::matchedLinePreviewTruncated(QString()));
}

// b2-repair-r4 (review probe P5): the per-hit fingerprint is the TRIMMED
// 400-CHARACTER PREVIEW of the matched line, not the whole line. On a dirty
// buffer whose edit pastes a line identical in those 400 characters above the
// hit, the pre-save fingerprint check used to accept, the panel saved the buffer
// and 「替换」 wrote the replacement into the INSERTED line while the scanned
// line moved down untouched — a wrong write into a file the user just saved,
// reported as success. The pre-save step now proves the position with the WHOLE
// line the scan read, before anything is saved.
void testPreviewCapDuplicateRefused()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("preview-cap.v"));
    const QString head = QStringLiteral("foo ") + QString(500, QLatin1Char('X'));
    const QString original = head + QStringLiteral("TAIL_A");
    writeText(path, (original + QLatin1Char('\n')).toUtf8());
    const QByteArray before = readBytes(path);

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(QStringLiteral("preview-cap.search"), waitForSearch(panel));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 1 && hit.column == 1) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("preview-cap.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The user's dirty buffer: a line identical to the hit's first 400 trimmed
    // characters (it differs only in the tail, after the preview cap) inserted
    // ABOVE the scanned line. Saving it would make the inserted line the line
    // the stale coordinates point at.
    const QString inserted = head + QStringLiteral("TAIL_B");
    BufferTextBridge duplicate(
        path, (inserted + QLatin1Char('\n') + original + QLatin1Char('\n')).toUtf8());
    vedit::workspace::search::setOpenDocumentBridge(&duplicate);
    panel.setReplaceText(QStringLiteral("BAR"));
    panel.replaceCurrentHit();
    check(QStringLiteral("preview-cap.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("preview-cap.refused"), !outcome.ok,
          QStringLiteral("ok=%1 replacements=%2 error=%3")
              .arg(outcome.ok)
              .arg(outcome.replacements)
              .arg(outcome.error));
    check(QStringLiteral("preview-cap.no-save"), duplicate.saves == 0,
          QStringLiteral("saves=%1").arg(duplicate.saves));
    check(QStringLiteral("preview-cap.disk-unchanged"), readBytes(path) == before,
          QString::fromUtf8(readBytes(path).left(40)));
    check(QStringLiteral("preview-cap.error-names-file"),
          outcome.error.contains(QStringLiteral("preview-cap.v")), outcome.error);

    // The same whole-line proof refuses when the buffer's line at the hit
    // position has the same 400-character preview but a different tail (a user
    // edit after the cap): the text the matcher read is no longer there.
    BufferTextBridge edited(path,
                            (head + QStringLiteral("TAIL_C") + QLatin1Char('\n')).toUtf8());
    vedit::workspace::search::setOpenDocumentBridge(&edited);
    panel.replaceCurrentHit();
    check(QStringLiteral("preview-cap.tail-idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);
    const ReplaceOutcome tailOutcome = panel.lastReplaceOutcome();
    check(QStringLiteral("preview-cap.tail-refused"), !tailOutcome.ok,
          QStringLiteral("ok=%1 error=%2").arg(tailOutcome.ok).arg(tailOutcome.error));
    check(QStringLiteral("preview-cap.tail-no-save"), edited.saves == 0,
          QStringLiteral("saves=%1").arg(edited.saves));
    check(QStringLiteral("preview-cap.tail-disk-unchanged"), readBytes(path) == before,
          QString::fromUtf8(readBytes(path).left(40)));
    panel.hide();
}

// The whole-line proof must not refuse a legitimate dirty-buffer replace: an
// edit BELOW the hit leaves the hit line exactly as the scan read it, so the
// save-first step and the replacement proceed — on the scanned line.
void testPreviewCapLegitimateReplace()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("preview-cap-ok.v"));
    const QString head = QStringLiteral("foo ") + QString(500, QLatin1Char('X'));
    const QString original = head + QStringLiteral("TAIL_A");
    writeText(path, (original + QLatin1Char('\n')).toUtf8());

    SearchPanel panel;
    panel.setRootPath(dir.path());
    panel.resize(900, 500);
    panel.show();
    QCoreApplication::processEvents();
    panel.setScope(SearchScope::WholeProject);
    panel.setQuery(QStringLiteral("foo"));
    panel.setCaseSensitive(true);
    panel.startSearch();
    check(QStringLiteral("preview-cap-ok.search"), waitForSearch(panel));

    int index = -1;
    for (int candidate = 0; candidate < panel.hits().size(); ++candidate) {
        const SearchHit &hit = panel.hits().at(candidate);
        if (hit.line == 1 && hit.column == 1) {
            index = candidate;
            break;
        }
    }
    check(QStringLiteral("preview-cap-ok.hit"), index >= 0,
          QStringLiteral("hits=%1").arg(panel.hits().size()));
    if (index < 0) {
        return;
    }
    panel.activateHit(index);

    // The unsaved edit is BELOW the hit line, which stays byte-identical to the
    // line the scan read.
    BufferTextBridge bridge(
        path, (original + QLatin1Char('\n') + QStringLiteral("foo added below") + QLatin1Char('\n'))
                  .toUtf8());
    vedit::workspace::search::setOpenDocumentBridge(&bridge);
    panel.setReplaceText(QStringLiteral("BAR"));
    const bool started = panel.replaceCurrentHit();
    check(QStringLiteral("preview-cap-ok.idle"), waitForPanelIdle(panel));
    vedit::workspace::search::setOpenDocumentBridge(nullptr);

    const ReplaceOutcome outcome = panel.lastReplaceOutcome();
    check(QStringLiteral("preview-cap-ok.started"), started);
    check(QStringLiteral("preview-cap-ok.ok"), outcome.ok,
          QStringLiteral("ok=%1 error=%2").arg(outcome.ok).arg(outcome.error));
    check(QStringLiteral("preview-cap-ok.replaced-one"), outcome.replacements == 1,
          QStringLiteral("replacements=%1").arg(outcome.replacements));
    check(QStringLiteral("preview-cap-ok.save-ran"), bridge.saves == 1,
          QStringLiteral("saves=%1").arg(bridge.saves));
    const QStringList lines = QString::fromUtf8(readBytes(path)).split(QLatin1Char('\n'));
    check(QStringLiteral("preview-cap-ok.replaced-scanned-line"),
          lines.value(0) == QStringLiteral("BAR ") + QString(500, QLatin1Char('X')) +
                               QStringLiteral("TAIL_A"),
          lines.value(0));
    check(QStringLiteral("preview-cap-ok.below-intact"),
          lines.value(1) == QStringLiteral("foo added below"), lines.value(1));
    panel.hide();
}

// The per-hit content identity as the pure range applier sees it: a hit whose
// recorded line fingerprint no longer matches the file refuses the whole call,
// a matching one is applied, and a hand-built range with no evidence keeps the
// previous contract (the query range check decides).
void testLineFingerprintGuard()
{
    const QString text = QStringLiteral("foo a\nfoo b\n");
    const vedit::workspace::search::ReplaceQuery query{QStringLiteral("foo"), true, false, false};
    const auto makeHit = [](int line, const QString &scannedLine) {
        vedit::workspace::search::ReplaceHit value;
        value.filePath = QStringLiteral("/tmp/fingerprint.v");
        value.line = line;
        value.column = 1;
        value.matchLength = 3;
        value.scanId = 7;
        value.lineFingerprint = vedit::workspace::search::matchedLineFingerprint(scannedLine);
        return value;
    };

    // The scanner reported line 2 as "foo a", but line 2 now holds "foo b":
    // the same token is at the same coordinates, yet it is not the text read.
    const auto mismatch = vedit::workspace::search::applyReplaceRanges(
        text, QList<vedit::workspace::search::ReplaceHit>{makeHit(2, QStringLiteral("foo a"))},
        QStringLiteral("BAR"), query);
    check(QStringLiteral("fingerprint.mismatch-refused"), !mismatch.ok && mismatch.text == text,
          mismatch.error);
    check(QStringLiteral("fingerprint.mismatch-names-file"),
          mismatch.error.contains(QStringLiteral("fingerprint.v")), mismatch.error);

    const auto matched = vedit::workspace::search::applyReplaceRanges(
        text, QList<vedit::workspace::search::ReplaceHit>{makeHit(2, QStringLiteral("foo b"))},
        QStringLiteral("BAR"), query);
    check(QStringLiteral("fingerprint.match-applied"),
          matched.ok && matched.text == QStringLiteral("foo a\nBAR b\n"), matched.error);

    vedit::workspace::search::ReplaceHit legacy = makeHit(1, QStringLiteral("foo a"));
    legacy.lineFingerprint.clear();
    const auto legacyResult = vedit::workspace::search::applyReplaceRanges(
        text, QList<vedit::workspace::search::ReplaceHit>{legacy}, QStringLiteral("BAR"), query);
    check(QStringLiteral("fingerprint.absent-range-check-still-guards"),
          legacyResult.ok && legacyResult.text == QStringLiteral("BAR a\nfoo b\n"),
          legacyResult.error);
}

// High 5: the dock's CurrentFile scope follows the ACTIVE pane's document, not
// the last file touched by the bar or a result click.
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE) && defined(VEDIT_SEARCH_HAVE_SHELL)
void testCurrentFileScopeFollowsActivePane(const QString &)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        skip(QStringLiteral("current-file.active-pane"), QStringLiteral("katepart not available"));
        return;
    }
    QTemporaryDir dir;
    QTemporaryDir stateDir;
    const QString aPath = QDir(dir.path()).filePath(QStringLiteral("pane-a.v"));
    const QString bPath = QDir(dir.path()).filePath(QStringLiteral("pane-b.v"));
    writeText(aPath, QByteArray("foo a\n"));
    writeText(bPath, QByteArray("foo b\n"));

    vedit::app::ShellOptions shellOptions;
    shellOptions.stateDir = stateDir.path();
    shellOptions.projectRoot = dir.path();
    shellOptions.restoreSession = false;
    shellOptions.showDefaultPanels = false;
    vedit::app::ApplicationShell shell(shellOptions);
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());

    QString openError;
    check(QStringLiteral("current-file.open"),
          shell.openPaths({aPath, bPath}, &openError), openError);
    const QList<vedit::app::EditorPane *> panes = shell.editorArea()->panes();
    check(QStringLiteral("current-file.two-panes"), panes.size() == 2,
          QString::number(panes.size()));
    vedit::app::EditorPane *paneA = nullptr;
    vedit::app::EditorPane *paneB = nullptr;
    for (vedit::app::EditorPane *pane : panes) {
        if (QFileInfo(pane->currentFilePath()) == QFileInfo(aPath)) {
            paneA = pane;
        }
        if (QFileInfo(pane->currentFilePath()) == QFileInfo(bPath)) {
            paneB = pane;
        }
    }
    check(QStringLiteral("current-file.panes-found"), paneA != nullptr && paneB != nullptr);
    if (paneA == nullptr || paneB == nullptr) {
        return;
    }

    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = dir.path();
    integration.stateDir = stateDir.path();
    vedit::workspace::installSearch(shell, integration);
    shell.panels()->showPanel(QStringLiteral("search"), &shell);
    SearchPanel *panel = shell.findChild<SearchPanel *>();
    check(QStringLiteral("current-file.dock-created"), panel != nullptr);
    if (panel != nullptr) {
        shell.editorArea()->setActivePane(paneB);
        QCoreApplication::processEvents();
        panel->setScope(SearchScope::CurrentFile);
        panel->setQuery(QStringLiteral("foo"));
        panel->setCaseSensitive(true);
        panel->startSearch();
        check(QStringLiteral("current-file.b.search"), waitForSearch(*panel));
        QStringList filesB;
        for (const SearchHit &hit : panel->hits()) {
            if (!filesB.contains(hit.filePath)) {
                filesB.append(hit.filePath);
            }
        }
        check(QStringLiteral("current-file.b.hits"),
              panel->hits().size() == 1 && filesB.size() == 1 &&
                  QFileInfo(filesB.value(0)) == QFileInfo(bPath),
              QStringLiteral("hits=%1 files=%2")
                  .arg(panel->hits().size())
                  .arg(filesB.join(QStringLiteral(","))));

        // Switch to the OTHER pane and search the same scope again.
        shell.editorArea()->setActivePane(paneA);
        QCoreApplication::processEvents();
        panel->startSearch();
        check(QStringLiteral("current-file.a.search"), waitForSearch(*panel));
        QStringList filesA;
        for (const SearchHit &hit : panel->hits()) {
            if (!filesA.contains(hit.filePath)) {
                filesA.append(hit.filePath);
            }
        }
        check(QStringLiteral("current-file.a.hits"),
              panel->hits().size() == 1 && filesA.size() == 1 &&
                  QFileInfo(filesA.value(0)) == QFileInfo(aPath),
              QStringLiteral("hits=%1 files=%2")
                  .arg(panel->hits().size())
                  .arg(filesA.join(QStringLiteral(","))));
    }
    vedit::search::SearchHub::instance().removeProjectSearchHandler(&shell);
}
#else
void testCurrentFileScopeFollowsActivePane(const QString &)
{
    skip(QStringLiteral("current-file.active-pane"),
         QStringLiteral("no editor kernel or shell in this build"));
}
#endif

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-search-replace-tests"));

    QString evidenceRoot;
    const QStringList arguments = QCoreApplication::arguments();
    const int evidenceIndex = arguments.indexOf(QStringLiteral("--evidence"));
    if (evidenceIndex >= 0 && evidenceIndex + 1 < arguments.size()) {
        evidenceRoot = QFileInfo(arguments.at(evidenceIndex + 1)).absoluteFilePath();
        QDir().mkpath(evidenceRoot);
    }

    QTemporaryDir fixture;
    if (!fixture.isValid()) {
        std::printf("FATAL: cannot create the temporary fixture project\n");
        return 1;
    }
    writeFixture(fixture.path());

    std::printf("FIXTURE %s\n", qPrintable(fixture.path()));
    if (!evidenceRoot.isEmpty()) {
        std::printf("EVIDENCE %s\n", qPrintable(evidenceRoot));
    }

    testSharedOptionModel(fixture.path());
    testOptionSemantics(fixture.path());
    testReplaceOne(fixture.path());
    writeFixture(fixture.path()); // reset before the batch replace
    testReplaceAll(fixture.path(), evidenceRoot);
    writeFixture(fixture.path()); // reset before the hand-off / buffer tests
    testHandoff(fixture.path());
    testShellHandoff(fixture.path());
    writeFixture(fixture.path());
    testUnsavedBuffer(fixture.path());

    // Review findings (b2-repair): each isolated on its own temporary project.
    testReplaceAllTimeout();
    testStaleRangeOne();
    testStaleRangeBatch();
    testUnicodeBom();
    testMidBatchFailure();
    testSelectedRowReplace();
    testRegexWholeWordReplace();
    testDockOpenKeepsBar();
    testSaveOnlyAffectedDocuments();
    testSaveFailureWording();
    testEditDuringReplace();
    testFrozenOptionDuringCountingScan();
    testFrozenEmptyQueryDuringCountingScan();
    testSingleReplaceAfterQueryCleared();
    testLineShiftRefused();
    testPostSaveScopeFrozen();
    testCancelWordingAfterSave();
    testBufferOnlyMatchIsSavedAndReplaced();
    testConfirmWindowChangeRefused();
    // b2-repair-r3: the scan window, the Ctrl+F hand-off during an operation and
    // the save-first step of a single 「替换」.
    testScanWindowSingleReplace();
    testScanWindowReplaceAll();
    testHandoffDuringOperation();
    testDirtyBufferSingleReplace();
    testDirtyBufferStaleEditNotSaved();
    testLineFingerprintGuard();
    // b2-repair-r4: the per-hit identity beyond the 400-character preview
    // (review probe P5) and the positive control that it is not over-refused.
    testPreviewCapTruncationDetector();
    testPreviewCapDuplicateRefused();
    testPreviewCapLegitimateReplace();
    testCurrentFileScopeFollowsActivePane(fixture.path());

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
