// Self-check and screenshot harness for the workspace search lane (task T23,
// acceptance A2c).
//
//   vedit-workspace-search-selftest --selfcheck
//       Runs the result-correctness, empty-state, jump, limit, permission,
//       large-file, cancellation and worker-lifetime checks; prints CHECK lines
//       and one machine-readable summary line. Exit code 0 iff no check failed.
//       The lifetime check re-executes this binary as --lifecycle-slow with the
//       lane's stall shim (tools/slowio) in LD_PRELOAD, so the destructor really
//       races a blocking read (evidence/search-uaf/).
//
//   vedit-workspace-search-selftest --screenshots <dir>
//       Installs the panel into the real ApplicationShell through the
//       documented PanelRegistry factory seam and captures PNGs (results,
//       empty state, jump, no-project, unreadable root).
//
// The shell itself is untouched: this harness only calls the public seams.
#include "global_search.h"
#include "jump/jump_registry.h"
#include "search_integration.h"
#include "search_panel.h"
#include "search_types.h"

#if defined(VEDIT_SEARCH_HAVE_FFI)
#include "vd_ffi.h"
#endif

#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
#include "app_shell.h"
#include "editor_area.h"
#include "panel_registry.h"
#include "search_integration.h"
#endif

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
#include "kernel/ktexteditor_provider.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPixmap>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
#include <QDockWidget>
#include <QStatusBar>
#endif

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

#include <unistd.h>

using vedit::workspace::search::GlobalSearch;
using vedit::workspace::jump::IJumpTarget;
using vedit::workspace::search::JumpRequest;
using vedit::workspace::search::SearchHit;
using vedit::workspace::search::SearchOptions;
using vedit::workspace::search::SearchPanel;
using vedit::workspace::search::SearchState;
using vedit::workspace::search::SearchSummary;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void skipCheck(const char *name, const char *why)
{
    ++g_skips;
    std::printf("CHECK %s SKIP (%s)\n", name, why);
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
        QTest::qWait(1);
    }
    return true;
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(content);
    }
    file.close();
}

bool waitForSearch(SearchPanel &panel, int timeoutMs = 60000)
{
    return waitUntil([&panel]() { return !panel.isSearching(); }, timeoutMs);
}

// A small project that exercises every result rule the acceptance names:
// several file types, a Unicode file/query, one line with two matches, a binary
// file, a file above the size limit and two directories that must be pruned.
class Fixture
{
public:
    Fixture()
    {
        if (!temp_.isValid()) {
            return;
        }
        root_ = temp_.path();
        writeFile(path(QStringLiteral("a.v")),
                  "module top;\n  always_ff @(posedge clk) begin\n  end\nendmodule\n");
        writeFile(path(QStringLiteral("b.sv")),
                  "// b.sv\nalways_ff x;\ninitial begin\nalways_ff y;\nend\n");
        writeFile(path(QStringLiteral("sub/c.vh")), "// header\nalways_ff z;\n");
        writeFile(path(QStringLiteral("notes.md")), "ALWAYS_FF uppercase\nplain\n");
        writeFile(path(QStringLiteral("sub/中文.v")), "// 模块 always_ff 中文注释\n");
        writeFile(path(QStringLiteral("double.txt")), "always_ff always_ff\n");

        QByteArray binary("always_ff");
        binary.append('\0');
        binary.append("binary payload");
        binary.append('\0');
        writeFile(path(QStringLiteral("bin.bin")), binary);

        // 10 MiB: above the default 8 MiB limit, so it must be skipped and
        // counted rather than read into memory.
        QFile big(path(QStringLiteral("huge.v")));
        if (big.open(QIODevice::WriteOnly)) {
            QByteArray block;
            block.reserve(1 << 20);
            for (int i = 0; i < 40000; ++i) {
                block.append("padding always_ff padding\n");
            }
            for (int i = 0; i < 10; ++i) {
                big.write(block);
            }
            big.close();
        }

        writeFile(path(QStringLiteral(".git/config")), "always_ff hidden\n");
        writeFile(path(QStringLiteral("build/generated.v")), "always_ff generated\n");
    }

    bool valid() const { return temp_.isValid() && !root_.isEmpty(); }
    QString root() const { return root_; }
    QString path(const QString &relative) const { return root_ + QLatin1Char('/') + relative; }

private:
    QTemporaryDir temp_;
    QString root_;
};

QTreeWidgetItem *topLevelForFile(const SearchPanel &panel, const QString &relativePath)
{
    QTreeWidget *tree = panel.resultsView();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->text(0) == relativePath) {
            return item;
        }
    }
    return nullptr;
}

int totalChildItems(QTreeWidget *tree)
{
    int total = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        total += tree->topLevelItem(i)->childCount();
    }
    return total;
}

QTreeWidgetItem *findHitItem(const SearchPanel &panel, const QString &relativePath, int line)
{
    QTreeWidgetItem *fileItem = topLevelForFile(panel, relativePath);
    if (fileItem == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < fileItem->childCount(); ++i) {
        QTreeWidgetItem *item = fileItem->child(i);
        if (item->text(1).toInt() == line) {
            return item;
        }
    }
    return nullptr;
}

const SearchHit *findHitData(const SearchPanel &panel, const QString &relativePath, int line)
{
    for (const SearchHit &hit : panel.hits()) {
        if (hit.relativePath == relativePath && hit.line == line) {
            return &hit;
        }
    }
    return nullptr;
}

// True when the result list holds a hit for `file` (compared as absolute paths,
// which is the panel's open/jump key).
bool hasHitForFile(const SearchPanel &panel, const QString &file)
{
    const QString absolute = QFileInfo(file).absoluteFilePath();
    for (const SearchHit &hit : panel.hits()) {
        if (hit.filePath == absolute) {
            return true;
        }
    }
    return false;
}

// Clicks the tree row of `hit` with a real mouse event (the user's action, the
// same path T23's jump.click.* checks exercise) so the product consumer of
// jumpRequested - not a target injected by this harness - performs the jump.
// Shell-only: the standalone lane build has no product target to click into.
#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
bool clickHitRow(SearchPanel &panel, const SearchHit &hit)
{
    QTreeWidgetItem *item = findHitItem(panel, hit.relativePath, hit.line);
    if (item == nullptr) {
        return false;
    }
    panel.resultsView()->scrollToItem(item);
    QCoreApplication::processEvents();
    const auto rowRect = [&panel, item]() {
        return panel.resultsView()->visualItemRect(item);
    };
    if (!waitUntil([&rowRect]() {
            const QRect rect = rowRect();
            return rect.isValid() && !rect.isEmpty();
        }, 5000)) {
        return false;
    }
    QTest::mouseClick(panel.resultsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rowRect().center());
    QCoreApplication::processEvents();
    return true;
}
#endif

class RecordingJumpTarget final : public IJumpTarget
{
public:
    bool jumpTo(const QString &filePath, int line, int column) override
    {
        JumpRequest request;
        request.filePath = filePath;
        request.line = line;
        request.column = column;
        requests.append(request);
        return true;
    }
    QList<JumpRequest> requests;
};

// What edit-core will do for real: open the file and put the cursor at the hit.
// Registered through the application-wide jump registry (A-1/T20); the panel no
// longer owns a per-panel target seam.
class DemoJumpTarget final : public IJumpTarget
{
public:
    explicit DemoJumpTarget(QPlainTextEdit *editor)
        : editor_(editor)
    {
    }

    bool jumpTo(const QString &filePath, int line, int column) override
    {
        JumpRequest request;
        request.filePath = filePath;
        request.line = line;
        request.column = column;
        requests.append(request);
        if (editor_ == nullptr) {
            return false;
        }
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            editor_->setPlainText(QString::fromUtf8(file.readAll()));
        }
        const int lineIndex = line > 0 ? line - 1 : 0;
        QTextCursor cursor(editor_->document()->findBlockByLineNumber(lineIndex));
        cursor.movePosition(QTextCursor::StartOfLine);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        editor_->setTextCursor(cursor);
        editor_->centerCursor();
        return true;
    }

    QList<JumpRequest> requests;

private:
    QPlainTextEdit *editor_ = nullptr;
};

