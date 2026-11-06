// RM-5a regression probe: why does the native fold count become 0 after the
// load path moved to KParts::ReadOnlyPart::openUrl()?
//
// Compares three load paths on the same fixture and prints the fold command's
// own result/message plus the document's highlighting mode at each step.
//
//   A  current product load  : loadFileIntoDocument() -> openUrl()
//   B  A + a forced mode transition (clear -> set again)
//   C  the old load          : Document::setText() + applyHighlightingMode()
#include "document_io.h"
#include "highlight_mode.h"
#include "ktexteditor_provider.h"

#include <KTextEditor/Command>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QAction>
#include <QTextStream>

namespace {

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

QString sample(int modules)
{
    QString text;
    for (int index = 0; index < modules; ++index) {
        const QString n = QString::number(index);
        text += QStringLiteral("module m%1 #(parameter W = 8) (\n").arg(n);
        text += QStringLiteral("  input  logic clk,\n");
        text += QStringLiteral("  input  logic rst_n,\n");
        text += QStringLiteral("  output logic [W-1:0] q\n");
        text += QStringLiteral(");\n");
        text += QStringLiteral("  logic [W-1:0] state, nxt;\n");
        text += QStringLiteral("  always_ff @(posedge clk) begin\n");
        text += QStringLiteral("    state <= nxt;\n");
        text += QStringLiteral("  end\n");
        for (int line = 8; line < 39; ++line) {
            text += QStringLiteral("  // filler %1 %2\n").arg(line).arg(n);
        }
        text += QStringLiteral("endmodule\n");
    }
    return text;
}

int hidden(KTextEditor::View *view)
{
    if (view == nullptr) {
        return -999;
    }
    const int firstReal = view->firstDisplayedLine(KTextEditor::View::RealLine);
    const int lastReal = view->lastDisplayedLine(KTextEditor::View::RealLine);
    const int firstVisible = view->firstDisplayedLine(KTextEditor::View::VisibleLine);
    const int lastVisible = view->lastDisplayedLine(KTextEditor::View::VisibleLine);
    return (lastReal - firstReal) - (lastVisible - firstVisible);
}

void writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
}

void reportFold(const QString &tag, KTextEditor::View *view)
{
    QTextStream out(stdout);
    if (view == nullptr || view->document() == nullptr) {
        out << "FOLD tag=" << tag << " view=null\n";
        return;
    }
    KTextEditor::Document *document = view->document();
    const auto snapshot = [view]() {
        QString text = QStringLiteral("firstReal=%1 lastReal=%2 firstVis=%3 lastVis=%4 y40=%5")
                           .arg(view->firstDisplayedLine(KTextEditor::View::RealLine))
                           .arg(view->lastDisplayedLine(KTextEditor::View::RealLine))
                           .arg(view->firstDisplayedLine(KTextEditor::View::VisibleLine))
                           .arg(view->lastDisplayedLine(KTextEditor::View::VisibleLine))
                           .arg(view->cursorToCoordinate(KTextEditor::Cursor(40, 0)).y());
        return text;
    };
    out << "FOLD tag=" << tag << " lines=" << document->lines()
        << " mode='" << document->highlightingMode() << "'"
        << " modified=" << (document->isModified() ? 1 : 0)
        << " url_set=" << (document->url().isValid() ? 1 : 0)
        << " hidden_before=" << hidden(view) << " " << snapshot() << "\n";
    out.flush();
    KTextEditor::Command *fold =
        KTextEditor::Editor::instance()->queryCommand(QStringLiteral("fold"));
    QString message;
    const bool foldRan = fold != nullptr
        && fold->exec(view, QStringLiteral("fold"), message,
                      KTextEditor::Range(0, 0, 39, 0));
    pump();
    out << "FOLD tag=" << tag << " fold_command=" << (fold != nullptr ? 1 : 0)
        << " exec_ok=" << (foldRan ? 1 : 0)
        << " message='" << message << "'"
        << " hidden_after=" << hidden(view) << " " << snapshot()
        << " mode_after='" << document->highlightingMode() << "'\n";
    out.flush();
}

