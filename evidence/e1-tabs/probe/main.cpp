// T12 lane self-test — drives the REAL ApplicationShell, its real tab strip and
// its real Save/Discard/Cancel dialog. Two modes:
//
//   tabs-probe --checks                  headless assertions (offscreen)
//   tabs-probe --screenshots <dir>       assertions + PNG evidence (xcb)
//
// It links the product's own static libraries (see build.sh / run.sh); no
// product source is compiled into it, so what it observes is the shipped code.
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"

#include "ktexteditor_provider.h"

#include <KTextEditor/Document>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTemporaryDir>

#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <cstdio>

// The probe drives the shell lane the way src/app/main.cpp does.
using namespace vedit::app;

namespace {

int failures = 0;

void check(const char *id, bool ok, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", id,
                detail.isEmpty() ? "" : " — ", qPrintable(detail));
    if (!ok) {
        ++failures;
    }
}

void pump(int rounds = 6)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}


// A screenshot is taken only after the transient status message is cleared: a
// QStatusBar draws the temporary message over the normal widget area, and a grab
// taken mid-transition records both texts on top of each other.
bool saveShot(ApplicationShell *shell, const QString &path)
{
    if (shell != nullptr && shell->statusBar() != nullptr) {
        shell->statusBar()->clearMessage();
    }
    pump();
    const bool ok = shell != nullptr && shell->grab().save(path);
    std::printf("SHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    return ok;
}

QString g_shotDir;
QString g_dialogAnswer;
QString g_dialogShotName;

// Answers the modal dialog the shell raises, after grabbing it. The dialog runs
// its own event loop, so this must be driven from a timer that re-arms itself
// until the modal widget exists.
void driveModalDialog(int attempt = 0)
{
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr) {
        if (attempt < 60) {
            QTimer::singleShot(50, [attempt]() { driveModalDialog(attempt + 1); });
        }
        return;
    }
    if (!g_shotDir.isEmpty() && !g_dialogShotName.isEmpty()) {
        const QString path = g_shotDir + QLatin1Char('/') + g_dialogShotName;
        const bool ok = modal->grab().save(path);
        std::printf("SHOT %s %s\n", ok ? "OK" : "FAIL", qPrintable(path));
    }
    auto *box = qobject_cast<QMessageBox *>(modal);
    if (box == nullptr) {
        modal->close();
        return;
    }
    for (QAbstractButton *button : box->buttons()) {
        if (button->text() == g_dialogAnswer) {
            button->click();
            return;
        }
    }
    box->reject();
}

void armDialogAnswer(const QString &answer, const QString &shotName = QString())
{
    g_dialogAnswer = answer;
    g_dialogShotName = shotName;
    QTimer::singleShot(80, []() { driveModalDialog(); });
}

struct Fixture {
    QTemporaryDir temp;
    QString alpha;
    QString bravo;
    QString charlie;
    bool ok = false;