void testPanelBasics(const Fixture &fixture)
{
    SearchPanel panel;
    panel.resize(900, 600);
    panel.show();
    QCoreApplication::processEvents();
    panel.setRootPath(fixture.root());
    check("panel.root.normalized", panel.rootPath() == QFileInfo(fixture.root()).absoluteFilePath());
    check("panel.empty.enter-query",
          panel.emptyStateLabel()->isVisible() &&
              panel.emptyStateLabel()->text().contains(QStringLiteral("Enter a query")));

    panel.setQuery(QStringLiteral("always_ff"));
    panel.startSearch();
    check("panel.search.terminates", waitForSearch(panel));
    const SearchSummary first = panel.lastSummary();
    check("panel.hits.count", panel.hits().size() == 7);
    check("panel.summary.state", first.state == SearchState::Done);
    check("panel.summary.files", first.filesMatched == 6);
    check("panel.summary.files-scanned", first.filesScanned == 6);
    check("panel.summary.skipped-binary", first.skippedBinary == 1);
    check("panel.summary.skipped-too-large", first.skippedTooLarge == 1);
    check("panel.summary.text.observable",
          panel.summaryLabel()->text().contains(QStringLiteral("binary skipped")) &&
              panel.summaryLabel()->text().contains(QStringLiteral("too large skipped")));
    check("panel.tree.files", panel.resultsView()->topLevelItemCount() == 6);
    check("panel.tree.one-row-per-hit", totalChildItems(panel.resultsView()) == 7);
    check("panel.tree.no-git", topLevelForFile(panel, QStringLiteral(".git/config")) == nullptr);
    check("panel.tree.no-build", topLevelForFile(panel, QStringLiteral("build/generated.v")) == nullptr);
    check("panel.tree.no-binary", topLevelForFile(panel, QStringLiteral("bin.bin")) == nullptr);
    check("panel.hits.one-per-line", findHitItem(panel, QStringLiteral("double.txt"), 1) != nullptr &&
                                         topLevelForFile(panel, QStringLiteral("double.txt"))->childCount() == 1);

    const SearchHit *aHit = findHitData(panel, QStringLiteral("a.v"), 2);
    check("panel.hit.line-number", aHit != nullptr);
    check("panel.hit.column", aHit != nullptr && aHit->column == 3);
    check("panel.hit.snippet", aHit != nullptr && aHit->text.contains(QStringLiteral("always_ff")));
    const SearchHit *unicodeHit = findHitData(panel, QStringLiteral("sub/中文.v"), 1);
    check("panel.hit.unicode-file", unicodeHit != nullptr);
    check("panel.hit.unicode-column", unicodeHit != nullptr && unicodeHit->column == 7);

    panel.setCaseSensitive(true);
    panel.setQuery(QStringLiteral("always_ff"));
    panel.startSearch();
    waitForSearch(panel);
    check("panel.case-sensitive.count", panel.hits().size() == 6);
    check("panel.case-sensitive.excludes-uppercase",
          topLevelForFile(panel, QStringLiteral("notes.md")) == nullptr);
    panel.setCaseSensitive(false);

    panel.setVerilogOnly(true);
    panel.startSearch();
    waitForSearch(panel);
    check("panel.verilog-only.count", panel.hits().size() == 5);
    check("panel.verilog-only.excludes-markdown",
          topLevelForFile(panel, QStringLiteral("notes.md")) == nullptr);
    check("panel.verilog-only.excludes-large-file",
          topLevelForFile(panel, QStringLiteral("huge.v")) == nullptr);
    panel.setVerilogOnly(false);

    panel.setQuery(QStringLiteral("中文注释"));
    panel.startSearch();
    waitForSearch(panel);
    check("panel.unicode.query",
          panel.hits().size() == 1 && panel.hits().at(0).relativePath == QStringLiteral("sub/中文.v"));
}

void testEmptyStates(const Fixture &fixture)
{
    // 1) no project root at all
    SearchPanel noRoot;
    noRoot.resize(700, 400);
    noRoot.show();
    QCoreApplication::processEvents();
    noRoot.setQuery(QStringLiteral("anything"));
    noRoot.startSearch();
    check("empty.no-project.visible", noRoot.emptyStateLabel()->isVisible());
    check("empty.no-project.text",
          noRoot.emptyStateLabel()->text().contains(QStringLiteral("No project root")));
    check("empty.no-project.no-search", !noRoot.isSearching());

    // 2) a root but an empty query
    SearchPanel emptyQuery;
    emptyQuery.resize(700, 400);
    emptyQuery.show();
    QCoreApplication::processEvents();
    emptyQuery.setRootPath(fixture.root());
    emptyQuery.startSearch();
    check("empty.query.text",
          emptyQuery.emptyStateLabel()->text().contains(QStringLiteral("Enter a query")));
    check("empty.query.no-search", !emptyQuery.isSearching());

    // 3) a finished search with no match
    SearchPanel noResults;
    noResults.resize(700, 400);
    noResults.show();
    QCoreApplication::processEvents();
    noResults.setRootPath(fixture.root());
    noResults.setQuery(QStringLiteral("no_such_token_9999"));
    noResults.startSearch();
    waitForSearch(noResults);
    check("empty.no-results.state", noResults.lastSummary().state == SearchState::Done &&
                                        noResults.lastSummary().hitCount == 0);
    check("empty.no-results.visible", noResults.emptyStateLabel()->isVisible());
    check("empty.no-results.text",
          noResults.emptyStateLabel()->text().contains(QStringLiteral("No results")));
    check("empty.no-results.tree-hidden", !noResults.resultsView()->isVisible());

    // 4) an unreadable root: a failure state, distinct from "no results"
    if (::geteuid() == 0) {
        skipCheck("empty.unreadable-root", "running as root: chmod 000 does not deny access");
        return;
    }
    QTemporaryDir locked;
    const QString secret = locked.path() + QStringLiteral("/secret");
    QDir().mkpath(secret);
    writeFile(secret + QStringLiteral("/inside.txt"), "always_ff\n");
    QFile::setPermissions(secret, QFileDevice::Permissions());
    SearchPanel unreadable;
    unreadable.resize(700, 400);
    unreadable.show();
    QCoreApplication::processEvents();
    unreadable.setRootPath(secret);
    unreadable.setQuery(QStringLiteral("always_ff"));
    unreadable.startSearch();
    waitForSearch(unreadable);
    check("empty.unreadable-root.state",
          unreadable.lastSummary().state == SearchState::Failed);
    check("empty.unreadable-root.text",
          unreadable.emptyStateLabel()->text().contains(QStringLiteral("not readable")));
    QFile::setPermissions(secret, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner);
}

