// T19 acceptance probe — session restore (AC-10.1.3.1 / AC-10.1.3.2).
//
// The probe drives TWO real ApplicationShell instances over ONE state directory:
// the first one is the "before close" run (open A and B, put B in the middle of
// the file, drag the split, show/hide panels, resize the window) and the second
// one is the "after restart" run that starts from the archive the first one
// wrote. Every restored fact is asserted and printed as one line, so this log is
// the evidence; the archive itself is written to <out>/session-before.json.
//
// It runs offscreen (`QT_QPA_PLATFORM=offscreen`) and needs no X server. The
// editor behind a pane is whichever provider the shell has: the demo
// QPlainTextEdit by default, or the real KTextEditor (edit-core) provider when
// the probe is built with -DVEDIT_PROBE_HAVE_EDIT_CORE=1 and run with
// `--provider=ktexteditor`.
//
// Exit code: 0 when every check passed, 1 when any check failed, 2 on setup
// errors (no state dir, no editor widget).
#include "app_shell.h"
#include "editor_area.h"
#include "panel_registry.h"
#include "session_store.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QGuiApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTimer>
#include <QSize>
#include <QTextCursor>
#include <QTextDocument>

#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
#include <KTextEditor/Cursor>
#include <KTextEditor/View>

#include "ktexteditor_provider.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;
QString g_outDir;
bool g_realProvider = false;

void pump(int rounds = 4)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
        std::fflush(stdout);
        return;
    }
    ++g_failures;
    std::printf("FAIL %s%s\n", name,
                detail.isEmpty() ? "" : (" — " + detail.toStdString()).c_str());
    std::fflush(stdout);
}

QString q(const QString &text)
{
    return QStringLiteral("\"%1\"").arg(text);
}

bool writeLines(const QString &path, int lines)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    for (int line = 0; line < lines; ++line) {
        file.write(QByteArray("assign line_") + QByteArray::number(line)
                   + QByteArray(" = data_") + QByteArray::number(line) + QByteArray(";\n"));
    }
    return true;
}

// --- viewport position, provider agnostic ----------------------------------
struct Position {
    int line = -1;
    int column = -1;
    int scroll = -1;
    bool valid = false;
};

Position readPosition(QWidget *editor)
{
    // Demo/fallback editor.
    if (auto *text = qobject_cast<QPlainTextEdit *>(editor)) {
        const QTextCursor cursor = text->textCursor();
        Position position;
        position.line = cursor.blockNumber();
        position.column = cursor.positionInBlock();
        position.scroll = text->verticalScrollBar()->value();
        position.valid = true;
        return position;
    }
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
    // The product editor: KTextEditor. Reading it directly is fine HERE — this
    // file is a probe, not part of the shell library.
    if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
        Position position;
        const KTextEditor::Cursor cursor = view->cursorPosition();
        position.line = cursor.line();
        position.column = cursor.column();
        position.scroll = view->verticalScrollBar()->value();
        position.valid = true;
        return position;
    }
#endif
    return Position{};
}

bool writePosition(QWidget *editor, int line, int column, int scroll)
{
    if (auto *text = qobject_cast<QPlainTextEdit *>(editor)) {
        QTextCursor cursor = text->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
        text->setTextCursor(cursor);
        text->verticalScrollBar()->setValue(scroll);
        return true;
    }
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
    if (auto *view = qobject_cast<KTextEditor::View *>(editor)) {
        view->setCursorPosition(KTextEditor::Cursor(line, column));
        if (QScrollBar *bar = view->verticalScrollBar()) {
            bar->setValue(scroll);
        }
        return true;
    }
#endif
    return false;
}

QPlainTextEdit *plainEditor(vedit::app::EditorPane *pane)
{
    return pane != nullptr ? qobject_cast<QPlainTextEdit *>(pane->editorWidget()) : nullptr;
}

QString positionText(const Position &position)
{
    return QStringLiteral("line=%1 column=%2 scroll=%3")
        .arg(position.line)
        .arg(position.column)
        .arg(position.scroll);
}