// The editor's own undo action (KStandardAction) lives on KateViewInternal, not
// on the public View — the same walk src/app/editor_area.cpp does.
QAction *findEditUndo(QWidget *editor)
{
    if (editor == nullptr) {
        return nullptr;
    }
    const auto matches = [](QAction *action) {
        return action != nullptr
            && (action->objectName() == QLatin1String("edit_undo")
                || action->objectName() == QLatin1String("undo"));
    };
    for (QAction *action : editor->actions()) {
        if (matches(action)) {
            return action;
        }
    }
    for (QWidget *widget : editor->findChildren<QWidget *>()) {
        for (QAction *action : widget->actions()) {
            if (matches(action)) {
                return action;
            }
        }
    }
    for (QAction *action : editor->findChildren<QAction *>()) {
        if (matches(action)) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("FOLD no-temp-dir\n");
        return 1;
    }
    const QString bigPath = temp.path() + QStringLiteral("/big-10k.sv");
    writeFile(bigPath, sample(256));
    const QString text = sample(256);

    // --- A: the current product load (openUrl) -----------------------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        const bool loaded = vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        std::printf("FOLD A loaded=%d error='%s'\n", loaded ? 1 : 0, qPrintable(error));
        reportFold(QStringLiteral("A-openUrl"), view);
    }

    // --- B: openUrl + a forced mode transition -----------------------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        const QString mode = document->highlightingMode();
        document->setHighlightingMode(QString());
        pump();
        document->setHighlightingMode(mode);
        pump();
        std::printf("FOLD B forced_mode '%s' -> reset -> '%s'\n", qPrintable(mode),
                    qPrintable(document->highlightingMode()));
        reportFold(QStringLiteral("B-forced"), view);
    }

    // --- D: mode applied BEFORE the openUrl load --------------------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString diagnostic;
        const bool modeFirst = vedit::editor::applyHighlightingMode(document, bigPath, &diagnostic);
        QString error;
        const bool loaded = vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        pump();
        std::printf("FOLD D mode_first=%d loaded=%d mode='%s' text_matches=%d\n",
                    modeFirst ? 1 : 0, loaded ? 1 : 0,
                    qPrintable(document->highlightingMode()),
                    document->text() == text ? 1 : 0);
        reportFold(QStringLiteral("D-mode-first"), view);
    }

    // --- E: another mode then back (forces a real transition) -------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        const QString mode = document->highlightingMode();
        document->setHighlightingMode(QStringLiteral("C++"));
        pump();
        std::printf("FOLD E after_other_mode='%s'\n", qPrintable(document->highlightingMode()));
        document->setHighlightingMode(mode);
        pump();
        reportFold(QStringLiteral("E-transition"), view);
    }

    // --- F: after openUrl, setText(same text) -----------------------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        document->setText(text);
        document->setModified(false);
        pump();
        reportFold(QStringLiteral("F-openUrl-then-setText"), view);
    }

    // --- G: openUrl then setText(same text): fold restored, and does that
    //        add an undo step? (the RM-5a floor must stay clean) -------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        QAction *undoAction = findEditUndo(view);
        std::printf("FOLD G undo_action=%d enabled_after_openurl=%d\n",
                    undoAction != nullptr ? 1 : 0,
                    undoAction != nullptr && undoAction->isEnabled() ? 1 : 0);
        const QString before = document->text();
        document->setText(document->text());
        document->setModified(false);
        pump();
        std::printf("FOLD G enabled_after_setText_same=%d text_unchanged=%d\n",
                    undoAction != nullptr && undoAction->isEnabled() ? 1 : 0,
                    document->text() == before ? 1 : 0);
        reportFold(QStringLiteral("G-setText-same"), view);
        if (undoAction != nullptr && undoAction->isEnabled()) {
            QMetaObject::invokeMethod(document, "undo");
            pump();
            std::printf("FOLD G after_undo text_unchanged=%d enabled=%d\n",
                        document->text() == before ? 1 : 0,
                        undoAction->isEnabled() ? 1 : 0);
            reportFold(QStringLiteral("G-after-undo"), view);
        }
    }

    // --- H: openUrl, then LAY OUT the view (what the product does: the view is
    //        shown in a pane). Is the fold ability a paint/layout effect? ----
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        view->setConfigValue(QStringLiteral("folding-bar"), true);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        view->resize(900, 700);
        view->show();
        pump();
        std::printf("FOLD H shown=%d size=%dx%d\n", view->isVisible() ? 1 : 0,
                    view->width(), view->height());
        reportFold(QStringLiteral("H-shown"), view);
    }

    // --- I: openUrl + resize only (no show) ------------------------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        QString error;
        vedit::editor::loadFileIntoDocument(document, bigPath, &error);
        view->resize(900, 700);
        pump();
        std::printf("FOLD I visible=%d size=%dx%d\n", view->isVisible() ? 1 : 0,
                    view->width(), view->height());
        reportFold(QStringLiteral("I-resize-only"), view);
    }

    // --- C: the old load path (setText + applyHighlightingMode) ------------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        if (argc > 1 && QString::fromLatin1(argv[1]) == QLatin1String("shown")) {
            view->resize(900, 700);
            view->show();
            pump();
        }
        QString error;
        const bool set = document->setText(text);
        QString diagnostic;
        const bool modeSet =
            vedit::editor::applyHighlightingMode(document, bigPath, &diagnostic);
        document->setModified(false);
        pump();
        std::printf("FOLD C setText=%d modeSet=%d text_matches=%d diag='%s'\n", set ? 1 : 0,
                    modeSet ? 1 : 0, document->text() == text ? 1 : 0, qPrintable(diagnostic));
        reportFold(QStringLiteral("C-setText"), view);
    }
    // --- C2: the old path with a SHOWN view (symmetry control) -----------
    {
        KTextEditor::Editor *editor = KTextEditor::Editor::instance();
        KTextEditor::Document *document = editor->createDocument(nullptr);
        KTextEditor::View *view = document->createView(nullptr);
        view->resize(900, 700);
        view->show();
        pump();
        document->setText(text);
        QString diagnostic;
        vedit::editor::applyHighlightingMode(document, bigPath, &diagnostic);
        document->setModified(false);
        pump();
        reportFold(QStringLiteral("C2-setText-shown"), view);
    }
    return 0;
}