void testUnreadableEntries(const Fixture &fixture)
{
    Q_UNUSED(fixture);
    if (::geteuid() == 0) {
        skipCheck("entries.unreadable", "running as root: chmod 000 does not deny access");
        return;
    }
    QTemporaryDir dir;
    writeFile(dir.path() + QStringLiteral("/ok.txt"), "always_ff ok\n");
    writeFile(dir.path() + QStringLiteral("/locked.txt"), "always_ff locked\n");
    writeFile(dir.path() + QStringLiteral("/locked-dir/inside.txt"), "always_ff inside\n");
    QFile::setPermissions(dir.path() + QStringLiteral("/locked.txt"), QFileDevice::Permissions());
    QFile::setPermissions(dir.path() + QStringLiteral("/locked-dir"), QFileDevice::Permissions());

    SearchPanel panel;
    panel.resize(700, 400);
    panel.show();
    QCoreApplication::processEvents();
    panel.setRootPath(dir.path());
    panel.setQuery(QStringLiteral("always_ff"));
    panel.startSearch();
    waitForSearch(panel);
    const SearchSummary summary = panel.lastSummary();
    check("entries.unreadable.state", summary.state == SearchState::Done);
    check("entries.unreadable.counted", summary.skippedUnreadable == 2);
    check("entries.unreadable.text",
          panel.summaryLabel()->text().contains(QStringLiteral("unreadable skipped")));
    check("entries.unreadable.results-intact",
          panel.hits().size() == 1 && panel.hits().at(0).relativePath == QStringLiteral("ok.txt"));

    QFile::setPermissions(dir.path() + QStringLiteral("/locked.txt"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::setPermissions(dir.path() + QStringLiteral("/locked-dir"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void testLimits(const Fixture &fixture)
{
    SearchPanel panel;
    panel.resize(700, 400);
    panel.show();
    QCoreApplication::processEvents();
    panel.setRootPath(fixture.root());
    panel.setResultLimit(3);
    panel.setQuery(QStringLiteral("always_ff"));
    panel.startSearch();
    waitForSearch(panel);
    check("limit.results", panel.hits().size() == 3);
    check("limit.truncated-flag", panel.lastSummary().truncated);
    check("limit.summary-text",
          panel.summaryLabel()->text().contains(QStringLiteral("result limit 3 reached")));
}

void testExtensions()
{
    check("extension.v", vedit::workspace::search::isVerilogFileName(QStringLiteral("test.v")));
    check("extension.vh", vedit::workspace::search::isVerilogFileName(QStringLiteral("test.VH")));
    check("extension.sv", vedit::workspace::search::isVerilogFileName(QStringLiteral("test.sv")));
    check("extension.svh", vedit::workspace::search::isVerilogFileName(QStringLiteral("test.svh")));
    check("extension.not-verilog",
          !vedit::workspace::search::isVerilogFileName(QStringLiteral("test.vhd")) &&
              !vedit::workspace::search::isVerilogFileName(QStringLiteral("test.txt")));
}

// Batch 20261110d: the panel searches the UNION of the folder and the project
// file set. The fixture below is the `.f` shape from the report: the opened
// folder is `filelists/` and the source lives next to it (`../tb/`), so a
// directory walk of the root can never see it. The union must also keep the
// folder's own non-source files searchable (the product is a text editor, not
// an HDL-only tool).
void testExplicitFileSet()
{
    QTemporaryDir temp;
    check("explicit.fixture", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString root = temp.path() + QStringLiteral("/filelists");
    const QString inside = root + QStringLiteral("/notes.md");
    const QString outside = temp.path() + QStringLiteral("/tb/tb_top.sv");
    const QString noteOnly = root + QStringLiteral("/readme_only.md");
    writeFile(inside, "scope_probe inside\n");
    writeFile(outside, "scope_probe outside\n");
    writeFile(noteOnly, "md_token_alpha only in a folder note\n");

    // 1) No provider = no project model yet: the plain walk. This is the RED
    // evidence for the reported bug — the sibling file is invisible.
    {
        SearchPanel panel;
        panel.setRootPath(root);
        panel.setQuery(QStringLiteral("scope_probe"));
        panel.startSearch();
        check("explicit.walk.terminates", waitForSearch(panel));
        check("explicit.walk.root-file", panel.hits().size() == 1);
        check("explicit.walk.misses-outside", !hasHitForFile(panel, outside));
        check("explicit.walk.not-named-as-set", !panel.lastSummary().usedProjectFileSet);
    }

    // 2) The union: the member OUTSIDE the root is searched AND the folder's own
    // files stay searchable; the member is listed in the set AND walked, and is
    // still scanned exactly once.
    {
        SearchPanel panel;
        panel.setRootPath(root);
        panel.setProjectFilesProvider(
            [inside, outside]() { return QStringList{inside, outside}; });
        panel.setQuery(QStringLiteral("scope_probe"));
        panel.startSearch();
        check("explicit.set.terminates", waitForSearch(panel));
        const SearchSummary summary = panel.lastSummary();
        check("explicit.set.both-files", panel.hits().size() == 2);
        check("explicit.set.finds-outside", hasHitForFile(panel, outside));
        check("explicit.set.dedup",
              summary.usedProjectFileSet && summary.filesInProjectSet == 2 &&
                  summary.filesScanned == 3);
        check("explicit.set.summary-text",
              panel.summaryLabel()->text().contains(QStringLiteral("whole folder")));

        // 3) NEGATIVE CONTROL: drop the member from the set -> it is no longer
        // searched. The scan cannot be passing through the directory.
        panel.setProjectFilesProvider([inside]() { return QStringList{inside}; });
        panel.startSearch();
        check("explicit.set.negative-control-terminates", waitForSearch(panel));
        check("explicit.set.negative-control-count", panel.hits().size() == 1);
        check("explicit.set.negative-control-no-outside", !hasHitForFile(panel, outside));
        check("explicit.set.negative-control-scanned", panel.lastSummary().filesScanned == 2);

        // 4) The captain's explicit expectation: a file INSIDE the root that is
        // NOT (or no longer) in the project set is STILL searched — the union is
        // not the set. `notes.md` leaves the set, the outside member stays in.
        panel.setProjectFilesProvider([outside]() { return QStringList{outside}; });
        panel.startSearch();
        check("explicit.union.terminates", waitForSearch(panel));
        check("explicit.union.folder-file-still-hit", hasHitForFile(panel, inside));
        check("explicit.union.member-hit", hasHitForFile(panel, outside));
        check("explicit.union.count", panel.hits().size() == 2);
        check("explicit.union.set-size", panel.lastSummary().filesInProjectSet == 1);
        check("explicit.union.scanned-both", panel.lastSummary().filesScanned == 3);

        // 5) A folder `.md` with a token that exists nowhere else: with a
        // project set supplied it must still be found (no narrowing to HDL).
        panel.setQuery(QStringLiteral("md_token_alpha"));
        panel.startSearch();
        check("explicit.union.md-terminates", waitForSearch(panel));
        check("explicit.union.md-hit",
              hasHitForFile(panel, noteOnly) && panel.hits().size() == 1);
    }

    // 6) An EMPTY provider must fall back to the plain walk, never scan zero
    // files.
    {
        SearchPanel panel;
        panel.setRootPath(root);
        panel.setProjectFilesProvider([]() { return QStringList{}; });
        panel.setQuery(QStringLiteral("scope_probe"));
        panel.startSearch();
        check("explicit.empty-provider.falls-back",
              waitForSearch(panel) && panel.hits().size() == 1 &&
                  !panel.lastSummary().usedProjectFileSet);
    }
}

// The explicit set is a SCOPE change only: the match options must report the
// SAME ranges in both modes (no silent capability regression).
void testExplicitFileSetParity()
{
    QTemporaryDir temp;
    check("parity.fixture", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString root = temp.path() + QStringLiteral("/root");
    const QString inside = root + QStringLiteral("/a.sv");
    const QString outside = temp.path() + QStringLiteral("/tb/b.sv");
    const QByteArray content("foo foo foofoo\nFOO\nfoo\n");
    writeFile(inside, content);
    writeFile(outside, content);

    struct OptionCase {
        const char *name;
        QString query;
        bool caseSensitive;
        bool wholeWords;
        bool regex;
    };
    const OptionCase cases[] = {
        {"default", QStringLiteral("foo"), false, false, false},
        {"case-sensitive", QStringLiteral("FOO"), true, false, false},
        {"whole-word", QStringLiteral("foo"), false, true, false},
        {"regex", QStringLiteral("f.o"), false, false, true},
    };
    const auto rangesFor = [](const SearchPanel &panel, const QString &file) {
        QStringList ranges;
        const QString absolute = QFileInfo(file).absoluteFilePath();
        for (const SearchHit &hit : panel.hits()) {
            if (hit.filePath != absolute) {
                continue;
            }
            ranges.append(QStringLiteral("%1:%2:%3:%4")
                              .arg(hit.line)
                              .arg(hit.column)
                              .arg(hit.endColumn)
                              .arg(hit.matchLength));
        }
        return ranges;
    };
    for (const OptionCase &optionCase : cases) {
        SearchPanel walkPanel;
        walkPanel.setRootPath(root);
        walkPanel.setQuery(optionCase.query);
        walkPanel.setCaseSensitive(optionCase.caseSensitive);
        walkPanel.setWholeWords(optionCase.wholeWords);
        walkPanel.setRegex(optionCase.regex);
        walkPanel.startSearch();
        const bool walkDone = waitForSearch(walkPanel);
        // Capture the walk result BEFORE the second panel exists: two panels
        // share the ONE SearchHub, and a later publish on the second invalidates
        // the first's rows (product behaviour, one dock per window).
        const QStringList walkRanges = rangesFor(walkPanel, inside);
        const int walkHits = static_cast<int>(walkPanel.hits().size());
        const int walkScanned = walkPanel.lastSummary().filesScanned;

        SearchPanel setPanel;
        setPanel.setRootPath(root);
        setPanel.setProjectFilesProvider(
            [inside, outside]() { return QStringList{inside, outside}; });
        setPanel.setQuery(optionCase.query);
        setPanel.setCaseSensitive(optionCase.caseSensitive);
        setPanel.setWholeWords(optionCase.wholeWords);
        setPanel.setRegex(optionCase.regex);
        setPanel.startSearch();
        const bool setDone = waitForSearch(setPanel);

        const QString checkName = QStringLiteral("parity.") + QLatin1String(optionCase.name);
        check(qPrintable(checkName + QStringLiteral(".terminates")), walkDone && setDone);

        const QStringList setRanges = rangesFor(setPanel, inside);
        std::printf("PARITY %s walk-hits=%d set-hits=%d walk-ranges='%s' set-ranges='%s'\n",
                    optionCase.name, walkHits, static_cast<int>(setPanel.hits().size()),
                    qPrintable(walkRanges.join(QLatin1Char(','))),
                    qPrintable(setRanges.join(QLatin1Char(','))));
        std::fflush(stdout);
        check(qPrintable(checkName + QStringLiteral(".same-ranges")), walkRanges == setRanges);
        check(qPrintable(checkName + QStringLiteral(".set-adds-member")),
              static_cast<int>(setPanel.hits().size()) == walkHits * 2);
        check(qPrintable(checkName + QStringLiteral(".walk-count")),
              walkScanned == 1 && setPanel.lastSummary().filesScanned == 2);
    }
}

#if defined(VEDIT_SEARCH_HAVE_FFI) && defined(VEDIT_SEARCH_EXPANSION_FIXTURE)
// Expand a project root with the SAME entry point the lint lane uses, so the
// file set compared by the probe below is the published project model and not
// a test helper with its own rules.
QStringList expandProjectFiles(const QString &root, QString *error)
{
    QStringList files;
    VdService *service = vd_service_create();
    if (service == nullptr) {
        *error = QStringLiteral("cannot create the Rust service handle");
        return files;
    }
    const QByteArray rootPath = root.toUtf8();
    const quint64 job = vd_project_expand_v3(service, rootPath.constData(), nullptr, nullptr,
                                             nullptr, nullptr, nullptr);
    if (job == 0) {
        VdError ffiError{};
        *error = vd_service_error(service, &ffiError) == VD_STATUS_READY
                     ? QString::fromUtf8(ffiError.message)
                     : QStringLiteral("the project expansion was rejected without a reason");
        vd_service_destroy(service);
        return files;
    }
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        VdProjectInfo info{};
        const qint32 status = vd_project_poll(service, job, &info);
        if (status == VD_STATUS_READY) {
            for (size_t index = 0; index < info.files.count; ++index) {
                files.append(QString::fromUtf8(info.files.items[index]));
            }
            vd_project_info_free(&info);
            break;
        }
        if (status != VD_STATUS_PENDING) {
            VdError ffiError{};
            *error = vd_service_error(service, &ffiError) == VD_STATUS_READY
                         ? QString::fromUtf8(ffiError.message)
                         : QStringLiteral("the project expansion failed without a reason");
            break;
        }
        if (timer.elapsed() > 60000) {
            *error = QStringLiteral("the project expansion did not finish in 60 s");
            break;
        }
        QCoreApplication::processEvents();
        QTest::qWait(2);
    }
    vd_service_destroy(service);
    return files;
}

bool listContainsFile(const QStringList &files, const QString &fileName)
{
    for (const QString &file : files) {
        if (QFileInfo(file).fileName() == fileName) {
            return true;
        }
    }
    return false;
}

// Batch 20261110d real-fixture probe. Root = the fixture's `filelists/` folder,
// whose `.f` files name sources in `../tb/` and `../rtl/` — a directory walk of
// the root finds NOTHING, while the project model names 2+ files. The folder
// carries its own `vedit.toml` with `[project] auto_discover = true` (the
// 2026-11-18 file-set decision makes declared filelists the default; see
// docs/USER_GUIDE.md §2.8).
//
//   * three-way count at the data level: the scan reads exactly the set the
//     lint pass and the file tree publish (same expansion, same count);
//   * negative control: remove `-f nested/tb_aux.f` from `tb.f` and the member
//     it added leaves the set AND the results, while the rest still matches.
void testProjectExpansionScope()
{
    const QString fixture = QStringLiteral(VEDIT_SEARCH_EXPANSION_FIXTURE);
    if (!QFileInfo(fixture).isDir()) {
        skipCheck("projectexp.fixture", "the repository fixture is not present in this build");
        return;
    }
    QString error;
    const QStringList files = expandProjectFiles(fixture, &error);
    std::printf("PROJECT-EXPANSION root='%s' files=%d error='%s'\n", qPrintable(fixture),
                static_cast<int>(files.size()), qPrintable(error));
    std::fflush(stdout);
    check("projectexp.expansion", error.isEmpty() && files.size() >= 2);
    if (!error.isEmpty() || files.size() < 2) {
        skipCheck("projectexp.search", "the project expansion did not produce the pinned set");
        return;
    }
    const QString prefix = QFileInfo(fixture).absoluteFilePath() + QLatin1Char('/');
    int outside = 0;
    for (const QString &file : files) {
        if (!QFileInfo(file).absoluteFilePath().startsWith(prefix)) {
            ++outside;
        }
    }
    check("projectexp.expansion.all-outside-root", outside == files.size());

    SearchPanel panel;
    panel.setRootPath(fixture);
    panel.setProjectFilesProvider([files]() { return files; });
    panel.setQuery(QStringLiteral("tb_aux"));
    panel.startSearch();
    check("projectexp.search.terminates", waitForSearch(panel));
    const SearchSummary summary = panel.lastSummary();
    check("projectexp.search.scanned-the-set",
          summary.usedProjectFileSet && summary.filesScanned >= files.size() &&
              summary.filesInProjectSet == files.size());
    std::printf("PROJECT-EXPANSION-SCAN filesScanned=%d set=%d\n", summary.filesScanned,
                summary.filesInProjectSet);
    std::fflush(stdout);
    bool hitOutside = false;
    for (const SearchHit &hit : panel.hits()) {
        if (!hit.filePath.startsWith(prefix)) {
            hitOutside = true;
        }
    }
    check("projectexp.search.finds-outside-root", hitOutside && !panel.hits().isEmpty());

    // The union's other half at the real fixture: `+define+SIMULATION` lives in
    // `filelists/tb.f`, a folder file that is NOT a project SOURCE member. It
    // must still be searchable (searching the set alone would miss it).
    panel.setQuery(QStringLiteral("SIMULATION"));
    panel.startSearch();
    check("projectexp.search.folder-file-still-hit",
          waitForSearch(panel) && panel.hits().size() == 1 &&
              panel.hits().at(0).filePath.startsWith(prefix));

    // --- negative control on a COPY of the real fixture ---------------------
    QTemporaryDir copy;
    check("projectexp.copy", copy.isValid());
    if (!copy.isValid()) {
        return;
    }
    // `fixture` is the `filelists/` folder (the opened root); the project the
    // copy mirrors is its parent.
    const QString fixtureProject = QFileInfo(fixture).absolutePath();
    const auto copyFile = [&copy, &fixtureProject](const QString &relative) {
        const QString target = copy.path() + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(target).absolutePath());
        return QFile::copy(fixtureProject + QLatin1Char('/') + relative, target);
    };
    const bool copiedExtra = copyFile(QStringLiteral("filelists/extra.f"));
    // The opened root's own `vedit.toml` is what declares the legacy
    // `auto_discover` switch; without it the copied root names no file and the
    // negative control would have nothing to assert (2026-11-18 file-set
    // decision, docs/USER_GUIDE.md §2.8).
    const bool copiedConfig = copyFile(QStringLiteral("filelists/vedit.toml"));
    const bool copiedTop = copyFile(QStringLiteral("tb/tb_top.sv"));
    const bool copiedAux = copyFile(QStringLiteral("tb/tb_aux.sv"));
    const bool copiedHeader = copyFile(QStringLiteral("tb/include/tb_defs.vh"));
    const bool copiedMem = copyFile(QStringLiteral("rtl/mem.sv"));
    const bool copied = copiedExtra && copiedTop && copiedAux && copiedHeader && copiedMem;
    // tb.f without the nested `-f` line: the second-level member disappears.
    QString tbFile;
    {
        QFile source(fixtureProject + QStringLiteral("/filelists/tb.f"));
        if (source.open(QIODevice::ReadOnly)) {
            QStringList lines = QString::fromUtf8(source.readAll()).split(QLatin1Char('\n'));
            lines.erase(std::remove_if(lines.begin(), lines.end(),
                                       [](const QString &line) {
                                           return line.trimmed() == QStringLiteral("-f nested/tb_aux.f");
                                       }),
                        lines.end());
            tbFile = lines.join(QLatin1Char('\n'));
        }
    }
    QDir().mkpath(copy.path() + QStringLiteral("/filelists"));
    writeFile(copy.path() + QStringLiteral("/filelists/tb.f"), tbFile.toUtf8());
    std::printf("PROJECTEXP-COPY extra=%d config=%d tb_top=%d tb_aux=%d header=%d mem=%d "
                "tb-file-bytes=%d\n",
                copiedExtra ? 1 : 0, copiedConfig ? 1 : 0, copiedTop ? 1 : 0,
                copiedAux ? 1 : 0, copiedHeader ? 1 : 0, copiedMem ? 1 : 0,
                static_cast<int>(tbFile.toUtf8().size()));
    std::fflush(stdout);
    check("projectexp.copy.files", copied && !tbFile.isEmpty());

    const QString copyRoot = copy.path() + QStringLiteral("/filelists");
    error.clear();
    const QStringList withoutAux = expandProjectFiles(copyRoot, &error);
    std::printf("PROJECT-EXPANSION-NEG root='%s' files=%d error='%s'\n", qPrintable(copyRoot),
                static_cast<int>(withoutAux.size()), qPrintable(error));
    std::fflush(stdout);
    check("projectexp.negative.expansion", error.isEmpty() && !withoutAux.isEmpty());
    check("projectexp.negative.member-removed", !listContainsFile(withoutAux, QStringLiteral("tb_aux.sv")));
    check("projectexp.negative.other-member-kept",
          listContainsFile(withoutAux, QStringLiteral("tb_top.sv")));

    SearchPanel negative;
    negative.setRootPath(copyRoot);
    negative.setProjectFilesProvider([withoutAux]() { return withoutAux; });
    negative.setQuery(QStringLiteral("tb_aux"));
    negative.startSearch();
    check("projectexp.negative.terminates", waitForSearch(negative));
    check("projectexp.negative.no-hit", negative.hits().isEmpty());
    negative.setQuery(QStringLiteral("tb_top"));
    negative.startSearch();
    check("projectexp.negative.other-member-hits",
          waitForSearch(negative) && !negative.hits().isEmpty());
}
#endif // VEDIT_SEARCH_HAVE_FFI && VEDIT_SEARCH_EXPANSION_FIXTURE

void testJump(const Fixture &fixture)
{
    SearchPanel panel;
    panel.resize(900, 600);
    panel.show();
    QCoreApplication::processEvents();
    panel.setRootPath(fixture.root());
    RecordingJumpTarget target;
    // A-1/T20: the panel has no per-panel target seam any more; it forwards the
    // request to the application-wide registry. Installing the recording target
    // there is the harness standing in for the product target.
    vedit::workspace::jump::setTarget(&target);
    panel.setQuery(QStringLiteral("always_ff"));
    panel.startSearch();
    waitForSearch(panel);

    QTreeWidgetItem *hitItem = findHitItem(panel, QStringLiteral("b.sv"), 4);
    check("jump.hit-row-exists", hitItem != nullptr);
    if (hitItem != nullptr) {
        panel.resultsView()->scrollToItem(hitItem);
        QCoreApplication::processEvents();
        const QRect rect = panel.resultsView()->visualItemRect(hitItem);
        check("jump.row-visible", rect.isValid() && !rect.isEmpty());
        if (rect.isValid() && !rect.isEmpty()) {
            QTest::mouseClick(panel.resultsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                              rect.center());
            QCoreApplication::processEvents();
        }
        check("jump.click.recorded", target.requests.size() == 1);
        if (target.requests.size() == 1) {
            const JumpRequest &request = target.requests.at(0);
            check("jump.click.file", request.filePath.endsWith(QStringLiteral("/b.sv")));
            check("jump.click.line", request.line == 4);
            check("jump.click.column", request.column == 1);
        }
    }

    // A file row toggles the group; it must not jump anywhere.
    const int before = target.requests.size();
    if (QTreeWidgetItem *fileRow = topLevelForFile(panel, QStringLiteral("b.sv"))) {
        panel.resultsView()->scrollToItem(fileRow);
        QCoreApplication::processEvents();
        const QRect rect = panel.resultsView()->visualItemRect(fileRow);
        if (rect.isValid() && !rect.isEmpty()) {
            QTest::mouseClick(panel.resultsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                              rect.center());
            QCoreApplication::processEvents();
        }
    }
    check("jump.file-row.no-jump", target.requests.size() == before);

    // The demo consumer proves the whole path: click -> open file -> line.
    QPlainTextEdit editor;
    DemoJumpTarget demo(&editor);
    vedit::workspace::jump::setTarget(&demo);
    check("jump.activate", panel.activateHit(0));
    check("jump.demo.opened", editor.toPlainText().contains(QStringLiteral("always_ff")));
    const int block = editor.textCursor().blockNumber();
    check("jump.demo.line", block + 1 == panel.hits().at(0).line);
    vedit::workspace::jump::clearTargetIf(&demo);
}

#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
void testShellSeam(const Fixture &fixture)
{
    QTemporaryDir stateDir;
    vedit::app::ShellOptions options;
    options.stateDir = stateDir.path();
    options.projectRoot = fixture.root();
    options.restoreSession = false;
    options.showDefaultPanels = false;

    vedit::app::ApplicationShell shell(options);
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = fixture.root();
    integrationOptions.stateDir = stateDir.path();
    vedit::workspace::installSearch(shell, integrationOptions);
    shell.panels()->showPanel(QStringLiteral("search"), &shell);

    QDockWidget *dock = shell.panels()->dock(QStringLiteral("search"));
    check("seam.dock-created", dock != nullptr);
    check("seam.not-placeholder", dock != nullptr && dock->widget() != nullptr &&
                                      dock->widget()->objectName() !=
                                          QStringLiteral("panel-placeholder-search"));
    auto *panel = dock != nullptr ? shell.findChild<SearchPanel *>() : nullptr;
    check("seam.factory-installed", panel != nullptr);
    check("seam.root-applied",
          panel != nullptr && panel->rootPath() == QFileInfo(fixture.root()).absoluteFilePath());
    if (panel != nullptr) {
        panel->setQuery(QStringLiteral("always_ff"));
        panel->startSearch();
        waitForSearch(*panel);
        check("seam.search-in-shell", panel->hits().size() == 7);

        // Batch 20261110d: the PRODUCT wiring (installSearch -> the published
        // `vedit.diagnostics.projectFiles` property) makes a project member
        // that lives OUTSIDE the shell root searchable. Not the panel API: the
        // property is what the project model publishes.
        QTemporaryDir outsideDir;
        const QString outsideFile = outsideDir.path() + QStringLiteral("/tb_member.sv");
        writeFile(outsideFile, "seam_probe only outside the root\n");
        shell.setProperty("vedit.diagnostics.projectFiles",
                          QStringList{QFileInfo(fixture.path(QStringLiteral("a.v")))
                                          .absoluteFilePath(),
                                      outsideFile});
        panel->setQuery(QStringLiteral("seam_probe"));
        panel->startSearch();
        waitForSearch(*panel);
        check("seam.project-set.used", panel->lastSummary().usedProjectFileSet);
        // Union: the shell root's own files are scanned as well, so the scan
        // count is at least the two members.
        check("seam.project-set.scans-members", panel->lastSummary().filesScanned >= 2);
        check("seam.project-set.finds-outside-member", hasHitForFile(*panel, outsideFile));
    }
}

// The PRODUCTION jump path (verdict H1): installSearch() installs the consumer of
// jumpRequested, so a real click on a hit row has to open the hit file in the
// shell's editor and put the cursor on the hit line. Everything asserted below is
// the shell's own editor state (document text, cursor, open-file property, status
// bar) - no demo target is injected by this harness here.
void testProductJump(const Fixture &fixture)
{
    QTemporaryDir stateDir;
    vedit::app::ShellOptions options;
    options.stateDir = stateDir.path();
    options.projectRoot = fixture.root();
    options.restoreSession = false;
    options.showDefaultPanels = false;

    std::unique_ptr<vedit::app::ApplicationShell> shell(new vedit::app::ApplicationShell(options));
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    // Regression T69: the PRODUCT editor provider is KTextEditor (src/app/main.cpp
    // installs makeEditCoreProvider()). The jump must be verified on that
    // provider; running only against the demo QPlainTextEdit is what hid the
    // regression (the demo and the product have different widget types).
    shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
#endif
    vedit::workspace::IntegrationOptions integrationOptions;
    integrationOptions.projectRoot = fixture.root();
    integrationOptions.stateDir = stateDir.path();
    vedit::workspace::installSearch(*shell, integrationOptions);
    shell->panels()->showPanel(QStringLiteral("search"), shell.get());
    if (QDockWidget *dock = shell->panels()->dock(QStringLiteral("search"))) {
        shell->resizeDocks({dock}, {420}, Qt::Vertical);
    }
    shell->resize(1280, 860);
    shell->show();
    QCoreApplication::processEvents();

    SearchPanel *panel = shell->findChild<SearchPanel *>();
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    check("jump.product.shell-ready", panel != nullptr && pane != nullptr);
    if (panel == nullptr || pane == nullptr) {
        return;
    }

#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    // A-2/T20: the view/document belong to the pane's ACTIVE TAB, not to the
    // pane for the whole test. The second hit opens as a NEW TAB (T12), so a
    // pointer cached from the first tab reads the first document forever - the
    // stale-cache bug that kept jump.product.second-* red. Re-resolve them from
    // `pane->editorWidget()` after every click; the assertions themselves are
    // unchanged (same checks, same expected file/line/column).
    KTextEditor::View *view = nullptr;
    KTextEditor::Document *document = nullptr;
    const auto refreshTab = [pane, &view, &document]() {
        view = vedit::editor::viewFor(pane->editorWidget());
        document = vedit::editor::documentFor(pane->editorWidget());
    };
    refreshTab();
    check("jump.product.ktexteditor-provider", view != nullptr && document != nullptr);
    if (view == nullptr || document == nullptr) {
        return;
    }
    const auto documentText = [&document]() {
        return document != nullptr ? document->text() : QString();
    };
    const auto cursorLine = [&view]() {
        return view != nullptr ? view->cursorPosition().line() + 1 : 0;
    };
    const auto cursorColumn = [&view]() {
        return view != nullptr ? view->cursorPosition().column() + 1 : 0;
    };
#else
    check("jump.product.ktexteditor-provider", false);
    return;
#endif

    // The product target is the one installSearch() registered in the
    // application-wide registry (A-1/T20); the panel no longer owns one.
    check("jump.product.target-installed", vedit::workspace::jump::hasTarget());
    check("jump.product.initially-no-file", pane->currentFilePath().isEmpty());

    panel->setQuery(QStringLiteral("always_ff"));
    panel->startSearch();
    waitForSearch(*panel);
    QCoreApplication::processEvents();

    // Two hits in two different files, both below line 1, so the assertions can
    // only pass when the open really positions the cursor (a file opening at the
    // default position would land on line 1 and the same document twice would
    // hide a stale-cursor bug).
    const QList<SearchHit> &hits = panel->hits();
    int firstIndex = -1;
    int secondIndex = -1;
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i).line < 2) {
            continue;
        }
        if (firstIndex < 0) {
            firstIndex = i;
            continue;
        }
        if (hits.at(i).filePath != hits.at(firstIndex).filePath
            && hits.at(i).line != hits.at(firstIndex).line) {
            secondIndex = i;
            break;
        }
    }
    check("jump.product.two-hits", firstIndex >= 0 && secondIndex >= 0);
    if (firstIndex < 0 || secondIndex < 0) {
        return;
    }

    const auto readAll = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    const SearchHit hitA = hits.at(firstIndex);
    const SearchHit hitB = hits.at(secondIndex);
    const QString expectedA = readAll(hitA.filePath);
    const QString expectedB = readAll(hitB.filePath);

    check("jump.product.click-first", clickHitRow(*panel, hitA));
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    refreshTab();
#endif
    std::printf("SEARCH-JUMP first requested='%s':%d current='%s' cursor=%d:%d\n",
                qPrintable(hitA.relativePath), hitA.line,
                qPrintable(pane->currentFilePath()), cursorLine(), cursorColumn());
    check("jump.product.first-current-file", pane->currentFilePath() == hitA.filePath);
    check("jump.product.first-loads-bytes",
          !expectedA.isEmpty() && documentText() == expectedA);
    check("jump.product.first-cursor-line", cursorLine() == hitA.line);
    check("jump.product.first-cursor-column", cursorColumn() == hitA.column);
    check("jump.product.first-status",
          shell->statusBar()->currentMessage().contains(hitA.filePath));

    check("jump.product.click-second", clickHitRow(*panel, hitB));
#if defined(VEDIT_SEARCH_HAVE_EDITOR_CORE)
    refreshTab();
#endif
    std::printf("SEARCH-JUMP second requested='%s':%d current='%s' cursor=%d:%d\n",
                qPrintable(hitB.relativePath), hitB.line,
                qPrintable(pane->currentFilePath()), cursorLine(), cursorColumn());
    check("jump.product.second-current-file", pane->currentFilePath() == hitB.filePath);
    check("jump.product.second-loads-bytes",
          !expectedB.isEmpty() && documentText() == expectedB);
    check("jump.product.second-cursor-line", cursorLine() == hitB.line);
    check("jump.product.second-cursor-column", cursorColumn() == hitB.column);

    // T121: the second open replaces the document in the SAME view while the
    // folds registered for the first document are released. A stale folding tree
    // used to crash katepart inside the next paint
    // (Kate::TextFolding::foldingRangesStartingOnLine -> SIGSEGV). Pump real
    // paint events and assert the view/document are still coherent: the crash is
    // then a red test, not a user's lost session.
    for (int pump = 0; pump < 5; ++pump) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QCoreApplication::sendPostedEvents(nullptr, static_cast<int>(QEvent::Paint));
    }
    check("jump.product.second-open-paint-stable",
          documentText() == expectedB && cursorLine() == hitB.line
              && pane->currentFilePath() == hitB.filePath);
}

#endif // VEDIT_WORKSPACE_SEARCH_HAVE_SHELL

void testLargeFileAndCancel()
{
    QTemporaryDir temp;
    const QString root = temp.path();
    const QString bulk = root + QStringLiteral("/bulk.txt");
    {
        QFile file(bulk);
        if (!file.open(QIODevice::WriteOnly)) {
            check("large.fixture-created", false);
            return;
        }
        QByteArray block;
        block.reserve(1 << 20);
        for (int i = 0; i < 40000; ++i) {
            block.append("padding padding padding\n");
        }
        for (int i = 0; i < 100; ++i) {
            file.write(block);
        }
        file.close();
    }
    check("large.fixture-created", QFileInfo(bulk).size() > 64LL * 1024 * 1024);

    // (a) the default size limit skips the file quickly and reports it
    {
        GlobalSearch search;
        QList<SearchHit> hits;
        SearchSummary summary;
        bool done = false;
        QObject::connect(&search, &GlobalSearch::hitsReady,
                         [&hits](const QList<SearchHit> &batch) { hits.append(batch); });
        QObject::connect(&search, &GlobalSearch::finished,
                         [&summary, &done](const SearchSummary &value) {
                             summary = value;
                             done = true;
                         });
        SearchOptions options;
        options.rootPath = root;
        options.query = QStringLiteral("padding");
        options.maxFileBytes = 8LL * 1024 * 1024;
        QElapsedTimer timer;
        timer.start();
        check("large.start.accepted", search.start(options));
        check("large.skipped-by-default",
              waitUntil([&done]() { return done; }, 20000) && summary.skippedTooLarge == 1 &&
                  hits.isEmpty());
        check("large.skipped.state", summary.state == SearchState::Done &&
                                         summary.filesScanned == 0);
        check("large.skipped.fast", timer.elapsed() < 5000);
    }

    // (b) a raised limit scans on the worker thread; start does not block and
    //     cancel stops the scan
    {
        GlobalSearch search;
        QList<SearchHit> hits;
        SearchSummary summary;
        bool done = false;
        QObject::connect(&search, &GlobalSearch::hitsReady,
                         [&hits](const QList<SearchHit> &batch) { hits.append(batch); });
        QObject::connect(&search, &GlobalSearch::finished,
                         [&summary, &done](const SearchSummary &value) {
                             summary = value;
                             done = true;
                         });
        SearchOptions options;
        options.rootPath = root;
        options.query = QStringLiteral("1234567890");
        options.maxFileBytes = 256LL * 1024 * 1024;
        QElapsedTimer startTimer;
        startTimer.start();
        check("large.start.accepted-raised", search.start(options));
        const qint64 startMs = startTimer.elapsed();
        check("large.start.non-blocking", startMs < 100);
        QTest::qWait(5);
        QElapsedTimer cancelTimer;
        cancelTimer.start();
        search.cancel();
        const qint64 cancelMs = cancelTimer.elapsed();
        check("large.cancel.returns-fast", cancelMs < 100);
        check("large.cancel.finishes", waitUntil([&done]() { return done; }, 20000));
        check("large.cancel.state", summary.state == SearchState::Cancelled);
        check("large.cancel.bounded", summary.elapsedMs < 15000);
        check("large.worker-thread", search.ranOnWorkerThread());
    }

    // (c) a large tree (2000 files) neither blocks the UI nor loses results
    {
        QTemporaryDir treeTemp;
        const QString treeRoot = treeTemp.path();
        constexpr int kFileCount = 2000;
        for (int i = 0; i < kFileCount; ++i) {
            writeFile(treeRoot +
                          QStringLiteral("/module_%1.v").arg(i, 4, 10, QLatin1Char('0')),
                      "module m;\n// always_ff marker\nendmodule\n");
        }
        SearchPanel panel;
        panel.setRootPath(treeRoot);
        panel.setQuery(QStringLiteral("always_ff"));
        QElapsedTimer timer;
        timer.start();
        panel.startSearch();
        const qint64 startMs = timer.elapsed();
        check("many.start.non-blocking", startMs < 100);
        check("many.finishes", waitForSearch(panel, 60000));
        check("many.hits", panel.hits().size() == kFileCount);
        check("many.files-scanned", panel.lastSummary().filesScanned == kFileCount);
        check("many.elapsed-bounded", panel.lastSummary().elapsedMs < 15000);
    }

    // (d) the time budget stops the scan and says so
    {
        GlobalSearch search;
        SearchSummary summary;
        bool done = false;
        QObject::connect(&search, &GlobalSearch::finished,
                         [&summary, &done](const SearchSummary &value) {
                             summary = value;
                             done = true;
                         });
        SearchOptions options;
        options.rootPath = root;
        options.query = QStringLiteral("1234567890");
        options.maxFileBytes = 256LL * 1024 * 1024;
        options.maxDurationMs = 30;
        check("large.timeout.accepted", search.start(options));
        check("large.timeout.finishes", waitUntil([&done]() { return done; }, 20000));
        check("large.timeout.flag", summary.timedOut);
        check("large.timeout.state", summary.state == SearchState::Done);
        check("large.timeout.bounded", summary.elapsedMs < 5000);
    }
}

// T64 / verdict M2 (GlobalSearch destructor use-after-free), part 1: a normal
// in-flight scan must be joined before the object dies, so no thread of the scan
// can outlive it. Fast guard; the timeout window itself is reproduced by
// runSlowLifecycleProbe() below through the *.slowio stall shim.
#if defined(Q_OS_UNIX)
bool workerThreadPresent()
{
    const QDir tasks(QStringLiteral("/proc/self/task"));
    const QStringList ids = tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : ids) {
        QFile comm(QStringLiteral("/proc/self/task/%1/comm").arg(id));
        if (comm.open(QIODevice::ReadOnly)
            && QString::fromUtf8(comm.readAll()).trimmed()
                   == QStringLiteral("vedit-global-search")) {
            return true;
        }
    }
    return false;
}
#endif

