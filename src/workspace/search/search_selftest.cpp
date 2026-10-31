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
#include "search_integration.h"
#include "search_panel.h"
#include "search_types.h"

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

#include <cstdio>
#include <functional>
#include <memory>

#include <unistd.h>

using vedit::workspace::search::GlobalSearch;
using vedit::workspace::search::IJumpTarget;
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
    void jumpTo(const JumpRequest &request) override { requests.append(request); }
    QList<JumpRequest> requests;
};

// What edit-core will do for real: open the file and put the cursor at the hit.
class DemoJumpTarget final : public IJumpTarget
{
public:
    explicit DemoJumpTarget(QPlainTextEdit *editor)
        : editor_(editor)
    {
    }

    void jumpTo(const JumpRequest &request) override
    {
        requests.append(request);
        if (editor_ == nullptr) {
            return;
        }
        QFile file(request.filePath);
        if (file.open(QIODevice::ReadOnly)) {
            editor_->setPlainText(QString::fromUtf8(file.readAll()));
        }
        const int lineIndex = request.line > 0 ? request.line - 1 : 0;
        QTextCursor cursor(editor_->document()->findBlockByLineNumber(lineIndex));
        cursor.movePosition(QTextCursor::StartOfLine);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        editor_->setTextCursor(cursor);
        editor_->centerCursor();
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

void testJump(const Fixture &fixture)
{
    SearchPanel panel;
    panel.resize(900, 600);
    panel.show();
    QCoreApplication::processEvents();
    panel.setRootPath(fixture.root());
    RecordingJumpTarget target;
    panel.setJumpTarget(&target);
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
    panel.setJumpTarget(&demo);
    check("jump.activate", panel.activateHit(0));
    check("jump.demo.opened", editor.toPlainText().contains(QStringLiteral("always_ff")));
    const int block = editor.textCursor().blockNumber();
    check("jump.demo.line", block + 1 == panel.hits().at(0).line);
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
    KTextEditor::View *view = vedit::editor::viewFor(pane->editorWidget());
    KTextEditor::Document *document = vedit::editor::documentFor(pane->editorWidget());
    check("jump.product.ktexteditor-provider", view != nullptr && document != nullptr);
    if (view == nullptr || document == nullptr) {
        return;
    }
    const auto documentText = [document]() { return document->text(); };
    const auto cursorLine = [view]() { return view->cursorPosition().line() + 1; };
    const auto cursorColumn = [view]() { return view->cursorPosition().column() + 1; };
#else
    check("jump.product.ktexteditor-provider", false);
    return;
#endif

    check("jump.product.target-installed", panel->jumpTarget() != nullptr);
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
    std::printf("SEARCH-JUMP second requested='%s':%d current='%s' cursor=%d:%d\n",
                qPrintable(hitB.relativePath), hitB.line,
                qPrintable(pane->currentFilePath()), cursorLine(), cursorColumn());
    check("jump.product.second-current-file", pane->currentFilePath() == hitB.filePath);
    check("jump.product.second-loads-bytes",
          !expectedB.isEmpty() && documentText() == expectedB);
    check("jump.product.second-cursor-line", cursorLine() == hitB.line);
    check("jump.product.second-cursor-column", cursorColumn() == hitB.column);
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
                panel->setJumpTarget(&target);
                panel->activateHit(0);
                QCoreApplication::processEvents();
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
    testLargeFileAndCancel();
    testDestructionJoinsWorker();
    testDestructorAgainstStalledRead();

    std::printf("WORKSPACE-SEARCH-SELFTEST ok=%d checks=%d failures=%d skipped=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures, g_skips);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