    Fixture()
    {
        ok = temp.isValid();
        if (!ok) {
            return;
        }
        alpha = temp.path() + QStringLiteral("/alpha.sv");
        bravo = temp.path() + QStringLiteral("/bravo.sv");
        charlie = temp.path() + QStringLiteral("/charlie_unit.sv");
        ok = writeFile(alpha, "module alpha;\nendmodule\n")
             && writeFile(bravo, "module bravo;\nendmodule\n")
             && writeFile(charlie, "module charlie_unit;\nendmodule\n");
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-tabs-probe"));

    bool screenshots = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--screenshots") && i + 1 < argc) {
            screenshots = true;
            g_shotDir = QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    if (screenshots && !QDir().mkpath(g_shotDir)) {
        std::printf("FAIL shot-dir\n");
        return 1;
    }

    Fixture fixture;
    check("probe.fixture", fixture.ok);

    ShellOptions options;
    options.stateDir = fixture.temp.path() + QStringLiteral("/state");
    options.restoreSession = false;
    options.showDefaultPanels = false;
    ApplicationShell shell(options);
    shell.resize(1440, 900);
    shell.show();
    pump();

    EditorArea *area = shell.editorArea();
    check("shell.one-pane", area != nullptr && area->paneCount() == 1);
    EditorPane *pane = area != nullptr ? area->activePane() : nullptr;
    check("shell.active-pane", pane != nullptr);
    if (pane == nullptr) {
        return 1;
    }

    // The shipped product editor, installed the way src/app/main.cpp installs it.
    shell.setEditorProvider(vedit::editor::makeEditCoreProvider());
    pump();
    check("editor.provider-is-ktexteditor", shell.editorProvider() != nullptr
              && shell.editorProvider()->id() == QLatin1String("ktexteditor"));

    // --- AC-9.1: the pane strip starts with ONE tab, not a provider header ---
    check("tabs.initial-count", pane->tabCount() == 1);
    check("tabs.strip-exists", pane->tabBar() != nullptr
              && pane->tabBar()->objectName() == QLatin1String("editor-tab-bar"));
    check("tabs.initial-label", pane->tabText(0) == QLatin1String("untitled"), pane->tabText(0));
    {
        bool providerNamed = false;
        for (QLabel *label : pane->findChildren<QLabel *>()) {
            if (label->text().contains(QStringLiteral("KTextEditor"), Qt::CaseInsensitive)
                || label->text().contains(QStringLiteral("edit-core"), Qt::CaseInsensitive)) {
                providerNamed = true;
            }
        }
        // AC-9.3: the old header line named the provider; nothing in the pane may.
        check("tabs.no-provider-name-in-pane", !providerNamed);
        check("tabs.style-has-selected-rule",
              pane->tabBar()->styleSheet().contains(QStringLiteral(":selected")));
    }

    // --- AC-9.1: three files -> three tabs named after the files ------------
    QString error;
    check("open.alpha", shell.openPaths({fixture.alpha}, &error), error);
    check("open.bravo", shell.openPaths({fixture.bravo}, &error), error);
    check("open.charlie", shell.openPaths({fixture.charlie}, &error), error);
    pump();
    check("tabs.three-opened", pane->tabCount() == 3,
          QStringLiteral("tabs=%1").arg(pane->tabCount()));
    check("tabs.same-pane", area->paneCount() == 1);
    check("tabs.file-names",
          pane->tabText(0) == QLatin1String("alpha.sv")
              && pane->tabText(1) == QLatin1String("bravo.sv")
              && pane->tabText(2) == QLatin1String("charlie_unit.sv"),
          QStringLiteral("%1 | %2 | %3")
              .arg(pane->tabText(0), pane->tabText(1), pane->tabText(2)));
    check("tabs.active-is-last-open", pane->activeTabIndex() == 2);
    check("tabs.open-paths", pane->openFilePaths().size() == 3
              && pane->openFilePaths().at(2) == QFileInfo(fixture.charlie).absoluteFilePath());
    check("tabs.current-file-is-active",
          pane->currentFilePath() == QFileInfo(fixture.charlie).absoluteFilePath(),
          pane->currentFilePath());
    if (screenshots) {
        const QString path = g_shotDir + QStringLiteral("/01-three-tabs.png");
        saveShot(&shell, path);
    }

    // --- AC-9.2: clicking a tab switches the document -----------------------
    pane->setActiveTab(0);
    pump();
    check("tabs.switch-file", pane->currentFilePath() == QFileInfo(fixture.alpha).absoluteFilePath());
    check("tabs.switch-index", pane->activeTabIndex() == 0);
    {
        KTextEditor::Document *switched = vedit::editor::documentFor(pane->editorWidget());
        // The load path fills the buffer (it deliberately does not go through
        // KParts::openUrl), so the identity of the switched-to document is its
        // text, and the file path is the pane's bookkeeping.
        check("tabs.switch-editor", switched != nullptr
                  && switched->text().contains(QStringLiteral("module alpha")),
              switched != nullptr ? switched->text().left(20) : QStringLiteral("<none>"));
    }
    pane->setActiveTab(2);
    pump();

    // --- AC-8.1: edit the active document -> THREE visible markers ----------
    KTextEditor::Document *document = vedit::editor::documentFor(pane->editorWidget());
    check("dirty.document", document != nullptr);
    if (document == nullptr) {
        return 1;
    }
    document->setText(document->text() + QStringLiteral("// edited by the probe\n"));
    document->setModified(true);
    pump();
    check("dirty.tab-marker", pane->tabText(2).startsWith(QStringLiteral("●")),
          pane->tabText(2));
    check("dirty.tab-marker-only-on-active-tab", !pane->tabText(0).startsWith(QStringLiteral("●"))
              && !pane->tabText(1).startsWith(QStringLiteral("●")));
    check("dirty.pane-state", pane->isActiveTabModified() && pane->isModified());
    check("dirty.title-placeholder", shell.windowTitle().contains(QStringLiteral("[*]")),
          shell.windowTitle());
    check("dirty.title-names-file", shell.windowTitle().contains(fixture.charlie),
          shell.windowTitle());
    check("dirty.window-modified", shell.isWindowModified());
    if (QWindow *window = shell.windowHandle()) {
        // The RESOLVED platform title is what the user (and a screenshot) sees.
        check("dirty.title-star-rendered", window->title().contains(QLatin1Char('*')),
              window->title());
    } else {
        check("dirty.title-star-rendered", false, QStringLiteral("no window handle"));
    }
    QLabel *dirtyLabel = shell.findChild<QLabel *>(QStringLiteral("status-dirty"));
    check("dirty.status-label", dirtyLabel != nullptr && dirtyLabel->isVisible()
              && dirtyLabel->text().contains(QStringLiteral("未保存的更改")),
          dirtyLabel != nullptr ? dirtyLabel->text() : QStringLiteral("<missing status-dirty>"));
    if (screenshots) {
        const QString path = g_shotDir + QStringLiteral("/02-dirty-three-markers.png");
        saveShot(&shell, path);
    }

    // --- AC-8.2: save -> all three markers clear together -------------------
    check("save.returns-true", shell.saveActiveDocument());
    pump();
    check("save.tab-marker-cleared", !pane->tabText(2).startsWith(QStringLiteral("●")),
          pane->tabText(2));
    check("save.window-modified-cleared", !shell.isWindowModified());
    check("save.title-star-cleared",
          shell.windowHandle() != nullptr && !shell.windowHandle()->title().contains(QLatin1Char('*')));
    check("save.status-label-hidden", !dirtyLabel->isVisible());
    check("save.disk-bytes",
          readFile(fixture.charlie).contains("edited by the probe"));
    if (screenshots) {
        const QString path = g_shotDir + QStringLiteral("/03-after-save-clean.png");
        saveShot(&shell, path);
    }

    // --- AC-9.4: every tab carries a close button; closing picks a neighbour -
    {
        auto *closeButton =
            qobject_cast<QToolButton *>(pane->tabBar()->tabButton(1, QTabBar::RightSide));
        check("close.tab-button-present", closeButton != nullptr
                  && closeButton->objectName() == QLatin1String("tab-close-button"));
        if (closeButton != nullptr) {
            closeButton->click();
            pump();
        }
        check("close.tab-closed", pane->tabCount() == 2,
              QStringLiteral("tabs=%1").arg(pane->tabCount()));
        check("close.neighbour-active", pane->activeTabIndex() == 1
                  && pane->tabText(1) == QLatin1String("charlie_unit.sv"),
              QStringLiteral("index=%1 text=%2")
                  .arg(pane->activeTabIndex())
                  .arg(pane->tabText(1)));
    }

    // --- AC-9.5: overflow scrolls instead of growing the pane ---------------
    {
        const int before = pane->tabCount();
        for (int i = 0; i < 10; ++i) {
            const QString extra = fixture.temp.path() + QStringLiteral("/extra_%1.sv").arg(i);
            writeFile(extra, "module extra;\nendmodule\n");
            shell.openPaths({extra}, nullptr);
        }
        pump();
        const int count = pane->tabCount();
        check("overflow.twelve-tabs", count == before + 10,
              QStringLiteral("tabs=%1").arg(count));
        check("overflow.scroll-buttons", pane->tabBar()->usesScrollButtons());
        // A tab strip with scroll buttons must not force the pane wider than the
        // window: the bar's minimum size stays small while its sizeHint is big.
        const QSize minimum = pane->tabBar()->minimumSizeHint();
        const QSize hint = pane->tabBar()->sizeHint();
        check("overflow.no-layout-growth", minimum.width() < 400 && hint.width() > minimum.width(),
              QStringLiteral("min=%1 hint=%2").arg(minimum.width()).arg(hint.width()));
        check("overflow.active-tab-visible",
              pane->tabBar()->tabRect(pane->activeTabIndex()).intersects(pane->tabBar()->rect()));
        // The strip must be CLIPPED (content wider than the bar) and must still
        // show the active tab: a narrow window is the case the user hits.
        shell.resize(760, 900);
        pump();
        check("overflow.clipped-at-narrow-width",
              pane->tabBar()->width() < pane->tabBar()->sizeHint().width(),
              QStringLiteral("bar=%1 hint=%2")
                  .arg(pane->tabBar()->width())
                  .arg(pane->tabBar()->sizeHint().width()));
        check("overflow.active-tab-visible-narrow",
              pane->tabBar()->tabRect(pane->activeTabIndex()).intersects(pane->tabBar()->rect()));
        if (screenshots) {
            const QString path = g_shotDir + QStringLiteral("/05-tab-overflow.png");
            saveShot(&shell, path);
        }
        shell.resize(1440, 900);
        pump();
    }

    // --- AC-8.3: closing a dirty tab asks, and the answer is what happens ---
    {
        // Make the ACTIVE tab dirty again, then walk the three answers.
        KTextEditor::Document *activeDocument =
            vedit::editor::documentFor(pane->editorWidget());
        const int index = pane->activeTabIndex();
        const QString path = pane->tabFilePath(index);
        check("prompt.document", activeDocument != nullptr);
        const QByteArray onDiskBefore = readFile(path);

        if (QApplication::platformName() == QLatin1String("offscreen")) {
            // Headless has nobody to ask: the safe answer is Cancel, and it must
            // be visible rather than a silent discard.
            activeDocument->setText(activeDocument->text() + QStringLiteral("// dirty\n"));
            activeDocument->setModified(true);
            pump();
            const int tabsBefore = pane->tabCount();
            check("prompt.headless-cancel-blocks", !pane->closeTab(index)
                      && pane->tabCount() == tabsBefore);
            check("prompt.headless-status",
                  shell.statusBar()->currentMessage().contains(QStringLiteral("unsaved"),
                                                               Qt::CaseInsensitive));
            activeDocument->setModified(false);
            pump();
            check("prompt.clean-close-allowed", pane->closeTab(index));
        } else {
            // 1) Cancel: the tab stays and the disk is untouched.
            activeDocument->setText(activeDocument->text() + QStringLiteral("// dirty\n"));
            activeDocument->setModified(true);
            pump();
            int tabsBefore = pane->tabCount();
            armDialogAnswer(QStringLiteral("Cancel"),
                            screenshots ? QStringLiteral("04-unsaved-close-prompt.png")
                                        : QString());
            const bool closed = pane->closeTab(index);
            pump();
            check("prompt.cancel-keeps-tab", !closed && pane->tabCount() == tabsBefore);
            check("prompt.cancel-disk-unchanged", readFile(path) == onDiskBefore);
            check("prompt.dialog-had-three-answers", g_dialogAnswer == QLatin1String("Cancel"));

            // 2) Discard: the tab closes, the disk is still untouched.
            tabsBefore = pane->tabCount();
            armDialogAnswer(QStringLiteral("Discard"));
            const bool discarded = pane->closeTab(index);
            pump();
            check("prompt.discard-closes", discarded && pane->tabCount() == tabsBefore - 1);
            check("prompt.discard-disk-unchanged", readFile(path) == onDiskBefore);
            if (screenshots) {
                const QString shot = g_shotDir + QStringLiteral("/06-after-discard.png");
                saveShot(&shell, shot);
            }

            // 3) Save: the tab closes AND its bytes reach the disk.
            EditorPane *activePane = area->activePane();
            KTextEditor::Document *saveDocument =
                vedit::editor::documentFor(activePane->editorWidget());
            const int saveIndex = activePane->activeTabIndex();
            const QString savePath = activePane->tabFilePath(saveIndex);
            saveDocument->setText(saveDocument->text()
                                  + QStringLiteral("// saved through the prompt\n"));
            saveDocument->setModified(true);
            pump();
            tabsBefore = activePane->tabCount();
            armDialogAnswer(QStringLiteral("Save"));
            const bool saved = activePane->closeTab(saveIndex);
            pump();
            check("prompt.save-closes", saved && activePane->tabCount() == tabsBefore - 1);
            check("prompt.save-writes-disk", readFile(savePath).contains("saved through the prompt"));
        }
    }

    // --- teardown ownership (the `--selfcheck` panel-seam crash) -------------
    // main.cpp destroys a whole ApplicationShell after seeding a session
    // (`seed.reset()` in the panel-seam block). The shell owns the provider as a
    // member but its panes are children of the central widget, so member
    // destruction runs BEFORE the widgets: a pane destructor that touches the
    // provider is a use-after-free. This drives exactly that sequence, with tabs
    // and a swapped provider in place, so the crash cannot come back silently.
    for (int round = 0; round < 3; ++round) {
        ShellOptions teardownOptions;
        teardownOptions.stateDir =
            fixture.temp.path() + QStringLiteral("/teardown-%1").arg(round);
        teardownOptions.restoreSession = false;
        teardownOptions.showDefaultPanels = false;
        auto doomed = std::make_unique<ApplicationShell>(teardownOptions);
        doomed->show();
        pump();
        doomed->setEditorProvider(vedit::editor::makeEditCoreProvider());
        pump();
        doomed->openPaths({fixture.alpha, fixture.bravo}, nullptr);
        doomed->editorArea()->splitActive(Qt::Horizontal);
        pump();
        if (EditorPane *teardownPane = doomed->editorArea()->activePane()) {
            if (KTextEditor::Document *teardownDocument =
                    vedit::editor::documentFor(teardownPane->editorWidget())) {
                teardownDocument->setText(teardownDocument->text()
                                          + QStringLiteral("// unsaved at teardown\n"));
                teardownDocument->setModified(true);
            }
        }
        pump();
        doomed.reset();
        pump();
        check(("teardown.round-" + std::to_string(round)).c_str(), true,
              QStringLiteral("shell with 2 panes and a swapped provider destroyed cleanly"));
    }

    std::printf("TABS-PROBE failures=%d platform=%s\n", failures,
                qPrintable(QApplication::platformName()));
    return failures == 0 ? 0 : 1;
}
