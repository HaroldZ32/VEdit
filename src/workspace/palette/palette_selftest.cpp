// Non-interactive selftest of the Ctrl+P palette lane (task T22, A2b).
//
// Covers: fuzzy ranking, the shared file index (one scan, publish path,
// incremental updates, ignored/hidden entries), palette filtering, the explicit
// "No matching files" empty state, Enter/Esc/arrow keyboard behaviour, and a
// 10,000-file keystroke measurement that must stay under the 200 ms budget.
//
// Run inside the pinned Qt6 image (or any host with Qt 6.10.2):
//   QT_QPA_PLATFORM=offscreen vedit-workspace-palette-selftest --screenshots <dir>
#include "command_palette.h"
#include "file_index.h"
#include "fuzzy_matcher.h"
#include "synthetic_paths.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>

namespace {

using namespace vedit::workspace;

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", qPrintable(name), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
}

bool saveWidget(QWidget *widget, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool ok = widget->grab().save(path);
    std::printf("SCREENSHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    std::fflush(stdout);
    return ok;
}

void sendKey(QWidget *target, int key, const QString &text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(target, &event);
}

void checkFuzzyMatcher()
{
    {
        const QStringList candidates{QStringLiteral("src/main.cpp"),
                                     QStringLiteral("src/domain/maintenance.cpp")};
        const auto results =
            FuzzyMatcher::filter(QStringLiteral("main"), candidates, QStringList(), 10);
        check(QStringLiteral("fuzzy.main-ranks-basename-first"),
              !results.isEmpty() && results.first().index == 0);
    }
    {
        const QStringList candidates{QStringLiteral("src/registry/foo.cpp"),
                                     QStringLiteral("src/foo/registry.cpp")};
        const auto results =
            FuzzyMatcher::filter(QStringLiteral("registry"), candidates, QStringList(), 10);
        check(QStringLiteral("fuzzy.basename-beats-directory"),
              !results.isEmpty() && results.first().index == 1);
    }
    {
        FuzzyMatch match;
        const bool matched = FuzzyMatcher::match(
            QStringLiteral("cp"), QStringLiteral("command_palette.cpp"),
            QStringLiteral("command_palette.cpp"), &match);
        check(QStringLiteral("fuzzy.subsequence"), matched);
        check(QStringLiteral("fuzzy.positions"),
              matched && match.positionCount == 2 && match.positions[0] == 0 &&
                  match.positions[1] == 8);
    }
    {
        FuzzyMatch match;
        check(QStringLiteral("fuzzy.case-insensitive"),
              FuzzyMatcher::match(QStringLiteral("CMP"),
                                  QStringLiteral("src/command_palette.cpp"),
                                  QStringLiteral("command_palette.cpp"), &match));
    }
    {
        FuzzyMatch match;
        check(QStringLiteral("fuzzy.no-match"),
              !FuzzyMatcher::match(QStringLiteral("zzz"), QStringLiteral("src/main.cpp"),
                                   QStringLiteral("main.cpp"), &match));
    }
    {
        const QStringList candidates{QStringLiteral("b"), QStringLiteral("a")};
        const auto results =
            FuzzyMatcher::filter(QString(), candidates, QStringList(), 10);
        check(QStringLiteral("fuzzy.empty-query-keeps-order"),
              results.size() == 2 && results[0].index == 0 && results[1].index == 1);
    }
    {
        QStringList many;
        for (int index = 0; index < 100; ++index) {
            many.append(QStringLiteral("file_%1.cpp").arg(index));
        }
        const auto results = FuzzyMatcher::filter(QString(), many, QStringList(), 10);
        check(QStringLiteral("fuzzy.limit"), results.size() == 10);
    }
}

void checkFileIndex(const QString &root)
{
    WorkspaceFileIndex index(root);
    QString error;
    check(QStringLiteral("index.scan"), index.scan(&error));
    check(QStringLiteral("index.scan-four-files"), index.size() == 4);

    const QStringList display = index.displayPaths();
    const QStringList expected{QStringLiteral("docs/readme.md"), QStringLiteral("src/main.cpp"),
                               QStringLiteral("src/util/helper.cpp"), QStringLiteral("top.v")};
    check(QStringLiteral("index.display-sorted"), display == expected);
    check(QStringLiteral("index.hidden-skipped"),
          !display.contains(QStringLiteral(".hidden/secret.cpp")) &&
              !display.contains(QStringLiteral(".git/objects/blob.txt")));
    check(QStringLiteral("index.ignored-dir-skipped"),
          !display.contains(QStringLiteral("node_modules/dep/index.js")));
    check(QStringLiteral("index.basenames"),
          index.basenames().size() == 4 && index.basenames().at(1) == QStringLiteral("main.cpp"));

    const int revisionAfterScan = index.revision();
    check(QStringLiteral("index.cached-scan"), index.scan(&error));
    check(QStringLiteral("index.no-rescan"), index.scanCount() == 1);
    check(QStringLiteral("index.no-change-on-cached-scan"),
          index.revision() == revisionAfterScan);

    index.addFile(QStringLiteral("src/new_file.cpp"));
    check(QStringLiteral("index.add-file"),
          index.size() == 5 && index.displayPaths().contains(QStringLiteral("src/new_file.cpp")));
    index.removeFile(QStringLiteral("src/new_file.cpp"));
    check(QStringLiteral("index.remove-file"), index.size() == 4);
    check(QStringLiteral("index.incremental-no-scan"), index.scanCount() == 1);

    WorkspaceFileIndex published(root);
    published.setFiles(QStringList{QStringLiteral("b.cpp"), QStringLiteral("a/z.cpp")});
    check(QStringLiteral("index.publish-no-scan"),
          published.scanCount() == 0 && published.size() == 2);
    check(QStringLiteral("index.publish-sorted"),
          published.displayPaths() ==
              QStringList{QStringLiteral("a/z.cpp"), QStringLiteral("b.cpp")});

    WorkspaceFileIndex capped(root);
    capped.setMaxFiles(2);
    capped.scan();
    check(QStringLiteral("index.max-files"), capped.size() == 2 && capped.truncated());
}

void checkPalette(const QString &root, WorkspaceFileIndex &diskIndex)
{
    CommandPalette palette;
    palette.setFileIndex(&diskIndex);
    palette.openPalette();
    check(QStringLiteral("palette.opens"), palette.isVisible());
    check(QStringLiteral("palette.lists-all"), palette.resultCount() == 4);
    check(QStringLiteral("palette.status-shows-total"),
          palette.findChild<QWidget *>(QStringLiteral("palette-status")) != nullptr);

    palette.setQueryText(QStringLiteral("main"));
    check(QStringLiteral("palette.filters"),
          palette.resultCount() == 1 && palette.visibleResults().size() == 1 &&
              palette.visibleResults().first() == QStringLiteral("src/main.cpp"));
    check(QStringLiteral("palette.filter-under-200ms"), palette.lastFilterMicros() < 200000);

    palette.setQueryText(QStringLiteral("zzzzz"));
    check(QStringLiteral("palette.empty-state"),
          palette.resultCount() == 0 && palette.emptyStateVisible() &&
              palette.emptyStateText() == CommandPalette::noResultsText());

    QString openedPath;
    palette.setOpenHandler([&openedPath](const QString &path) { openedPath = path; });
    palette.setQueryText(QStringLiteral("main"));
    sendKey(palette.queryEdit(), Qt::Key_Return);
    check(QStringLiteral("palette.enter-opens"),
          openedPath == root + QStringLiteral("/src/main.cpp"));
    check(QStringLiteral("palette.enter-closes"), !palette.isVisible());

    palette.openPalette();
    palette.setQueryText(QString());
    for (const QChar character : QStringLiteral("main")) {
        sendKey(palette.queryEdit(), character.toUpper().unicode(), QString(character));
    }
    check(QStringLiteral("palette.typing-filters"),
          palette.queryText() == QStringLiteral("main") && palette.resultCount() == 1);

    palette.openPalette();
    sendKey(palette.queryEdit(), Qt::Key_Escape);
    check(QStringLiteral("palette.escape-closes"), !palette.isVisible());

    openedPath.clear();
    palette.openPalette();
    palette.setQueryText(QString());
    sendKey(palette.queryEdit(), Qt::Key_Down);
    check(QStringLiteral("palette.arrow-selects"), palette.currentRow() == 1);
    sendKey(palette.queryEdit(), Qt::Key_Return);
    check(QStringLiteral("palette.arrow-enter-opens"),
          openedPath == root + QStringLiteral("/src/main.cpp"));

    CommandPalette bare;
    bare.openPalette();
    check(QStringLiteral("palette.no-project-empty-state"),
          bare.emptyStateVisible() &&
              bare.emptyStateText().contains(QStringLiteral("No project")));
    bare.closePalette();
}

void checkTenThousandFiles()
{
    WorkspaceFileIndex index;
    index.setFiles(makeSyntheticPaths(10000));
    check(QStringLiteral("index.synthetic-10000"), index.size() == 10000);

    CommandPalette palette;
    palette.setFileIndex(&index);
    palette.openPalette();
    palette.resize(820, 460);
    check(QStringLiteral("palette.10k-opens"), palette.resultCount() == palette.maxResults());

    const QStringList queries{QStringLiteral("m"),    QStringLiteral("ma"),
                              QStringLiteral("mai"),  QStringLiteral("main"),
                              QStringLiteral("cmd"),  QStringLiteral("pal"),
                              QStringLiteral("src"),  QStringLiteral("zzzz")};
    qint64 worstMicros = 0;
    bool underBudget = true;
    for (const QString &query : queries) {
        palette.setQueryText(query);
        worstMicros = std::max(worstMicros, palette.lastFilterMicros());
        std::printf("MEASURE palette.query=%s micros=%lld results=%d\n", qPrintable(query),
                    static_cast<long long>(palette.lastFilterMicros()), palette.resultCount());
        std::fflush(stdout);
        if (palette.lastFilterMicros() >= 200000) {
            underBudget = false;
        }
    }
    check(QStringLiteral("palette.10k-keystroke-under-200ms"), underBudget);
    std::printf("MEASURE palette.10k-worst-micros=%lld\n", static_cast<long long>(worstMicros));

    palette.setQueryText(QStringLiteral("main"));
    check(QStringLiteral("palette.10k-results-nonempty"), palette.resultCount() > 0);
    palette.setQueryText(QStringLiteral("zzzz"));
    check(QStringLiteral("palette.10k-empty-state"), palette.emptyStateVisible());
    check(QStringLiteral("palette.10k-never-scans-disk"), index.scanCount() == 0);
}

void captureScreenshots(const QString &directory, WorkspaceFileIndex &diskIndex)
{
    if (directory.isEmpty()) {
        return;
    }
    QDir().mkpath(directory);

    WorkspaceFileIndex index;
    index.setFiles(makeSyntheticPaths(10000));
    CommandPalette palette;
    palette.setFileIndex(&index);
    palette.openPalette();
    palette.resize(820, 460);

    palette.setQueryText(QStringLiteral("pal"));
    saveWidget(&palette, directory + QStringLiteral("/10-palette-filter.png"));

    palette.setQueryText(QStringLiteral("zzzz"));
    saveWidget(&palette, directory + QStringLiteral("/11-palette-empty-state.png"));

    CommandPalette noProject;
    noProject.resize(820, 460);
    noProject.openPalette();
    saveWidget(&noProject, directory + QStringLiteral("/12-palette-no-project.png"));

    CommandPalette disk;
    disk.setFileIndex(&diskIndex);
    disk.openPalette();
    disk.resize(820, 460);
    disk.setQueryText(QStringLiteral("main"));
    saveWidget(&disk, directory + QStringLiteral("/13-palette-project.png"));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-workspace-palette-selftest"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Selftest of the VEdit Ctrl+P file palette lane (T22 / A2b)."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("screenshots"),
                      QStringLiteral("Write palette screenshots into this directory."),
                      QStringLiteral("dir")});
    parser.process(app);

    QTemporaryDir temp;
    check(QStringLiteral("fixture.temp-dir"), temp.isValid());
    const QString root = temp.path() + QStringLiteral("/project");
    writeFile(root + QStringLiteral("/src/main.cpp"),
              QByteArray("int main() { return 0; } // src/main.cpp\n"));
    writeFile(root + QStringLiteral("/src/util/helper.cpp"), QByteArray("// helper\n"));
    writeFile(root + QStringLiteral("/docs/readme.md"), QByteArray("# readme\n"));
    writeFile(root + QStringLiteral("/top.v"), QByteArray("module top; endmodule\n"));
    writeFile(root + QStringLiteral("/.hidden/secret.cpp"), QByteArray("hidden\n"));
    writeFile(root + QStringLiteral("/.git/objects/blob.txt"), QByteArray("git\n"));
    writeFile(root + QStringLiteral("/node_modules/dep/index.js"), QByteArray("js\n"));

    checkFuzzyMatcher();
    checkFileIndex(root);

    WorkspaceFileIndex diskIndex(root);
    QString scanError;
    check(QStringLiteral("palette-fixture.scan"), diskIndex.scan(&scanError));
    checkPalette(root, diskIndex);
    checkTenThousandFiles();
    captureScreenshots(parser.value(QStringLiteral("screenshots")), diskIndex);

    std::printf("PALETTE-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