void testDestructionJoinsWorker()
{
#if defined(Q_OS_UNIX)
    QTemporaryDir temp;
    check("lifecycle.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return;
    }
    const QString root = temp.path();
    QByteArray lines;
    for (int i = 0; i < 200; ++i) {
        lines.append("always_ff lifecycle line\n");
    }
    writeFile(root + QStringLiteral("/a.txt"), lines);
    writeFile(root + QStringLiteral("/b.txt"), lines);

    check("lifecycle.no-worker-before", !workerThreadPresent());
    auto *search = new GlobalSearch;
    QObject::connect(search, &GlobalSearch::hitsReady, [](const QList<SearchHit> &) {});
    SearchOptions options;
    options.rootPath = root;
    options.query = QStringLiteral("always_ff");
    check("lifecycle.scan-started", search->start(options));
    check("lifecycle.worker-running", search->isRunning());
    delete search;
    check("lifecycle.destructor-joins-worker", !workerThreadPresent());
    QCoreApplication::processEvents();
#else
    skipCheck("lifecycle.destructor-joins-worker", "POSIX /proc check is Unix-only");
#endif
}

// T64 / verdict M2 (GlobalSearch destructor use-after-free), part 2: the real
// timeout window. The scanner's only long, cancel-blind section is a blocking
// read; this machine has no slow storage, so the child process is started with
// the lane's stall shim (tools/slowio), which makes read() on a *.slowio file
// take 11.5 s - exactly the "scan budget (15 s) > destructor bound (10 s)"
// condition that freed the QObject under the worker.
//
// The child asserts the destructor returned only AFTER the stalled read
// finished (marker file written by the shim): a destructor that joins the
// worker can only return after the worker exited, a bounded one returns first.
// Run under ASan, the pre-fix child also aborts with heap-use-after-free.
int runSlowLifecycleProbe()
{
#if defined(Q_OS_UNIX)
    constexpr int kStallMs = 11500; // > the old 10 s destructor bound
    QTemporaryDir temp;
    check("slowlifecycle.temp-dir", temp.isValid());
    if (!temp.isValid()) {
        return 1;
    }
    const QString root = temp.path();
    QByteArray lines;
    for (int i = 0; i < 70; ++i) {
        lines.append("always_ff line\n");
    }
    writeFile(root + QStringLiteral("/a.txt"), lines);
    const QString slowPath = root + QStringLiteral("/zz_slow.slowio");
    writeFile(slowPath, QByteArray("always_ff on the slow disk\n"));
    const int before = g_failures;

    auto *search = new GlobalSearch;
    QList<SearchHit> delivered;
    QObject::connect(search, &GlobalSearch::hitsReady,
                     [&delivered](const QList<SearchHit> &batch) { delivered += batch; });
    SearchOptions options;
    options.rootPath = root;
    options.query = QStringLiteral("always_ff");
    options.maxDurationMs = 60000;
    check("slowlifecycle.scan-started", search->start(options));
    check("slowlifecycle.batch-before-block",
          waitUntil([&delivered]() { return delivered.size() >= 64; }, 30000));
    // a.txt is scanned and its batch is out, so the scan is in the stalled read.
    QTest::qWait(200);

    QElapsedTimer timer;
    timer.start();
    delete search;
    const qint64 destroyMs = timer.elapsed();
    const bool released = QFileInfo(slowPath + QStringLiteral(".released")).exists();
    std::printf("SLOWLIFECYCLE delete-returned elapsedMs=%lld released-marker=%d stallMs=%d\n",
                static_cast<long long>(destroyMs), released ? 1 : 0, kStallMs);
    check("slowlifecycle.destructor-waits-for-stalled-read", released);
    if (!released) {
        // Pre-fix: the worker is still inside read(); let it resume (and, under
        // ASan, report the use-after-free) before reporting.
        waitUntil([&slowPath]() { return QFileInfo(slowPath + QStringLiteral(".released")).exists(); },
                  10000);
        QCoreApplication::processEvents();
    }
    std::printf("WORKSPACE-SEARCH-SLOWLIFECYCLE ok=%d checks=%d failures=%d\n",
                g_failures == before ? 1 : 0, g_checks, g_failures);
    return g_failures == before ? 0 : 1;
#else
    skipCheck("slowlifecycle.destructor-waits-for-stalled-read", "POSIX stall shim is Unix-only");
    return 0;
#endif
}

