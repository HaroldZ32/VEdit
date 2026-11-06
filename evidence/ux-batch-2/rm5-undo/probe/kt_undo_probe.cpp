// Probe RM-5a step 1: what does KTextEditor do when the file was loaded with
// Document::setText() (the VEdit open path) and the user presses Ctrl+Z
// repeatedly?
//
// The product open path is src/editor/kernel/document_io.cpp:
// loadFileIntoDocument() -> document->setText(QString::fromUtf8(bytes)).
// If that lands on the undo stack, repeated undo walks back to the EMPTY
// document instead of stopping at the state the file was opened in.
//
// Phases:
//   A  control: type into a freshly loaded document (no undo) -> does typing land?
//   B  undo until the buffer stops changing, printing text/undo state per press
//   C  type again after the undos -> does the buffer still accept input?
//   D  destroy in the order the product destroys (document is a child of the view)
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QMetaObject>
#include <QString>
#include <QTextStream>
#include <QWidget>
#include <QtTest/QtTest>

namespace {

void out(const QString &line)
{
    QTextStream(stdout) << line << Qt::endl;
}

// Deepest child under the caret: KatePart delivers real keys to KateViewInternal,
// not to the public View (see src/app/editor_area_activation_test.cpp).
QWidget *innerWidget(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    const QPoint point = view->cursorToCoordinate(view->cursorPosition());
    QWidget *target = view->childAt(point);
    return target != nullptr ? target : view;
}

bool typeText(KTextEditor::View *view, const QString &text)
{
    QWidget *target = innerWidget(view);
    if (target == nullptr) {
        return false;
    }
    target->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClicks(target, text);
    QApplication::processEvents();
    return true;
}

int undoCount(KTextEditor::Document *document)
{
    int value = -1;
    if (!QMetaObject::invokeMethod(document, "undoCount", Q_RETURN_ARG(int, value))) {
        return -1;
    }
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        out(QStringLiteral("PROBE editor=null FAIL"));
        return 2;
    }

    QWidget *window = new QWidget;
    window->resize(900, 600);
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(window);
    document->setParent(view);
    window->show();
    QTest::qWait(80);
    QApplication::processEvents();

    // Exactly the product open path.
    document->setText(QStringLiteral("module top;\n  wire a;\nendmodule\n"));
    document->setModified(false);
    QApplication::processEvents();
    out(QStringLiteral("PROBE after_load text=%1 lines=%2 undoCount=%3 modified=%4 viewdoc_same=%5")
            .arg(document->text().size())
            .arg(document->lines())
            .arg(undoCount(document))
            .arg(document->isModified() ? 1 : 0)
            .arg(view->document() == document ? 1 : 0));

    // --- A: control — typing lands BEFORE any undo? -------------------------
    view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
    const int beforeA = document->text().size();
    typeText(view, QStringLiteral("abc"));
    out(QStringLiteral("PROBE A_control before=%1 after=%2 landed=%3")
            .arg(beforeA)
            .arg(document->text().size())
            .arg(document->text().size() > beforeA ? 1 : 0));

    // A few further user edits.
    for (int i = 0; i < 3; ++i) {
        document->insertText(KTextEditor::Cursor(document->lines() - 1, 0),
                             QStringLiteral("  wire b%1;\n").arg(i));
        QApplication::processEvents();
    }
    out(QStringLiteral("PROBE after_edits text=%1 undoCount=%2 modified=%3")
            .arg(document->text().size())
            .arg(undoCount(document))
            .arg(document->isModified() ? 1 : 0));

    // --- B: repeated undo ---------------------------------------------------
    QString previous = document->text();
    for (int press = 1; press <= 12; ++press) {
        const bool ran = QMetaObject::invokeMethod(document, "undo");
        QApplication::processEvents();
        const QString now = document->text();
        out(QStringLiteral("PROBE undo=%1 ran=%2 text=%3 lines=%4 modified=%5 changed=%6 "
                           "view_doc_same=%7")
                .arg(press)
                .arg(ran ? 1 : 0)
                .arg(now.size())
                .arg(document->lines())
                .arg(document->isModified() ? 1 : 0)
                .arg(now != previous ? 1 : 0)
                .arg(view->document() == document ? 1 : 0));
        previous = now;
    }

    // --- C: typing after the undo walk -------------------------------------
    const int beforeC = document->text().size();
    typeText(view, QStringLiteral("hello"));
    out(QStringLiteral("PROBE C_type_after_undo before=%1 after=%2 landed=%3 viewdoc_same=%4")
            .arg(beforeC)
            .arg(document->text().size())
            .arg(document->text().size() > beforeC ? 1 : 0)
            .arg(view->document() == document ? 1 : 0));

    // --- D: teardown in the PRODUCT's order ---------------------------------
    // The pane deletes the editor WIDGET (the view); the document is its child
    // and dies with it. Deleting the bare document first is not a product path
    // and crashes KTextEditor (it keeps using it), so the probe follows the pane.
    delete window;
    out(QStringLiteral("PROBE D_delete_window ok=1 (document died with its view)"));
    return 0;
}
