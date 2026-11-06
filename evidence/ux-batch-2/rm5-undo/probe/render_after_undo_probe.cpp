// Probe RM-5a step 4: after the buffer is undone to empty, does the VIEW still
// render typing, or does it stay a dead white rectangle? This separates the two
// halves of the user's report: "blank" (buffer empty) vs "editing does nothing"
// (view not repainting).
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QImage>
#include <QMetaObject>
#include <QString>
#include <QTextStream>
#include <QWidget>
#include <QtTest/QtTest>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }
bool undo(KTextEditor::Document *d) { return QMetaObject::invokeMethod(d, "undo"); }
// Number of pixels that differ from the top-left corner colour: a blank view has
// only its background, a view showing text has glyph pixels.
QImage grab(QWidget *view) { return view->grab().toImage(); }

int inkPixels(const QImage &image)
{
    if (image.isNull()) {
        return -1;
    }
    const QRgb background = image.pixel(2, 2);
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != background) {
                ++count;
            }
        }
    }
    return count;
}

// Pixels that changed between two grabs of the SAME widget: the reliable signal
// that the viewport repainted the new text (an empty view and a text view differ
// only in the text area; the gutter is identical).
int changedPixels(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return -1;
    }
    int count = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) { out(QStringLiteral("RENDER editor=null FAIL")); return 2; }

    QWidget window;
    window.resize(900, 600);
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(&window);
    document->setParent(view);
    window.show();
    QTest::qWait(80);
    QApplication::processEvents();

    document->setText(QStringLiteral("module top;\n  wire a;\nendmodule\n"));
    document->setModified(false);
    QApplication::processEvents();
    const QImage loadedShot = grab(view);
    out(QStringLiteral("RENDER after_load ink=%1 text=%2")
            .arg(inkPixels(loadedShot)).arg(document->text().size()));

    // Three edits through the same programmatic seam the panic path used, then
    // one undo per edit until the buffer is empty (the pre-fix real-machine
    // state).
    for (int i = 0; i < 3; ++i) {
        document->insertText(KTextEditor::Cursor(document->lines() - 1, 0),
                             QStringLiteral("  wire b%1;\n").arg(i));
        QApplication::processEvents();
    }
    int presses = 0;
    while (!document->text().isEmpty() && presses < 10) {
        undo(document);
        QApplication::processEvents();
        ++presses;
    }
    const QImage emptyShot = grab(view);
    out(QStringLiteral("RENDER after_undo_to_empty presses=%1 text=%2 ink=%3 changed_vs_loaded=%4")
            .arg(presses).arg(document->text().size()).arg(inkPixels(emptyShot))
            .arg(changedPixels(loadedShot, emptyShot)));

    // Type into the emptied view: does the viewport paint the new text?
    view->setCursorPosition(KTextEditor::Cursor(0, 0));
    QWidget *target = view->childAt(view->cursorToCoordinate(view->cursorPosition()));
    QWidget *deliver = target != nullptr ? target : view;
    deliver->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClicks(deliver, QStringLiteral("ZZZZZZZZ"));
    QApplication::processEvents();
    QTest::qWait(60);
    QApplication::processEvents();
    const QImage typedShot = grab(view);
    out(QStringLiteral("RENDER after_typing text=%1 ink=%2 changed_vs_empty=%3")
            .arg(document->text().size()).arg(inkPixels(typedShot))
            .arg(changedPixels(emptyShot, typedShot)));
    return 0;
}