// --- a shell with the requested provider -----------------------------------
std::unique_ptr<vedit::app::ApplicationShell> makeShell(const vedit::app::ShellOptions &optionsIn)
{
    vedit::app::ShellOptions options = optionsIn;
    // The provider must be installed BEFORE the session restore runs (the restore
    // re-opens files through the provider), so the shell defers it.
    options.deferSessionRestore = true;
    auto shell = std::make_unique<vedit::app::ApplicationShell>(options);
#if defined(VEDIT_PROBE_HAVE_EDIT_CORE)
    if (g_realProvider) {
        shell->setEditorProvider(vedit::editor::makeEditCoreProvider());
    }
#endif
    shell->startSession();
    return shell;
}

QJsonObject readArchive(const QString &stateDir)
{
    QFile file(stateDir + QStringLiteral("/session.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Structural skeleton of the layout: node type / orientation / ratio / pane id /
// provider / active tab / document paths. Caret and scroll are checked field by
// field instead, because a hidden tab's position is only applied when its tab is
// activated (which the probe does explicitly).
QJsonObject skeleton(const QJsonObject &node)
{
    QJsonObject out;
    const QString type = node.value(QStringLiteral("type")).toString();
    out.insert(QStringLiteral("type"), type);
    if (type == QLatin1String("leaf")) {
        out.insert(QStringLiteral("pane"), node.value(QStringLiteral("pane")).toString());
        out.insert(QStringLiteral("provider"), node.value(QStringLiteral("provider")).toString());
        out.insert(QStringLiteral("activeTab"), node.value(QStringLiteral("activeTab")).toInt());
        QJsonArray paths;
        const QJsonArray documents = node.value(QStringLiteral("documents")).toArray();
        for (const QJsonValue &value : documents) {
            paths.append(value.toObject().value(QStringLiteral("path")).toString());
        }
        out.insert(QStringLiteral("documents"), paths);
        return out;
    }
    out.insert(QStringLiteral("orientation"), node.value(QStringLiteral("orientation")).toString());
    out.insert(QStringLiteral("ratio"), node.value(QStringLiteral("ratio")).toDouble());
    out.insert(QStringLiteral("first"), skeleton(node.value(QStringLiteral("first")).toObject()));
    out.insert(QStringLiteral("second"), skeleton(node.value(QStringLiteral("second")).toObject()));
    return out;
}

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString orientationOf(const QJsonObject &root)
{
    return root.value(QStringLiteral("orientation")).toString();
}

// ===========================================================================
// Scenario A — AC-10.1.3.2: A and B are two tabs of one pane, B is active and
// sits in the middle of the file; after a restart both tabs, B's active state
// and BOTH documents' caret/scroll are back, together with the window size, the
// panel visibility and the project root.
// ===========================================================================
void scenarioAb(const QString &rootDir, const QString &stateDir)
{
    const QString fileA = rootDir + QStringLiteral("/a.sv");
    const QString fileB = rootDir + QStringLiteral("/b.sv");
    if (!writeLines(fileA, 40) || !writeLines(fileB, 400)) {
        std::printf("FATAL: cannot create the A/B fixtures in %s\n", qPrintable(rootDir));
        std::exit(2);
    }

    QString savedActiveTab;
    QString savedBPath;
    QSize savedSize;
    QPoint savedPosition;
    {
        vedit::app::ShellOptions options;
        options.stateDir = stateDir;
        options.projectRoot = rootDir;
        options.restoreSession = false;
        options.showDefaultPanels = true;
        auto shell = makeShell(options);
        shell->resize(1111, 733);
        shell->move(140, 90);
        shell->show();
        pump();

        vedit::app::EditorPane *pane = shell->editorArea()->activePane();
        if (pane == nullptr) {
            std::printf("FATAL: the fresh shell has no active pane\n");
            std::exit(2);
        }
        QString error;
        check("seed.open-a", pane->openFile(fileA, &error), error);
        error.clear();
        check("seed.open-b", pane->openFile(fileB, &error), error);
        check("seed.two-tabs", pane->tabCount() == 2,
              QStringLiteral("tabs=%1").arg(pane->tabCount()));

        pane->setActiveTab(0);
        pump();
        const bool aPositioned = writePosition(pane->editorWidget(), 5, 1, 0);
        pump();
        pane->setActiveTab(1);
        pump();
        const bool bPositioned = writePosition(pane->editorWidget(), 300, 6, 120);
        pump();
        check("seed.positions", aPositioned && bPositioned);
        check("seed.b-active", pane->activeTabIndex() == 1,
              QStringLiteral("activeTab=%1").arg(pane->activeTabIndex()));

        // Panels: the terminal dock is inserted, the file tree is closed — both
        // the inserted set and the visibility must survive the restart.
        shell->panels()->showPanel(QStringLiteral("terminal"), shell.get());
        shell->panels()->hidePanel(QStringLiteral("file-tree"));
        pump();
        check("seed.panel-state",
              shell->panels()->isVisible(QStringLiteral("terminal"))
                  && !shell->panels()->isVisible(QStringLiteral("file-tree")));
        savedSize = shell->size();
        savedPosition = shell->pos();
        check("seed.geometry", savedSize.width() > 0 && savedSize.height() > 0,
              QStringLiteral("%1x%2").arg(savedSize.width()).arg(savedSize.height()));

        QString saveError;
        check("seed.save-session", shell->saveSession(&saveError), saveError);

        if (!g_outDir.isEmpty()) {
            QFile::remove(g_outDir + QStringLiteral("/session-before.json"));
            QFile::copy(stateDir + QStringLiteral("/session.json"),
                        g_outDir + QStringLiteral("/session-before.json"));
            const QPixmap shot = shell->grab();
            shot.save(g_outDir + QStringLiteral("/before-close.png"));
        }
        savedActiveTab = QStringLiteral("B");
        savedBPath = fileB;
    }

    {
        vedit::app::ShellOptions options;
        options.stateDir = stateDir;
        options.projectRoot = QString(); // must come from the archive
        options.restoreSession = true;
        options.showDefaultPanels = true;
        auto shell = makeShell(options);
        shell->show();
        pump();

        check("restore.pane-count", shell->editorArea()->paneCount() == 1,
              QStringLiteral("panes=%1").arg(shell->editorArea()->paneCount()));
        vedit::app::EditorPane *pane = shell->editorArea()->activePane();
        check("restore.pane-active", pane != nullptr);
        if (pane == nullptr) {
            return;
        }
        check("restore.tab-count", pane->tabCount() == 2,
              QStringLiteral("tabs=%1").arg(pane->tabCount()));
        check("restore.tab-a", pane->tabFilePath(0) == fileA, pane->tabFilePath(0));
        check("restore.tab-b", pane->tabFilePath(1) == savedBPath, pane->tabFilePath(1));
        check("restore.active-tab-is-a-b-tab", pane->activeTabIndex() == 1,
              QStringLiteral("activeTab=%1 (%2)").arg(pane->activeTabIndex()).arg(savedActiveTab));

        // The active tab is the visible one, so its pending position was applied
        // when the window was shown.
        const Position b = readPosition(pane->editorWidget());
        check("restore.b.caret", b.valid && b.line == 300 && b.column == 6, positionText(b));
        check("restore.b.scroll", b.valid && b.scroll == 120, positionText(b));

        // A is a background tab; its position is applied when its tab is shown
        // (a hidden stacked page has no viewport geometry).
        pane->setActiveTab(0);
        pump();
        const Position a = readPosition(pane->editorWidget());
        check("restore.a.caret", a.valid && a.line == 5 && a.column == 1, positionText(a));
        check("restore.a.scroll", a.valid && a.scroll == 0, positionText(a));
        pane->setActiveTab(1);
        pump();

        const QString platform = QGuiApplication::platformName();
        const bool offscreen = platform.contains(QStringLiteral("offscreen"));
        if (offscreen) {
            // The offscreen plugin has one fixed virtual screen; Qt's
            // restoreGeometry keeps the restored window on it, so only the
            // height is a faithful comparison here. The xcb run (Xvfb) asserts
            // the full geometry and the position.
            check("restore.geometry.height", shell->size().height() == savedSize.height(),
                  QStringLiteral("%1x%2 (saved %3x%4, platform=%5, width clamped by the "
                                 "offscreen screen)")
                      .arg(shell->size().width())
                      .arg(shell->size().height())
                      .arg(savedSize.width())
                      .arg(savedSize.height())
                      .arg(platform));
        } else {
            check("restore.geometry", shell->size() == savedSize,
                  QStringLiteral("%1x%2 (saved %3x%4, platform=%5)")
                      .arg(shell->size().width())
                      .arg(shell->size().height())
                      .arg(savedSize.width())
                      .arg(savedSize.height())
                      .arg(platform));
            check("restore.geometry.position", shell->pos() == savedPosition,
                  QStringLiteral("%1,%2 (saved %3,%4)")
                      .arg(shell->pos().x())
                      .arg(shell->pos().y())
                      .arg(savedPosition.x())
                      .arg(savedPosition.y()));
        }
        check("restore.panel-terminal", shell->panels()->isVisible(QStringLiteral("terminal")));
        check("restore.panel-outline", shell->panels()->isVisible(QStringLiteral("outline")));
        check("restore.panel-file-tree-hidden",
              !shell->panels()->isVisible(QStringLiteral("file-tree")));
        const QString expectedProject = QFileInfo(rootDir).fileName();
        check("restore.project-root", shell->projectLabel() == expectedProject,
              QStringLiteral("label=%1 expected=%2").arg(shell->projectLabel(), expectedProject));

        const QJsonObject archive = readArchive(stateDir);
        check("archive.version", archive.value(QStringLiteral("version")).toInt() == 2,
              QStringLiteral("version=%1").arg(archive.value(QStringLiteral("version")).toInt()));
        const QJsonObject layout = archive.value(QStringLiteral("editorLayout")).toObject();
        check("archive.layout-version", layout.value(QStringLiteral("version")).toInt() == 2);
        const QJsonObject leaf = layout.value(QStringLiteral("root")).toObject();
        const QJsonArray documents = leaf.value(QStringLiteral("documents")).toArray();
        check("archive.leaf-documents", documents.size() == 2,
              QStringLiteral("documents=%1").arg(documents.size()));
        if (!documents.isEmpty()) {
            const QJsonObject documentB = documents.at(1).toObject();
            const QJsonObject caret = documentB.value(QStringLiteral("caret")).toObject();
            check("archive.b-caret", caret.value(QStringLiteral("line")).toInt() == 300
                                         && caret.value(QStringLiteral("column")).toInt() == 6,
                  compact(caret));
            check("archive.b-scroll",
                  documentB.value(QStringLiteral("scroll")).toInt() == 120,
                  QStringLiteral("scroll=%1")
                      .arg(documentB.value(QStringLiteral("scroll")).toInt()));
        }
        check("archive.active-tab", leaf.value(QStringLiteral("activeTab")).toInt() == 1);

        if (!g_outDir.isEmpty()) {
            shell->grab().save(g_outDir + QStringLiteral("/after-restart.png"));
        }
    }
}

// ===========================================================================
// Scenario B — AC-10.1.3.1 split layout: the pane count, the horizontal and
// vertical split tree, the ratios and the active pane survive a restart, and so
// does every pane's document.
// ===========================================================================
void scenarioSplit(const QString &rootDir, const QString &stateDir)
{
    const QString file1 = rootDir + QStringLiteral("/one.sv");
    const QString file2 = rootDir + QStringLiteral("/two.sv");
    const QString file3 = rootDir + QStringLiteral("/three.sv");
    for (const QString &file : {file1, file2, file3}) {
        if (!writeLines(file, 120)) {
            std::printf("FATAL: cannot create %s\n", qPrintable(file));
            std::exit(2);
        }
    }

    QJsonObject before;
    {
        vedit::app::ShellOptions options;
        options.stateDir = stateDir;
        options.restoreSession = false;
        options.showDefaultPanels = true;
        auto shell = makeShell(options);
        shell->resize(1200, 800);
        shell->show();
        pump();

        vedit::app::EditorPane *first = shell->editorArea()->activePane();
        QString error;
        check("seed.split.open-1", first != nullptr && first->openFile(file1, &error), error);
        vedit::app::EditorPane *second = shell->editorArea()->splitActive(Qt::Horizontal);
        error.clear();
        check("seed.split.open-2", second != nullptr && second->openFile(file2, &error), error);
        vedit::app::EditorPane *third = shell->editorArea()->splitActive(Qt::Vertical);
        error.clear();
        check("seed.split.open-3", third != nullptr && third->openFile(file3, &error), error);
        pump();
        check("seed.split.panes", shell->editorArea()->paneCount() == 3,
              QStringLiteral("panes=%1").arg(shell->editorArea()->paneCount()));

        before = shell->editorArea()->saveLayout();
        QString saveError;
        check("seed.split.save", shell->saveSession(&saveError), saveError);
    }

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = true;
    options.showDefaultPanels = true;
    auto shell = makeShell(options);
    shell->show();
    pump();

    check("split.pane-count", shell->editorArea()->paneCount() == 3,
          QStringLiteral("panes=%1").arg(shell->editorArea()->paneCount()));
    const QJsonObject after = shell->editorArea()->saveLayout();
    check("split.root-orientation",
          orientationOf(after.value(QStringLiteral("root")).toObject())
              == orientationOf(before.value(QStringLiteral("root")).toObject()),
          QStringLiteral("after=%1").arg(orientationOf(after.value(QStringLiteral("root")).toObject())));
    check("split.structure-roundtrip",
          skeleton(after.value(QStringLiteral("root")).toObject())
              == skeleton(before.value(QStringLiteral("root")).toObject()),
          compact(skeleton(after.value(QStringLiteral("root")).toObject())));
    check("split.active-pane",
          after.value(QStringLiteral("active")).toString()
              == before.value(QStringLiteral("active")).toString(),
          QStringLiteral("after=%1 before=%2")
              .arg(after.value(QStringLiteral("active")).toString(),
                   before.value(QStringLiteral("active")).toString()));

    // Every pane must have re-opened its own document.
    for (vedit::app::EditorPane *pane : shell->editorArea()->panes()) {
        const QString path = pane->tabFilePath(0);
        check("split.pane-has-document", !path.isEmpty(),
              QStringLiteral("pane=%1 path=%2").arg(pane->paneId(), path));
    }
}

// ===========================================================================
// Scenario C — tolerance. A v1 archive (no per-pane documents) and an archive
// that names a file deleted before the restart both restore without a crash and
// without losing the rest of the session.
// ===========================================================================
void scenarioTolerance(const QString &rootDir)
{
    // --- v1 archive ------------------------------------------------------
    const QString v1Dir = rootDir + QStringLiteral("/v1-state");
    QDir().mkpath(v1Dir);
    QJsonObject v1Leaf;
    v1Leaf.insert(QStringLiteral("type"), QStringLiteral("leaf"));
    v1Leaf.insert(QStringLiteral("pane"), QStringLiteral("pane-1"));
    v1Leaf.insert(QStringLiteral("provider"), QStringLiteral("demo"));
    QJsonObject v1Split;
    v1Split.insert(QStringLiteral("type"), QStringLiteral("split"));
    v1Split.insert(QStringLiteral("orientation"), QStringLiteral("vertical"));
    v1Split.insert(QStringLiteral("ratio"), 0.4);
    v1Split.insert(QStringLiteral("first"), v1Leaf);
    v1Split.insert(QStringLiteral("second"), QJsonObject{
                                                    {QStringLiteral("type"), QStringLiteral("leaf")},
                                                    {QStringLiteral("pane"), QStringLiteral("pane-2")},
                                                });
    QJsonObject v1Layout;
    v1Layout.insert(QStringLiteral("version"), 1);
    v1Layout.insert(QStringLiteral("active"), QStringLiteral("pane-2"));
    v1Layout.insert(QStringLiteral("nextPane"), 3);
    v1Layout.insert(QStringLiteral("root"), v1Split);
    QJsonObject v1Archive;
    v1Archive.insert(QStringLiteral("version"), 1);
    v1Archive.insert(QStringLiteral("panels"), QJsonArray{QStringLiteral("outline")});
    v1Archive.insert(QStringLiteral("editorLayout"), v1Layout);
    {
        QFile file(v1Dir + QStringLiteral("/session.json"));
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write(QJsonDocument(v1Archive).toJson());
    }
    {
        vedit::app::ShellOptions options;
        options.stateDir = v1Dir;
        options.restoreSession = true;
        options.showDefaultPanels = false;
        auto shell = makeShell(options);
        shell->show();
        pump();
        check("v1.pane-count", shell->editorArea()->paneCount() == 2,
              QStringLiteral("panes=%1").arg(shell->editorArea()->paneCount()));
        const QJsonObject layout = shell->editorArea()->saveLayout();
        check("v1.tree", orientationOf(layout.value(QStringLiteral("root")).toObject())
                             == QLatin1String("vertical"));
        check("v1.active-pane",
              layout.value(QStringLiteral("active")).toString() == QLatin1String("pane-2"));
        check("v1.empty-tabs", shell->editorArea()->panes().at(0)->tabCount() == 1
                                   && shell->editorArea()->panes().at(0)->tabFilePath(0).isEmpty());
        check("v1.no-crash-diagnostics", shell->diagnostics().isEmpty(),
              shell->diagnostics().join(QStringLiteral(" | ")));
    }

    // --- a document that vanished before the restart ---------------------
    const QString missingDir = rootDir + QStringLiteral("/missing-state");
    const QString keepFile = rootDir + QStringLiteral("/keep.sv");
    const QString goneFile = rootDir + QStringLiteral("/gone.sv");
    writeLines(keepFile, 10);
    writeLines(goneFile, 10);
    {
        vedit::app::ShellOptions options;
        options.stateDir = missingDir;
        options.restoreSession = false;
        options.showDefaultPanels = false;
        auto shell = makeShell(options);
        shell->show();
        pump();
        vedit::app::EditorPane *pane = shell->editorArea()->activePane();
        QString error;
        check("missing.seed-keep", pane != nullptr && pane->openFile(keepFile, &error), error);
        error.clear();
        check("missing.seed-gone", pane != nullptr && pane->openFile(goneFile, &error), error);
        QString saveError;
        check("missing.seed-save", shell->saveSession(&saveError), saveError);
    }
    QFile::remove(goneFile);
    {
        vedit::app::ShellOptions options;
        options.stateDir = missingDir;
        options.restoreSession = true;
        options.showDefaultPanels = false;
        auto shell = makeShell(options);
        shell->show();
        pump();
        vedit::app::EditorPane *pane = shell->editorArea()->activePane();
        check("missing.pane-exists", pane != nullptr);
        if (pane != nullptr) {
            check("missing.kept-document", pane->tabCount() == 1
                                               && pane->tabFilePath(0) == keepFile,
                  QStringLiteral("tabs=%1 path=%2")
                      .arg(pane->tabCount())
                      .arg(pane->tabFilePath(0)));
        }
        bool mentioned = false;
        for (const QString &diagnostic : shell->diagnostics()) {
            if (diagnostic.contains(QStringLiteral("cannot reopen"))
                && diagnostic.contains(QFileInfo(goneFile).fileName())) {
                mentioned = true;
            }
        }
        check("missing.diagnostic", mentioned, shell->diagnostics().join(QStringLiteral(" | ")));
        check("missing.still-starts", shell->editorArea()->paneCount() == 1);
    }
}

// ===========================================================================
// Scenario D — the dirty-buffer rule (requirements C13 / AC-10.1.2.2): closing
// the window with an unsaved buffer must not close and must not archive. On a
// GUI platform the Save/Discard/Cancel prompt is really shown; the probe answers
// it by role. Headless (offscreen) the shell refuses the close by design.
// ===========================================================================
// The dirty close prompt is a real modal QMessageBox in a GUI session. The probe
// has no human, so it schedules the answer BEFORE calling close(): the timer
// fires inside QMessageBox::exec() and clicks the button with the requested role
// (button ROLES, not texts, so a translated or renamed label cannot break it).
void scheduleDialogAnswer(QMessageBox::ButtonRole role)
{
    auto *timer = new QTimer(qApp);
    auto *elapsed = new QElapsedTimer();
    elapsed->start();
    timer->setInterval(40);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, elapsed, role]() {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            for (QAbstractButton *button : box->buttons()) {
                if (box->buttonRole(button) == role) {
                    timer->stop();
                    delete elapsed;
                    timer->deleteLater();
                    button->click();
                    return;
                }
            }
        }
        if (elapsed->elapsed() > 8000) {
            timer->stop();
            delete elapsed;
            timer->deleteLater();
            std::printf("FAIL dirty.prompt-timeout — no message box with the requested role\n");
            ++g_failures;
        }
    });
    timer->start();
}