// Parent side of part 2: re-execute this binary with the lane's stall shim in
// LD_PRELOAD and require a clean child run.
void testDestructorAgainstStalledRead()
{
#if defined(Q_OS_UNIX) && defined(VEDIT_SEARCH_SLOWIO_LIB)
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral(VEDIT_SEARCH_SLOWIO_LIB));
    child.setProcessEnvironment(env);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(),
                QStringList{QStringLiteral("--lifecycle-slow")});
    const bool finished = child.waitForFinished(90000);
    const int exitCode = finished ? child.exitCode() : -1;
    const QString output = QString::fromUtf8(child.readAll());
    std::printf("SLOWLIFECYCLE-CHILD finished=%d exit=%d\n%s",
                finished ? 1 : 0, exitCode, qPrintable(output));
    check("lifecycle.stall-child-finished", finished);
    check("lifecycle.stall-child-pass", exitCode == 0);
#else
    skipCheck("lifecycle.stall-child-pass", "no POSIX stall shim in this build");
#endif
}

#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
int captureScreenshots(const QString &directory)
{
    Fixture fixture;
    if (!fixture.valid()) {
        std::printf("SCREENSHOTS FAILED: fixture directory not created\n");
        return 1;
    }
    QDir().mkpath(directory);
    const QString root = fixture.root();
    int failures = 0;

    const auto grab = [&directory, &failures](QWidget *widget, const QString &name) {
        const QString path = directory + QLatin1Char('/') + name;
        const QPixmap pixmap = widget->grab();
        const bool saved = !pixmap.isNull() && pixmap.save(path, "PNG");
        std::printf("SHOT %s %s %dx%d\n", qPrintable(path), saved ? "written" : "FAILED",
                    pixmap.width(), pixmap.height());
        if (!saved) {
            ++failures;
        }
    };

    const auto makeShell = [&root](QTemporaryDir &stateDir, bool withProject) {
        vedit::app::ShellOptions options;
        options.stateDir = stateDir.path();
        options.projectRoot = withProject ? root : QString();
        options.restoreSession = false;
        options.showDefaultPanels = false;
        auto *shell = new vedit::app::ApplicationShell(options);
        vedit::workspace::IntegrationOptions integrationOptions;
        integrationOptions.projectRoot = withProject ? root : QString();
        integrationOptions.stateDir = stateDir.path();
        vedit::workspace::installSearch(*shell, integrationOptions);
        shell->panels()->showPanel(QStringLiteral("search"), shell);
        shell->resize(1280, 860);
        shell->show();
        QCoreApplication::processEvents();
        if (QDockWidget *dock = shell->panels()->dock(QStringLiteral("search"))) {
            shell->resizeDocks({dock}, {360}, Qt::Vertical);
        }
        return shell;
    };

    // 1) results in the real shell dock + jump into the demo editor pane
    {
        QTemporaryDir stateDir;
        std::unique_ptr<vedit::app::ApplicationShell> shell(makeShell(stateDir, true));
        QCoreApplication::processEvents();
        SearchPanel *panel = shell->findChild<SearchPanel *>();
        if (panel == nullptr) {
            std::printf("SCREENSHOTS FAILED: search panel not installed through the seam\n");
            ++failures;
        } else {
            panel->setQuery(QStringLiteral("always_ff"));
            panel->startSearch();
            waitForSearch(*panel);
            QCoreApplication::processEvents();
            grab(shell.get(), QStringLiteral("01-search-results.png"));

            QPlainTextEdit *editor = nullptr;
            if (vedit::app::EditorPane *pane = shell->editorArea()->activePane()) {
                editor = qobject_cast<QPlainTextEdit *>(pane->editorWidget());
            }
            if (editor != nullptr) {
                DemoJumpTarget target(editor);
                // The screenshot harness stands in for the product target in the
                // application-wide registry (A-1/T20); cleared before `target`
                // (a stack object) dies.
                vedit::workspace::jump::setTarget(&target);
                panel->activateHit(0);
                QCoreApplication::processEvents();
                vedit::workspace::jump::clearTargetIf(&target);
            }
            grab(shell.get(), QStringLiteral("03-search-jump.png"));

            panel->setQuery(QStringLiteral("no_such_symbol_anywhere"));
            panel->startSearch();
            waitForSearch(*panel);
            QCoreApplication::processEvents();
            grab(shell.get(), QStringLiteral("02-search-empty-state.png"));
        }
    }

    // 2) no project at all
    {
        QTemporaryDir stateDir;
        std::unique_ptr<vedit::app::ApplicationShell> shell(makeShell(stateDir, false));
        QCoreApplication::processEvents();
        grab(shell.get(), QStringLiteral("04-search-no-project.png"));
    }

    // 3) unreadable root (distinct from "no results")
    if (::geteuid() != 0) {
        QTemporaryDir stateDir;
        QTemporaryDir locked;
        const QString secret = locked.path() + QStringLiteral("/secret");
        QDir().mkpath(secret);
        writeFile(secret + QStringLiteral("/inside.txt"), "always_ff\n");
        QFile::setPermissions(secret, QFileDevice::Permissions());
        {
            vedit::app::ShellOptions options;
            options.stateDir = stateDir.path();
            options.projectRoot = secret;
            options.restoreSession = false;
            options.showDefaultPanels = false;
            vedit::app::ApplicationShell shell(options);
            vedit::workspace::IntegrationOptions integrationOptions;
            integrationOptions.projectRoot = secret;
            integrationOptions.stateDir = stateDir.path();
            vedit::workspace::installSearch(shell, integrationOptions);
            shell.panels()->showPanel(QStringLiteral("search"), &shell);
            shell.resize(1280, 860);
            shell.show();
            QCoreApplication::processEvents();
            if (QDockWidget *dock = shell.panels()->dock(QStringLiteral("search"))) {
                shell.resizeDocks({dock}, {360}, Qt::Vertical);
            }
            if (auto *panel = shell.findChild<SearchPanel *>()) {
                panel->setQuery(QStringLiteral("always_ff"));
                panel->startSearch();
                waitForSearch(*panel);
                QCoreApplication::processEvents();
            }
            grab(&shell, QStringLiteral("05-search-unreadable-root.png"));
        }
        QFile::setPermissions(secret, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner);
    }

    std::printf("SCREENSHOTS %s failures=%d\n", failures == 0 ? "ok=1" : "ok=0", failures);
    return failures == 0 ? 0 : 1;
}
#endif // VEDIT_WORKSPACE_SEARCH_HAVE_SHELL

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-workspace-search-selftest"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VEdit global search self-check (task T23)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("selfcheck"),
                      QStringLiteral("Run the global search self-checks and exit.")});
    parser.addOption({QStringLiteral("screenshots"),
                      QStringLiteral("Capture screenshots into this directory and exit."),
                      QStringLiteral("dir")});
    parser.addOption({QStringLiteral("lifecycle-slow"),
                      QStringLiteral("Internal: slow-storage lifecycle probe (child of "
                                     "--selfcheck, requires the *.slowio stall shim).")});
    parser.process(app);

    if (parser.isSet(QStringLiteral("lifecycle-slow"))) {
        return runSlowLifecycleProbe();
    }

    if (parser.isSet(QStringLiteral("screenshots"))) {
#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
        return captureScreenshots(parser.value(QStringLiteral("screenshots")));
#else
        std::printf("SCREENSHOTS unavailable: this build has no app shell (root build required)\n");
        return 2;
#endif
    }

    const Fixture fixture;
    check("fixture.created", fixture.valid());
    if (fixture.valid()) {
        testPanelBasics(fixture);
        testEmptyStates(fixture);
        testUnreadableEntries(fixture);
        testLimits(fixture);
        testExtensions();
        testJump(fixture);
#ifdef VEDIT_WORKSPACE_SEARCH_HAVE_SHELL
        testShellSeam(fixture);
        testProductJump(fixture);
#endif
    }
    // Batch 20261110d: the project file set is the search scope. These build
    // their own `.f`-shaped fixtures, so they do not depend on the shared one.
    testExplicitFileSet();
    testExplicitFileSetParity();
#if defined(VEDIT_SEARCH_HAVE_FFI) && defined(VEDIT_SEARCH_EXPANSION_FIXTURE)
    testProjectExpansionScope();
#endif
    testLargeFileAndCancel();
    testDestructionJoinsWorker();
    testDestructorAgainstStalledRead();

    std::printf("WORKSPACE-SEARCH-SELFTEST ok=%d checks=%d failures=%d skipped=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