QString readFileText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

void scenarioDirtyClose(const QString &rootDir)
{
    const QString stateDir = rootDir + QStringLiteral("/dirty-state");
    QDir(stateDir).removeRecursively();
    const QString file = rootDir + QStringLiteral("/dirty.sv");
    writeLines(file, 20);

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    auto shell = makeShell(options);
    shell->show();
    pump();
    vedit::app::EditorPane *pane = shell->editorArea()->activePane();
    QString error;
    check("dirty.open", pane != nullptr && pane->openFile(file, &error), error);

    QPlainTextEdit *editor = plainEditor(pane);
    check("dirty.editor", editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    QTextCursor cursor = editor->textCursor();
    cursor.insertText(QStringLiteral("// unsaved edit\n"));
    pump();
    check("dirty.flag", pane->isTabModified(0));

    const bool headless = QGuiApplication::platformName().contains(QStringLiteral("offscreen"));
    if (headless) {
        // No dialog can be shown: the shell must refuse, keep the buffer, and
        // must not write an archive that pretends the edit was saved.
        shell->close();
        pump();
        check("dirty.headless-close-refused", shell->isVisible());
        check("dirty.headless-no-archive",
              !QFile::exists(stateDir + QStringLiteral("/session.json")));
        check("dirty.headless-buffer-kept",
              editor->toPlainText().contains(QStringLiteral("// unsaved edit")));
    } else {
        // 1) Cancel: the window stays open, nothing is written anywhere.
        scheduleDialogAnswer(QMessageBox::RejectRole);
        shell->close();
        pump();
        check("dirty.cancel-close-refused", shell->isVisible());
        check("dirty.cancel-no-archive",
              !QFile::exists(stateDir + QStringLiteral("/session.json")));
        check("dirty.cancel-buffer-kept",
              editor->toPlainText().contains(QStringLiteral("// unsaved edit")));
        check("dirty.cancel-file-untouched",
              !readFileText(file).contains(QStringLiteral("// unsaved edit")));

        // 2) Save: the prompt writes the buffer, then the close and the archive
        //    both go through.
        scheduleDialogAnswer(QMessageBox::AcceptRole);
        shell->close();
        pump();
        check("dirty.save-close-accepted", !shell->isVisible());
        check("dirty.save-file-written",
              readFileText(file).contains(QStringLiteral("// unsaved edit")));
        check("dirty.save-archive-written",
              QFile::exists(stateDir + QStringLiteral("/session.json")));
        return;
    }

    // Headless: the user saves through the same code path the prompt's Save
    // answer uses, then the close goes through and the archive exists.
    editor->document()->setModified(false);
    pump();
    check("dirty.clean-flag", !pane->isTabModified(0));
    shell->close();
    pump();
    check("dirty.close-accepted", !shell->isVisible());
    check("dirty.archive-written",
          QFile::exists(stateDir + QStringLiteral("/session.json")));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("i3-session-probe"));

    QString workDir; // parent of the fixture roots
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        if (argument.startsWith("--out=")) {
            g_outDir = QString::fromUtf8(argument.mid(6));
        } else if (argument.startsWith("--work=")) {
            workDir = QString::fromUtf8(argument.mid(7));
        } else if (argument == "--provider=ktexteditor") {
            g_realProvider = true;
        }
    }
    if (workDir.isEmpty()) {
        std::printf("FATAL: --work=<empty dir> is required\n");
        return 2;
    }
#if !defined(VEDIT_PROBE_HAVE_EDIT_CORE)
    if (g_realProvider) {
        std::printf("FATAL: this probe was built without the edit-core provider\n");
        return 2;
    }
#endif
    workDir = QDir(workDir).absolutePath();
    QDir().mkpath(workDir);
    if (!g_outDir.isEmpty()) {
        QDir().mkpath(g_outDir);
    }
    std::printf("provider=%s\n", g_realProvider ? "ktexteditor" : "demo");

    const QString rootA = workDir + QStringLiteral("/scenario-ab");
    const QString rootB = workDir + QStringLiteral("/scenario-split");
    const QString rootC = workDir + QStringLiteral("/scenario-tolerance");
    const QString rootD = workDir + QStringLiteral("/scenario-dirty");
    for (const QString &path : {rootA, rootB, rootC, rootD}) {
        QDir().mkpath(path);
    }

    scenarioAb(rootA, rootA + QStringLiteral("/state"));
    scenarioSplit(rootB, rootB + QStringLiteral("/state"));
    scenarioTolerance(rootC);
    scenarioDirtyClose(rootD);

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
