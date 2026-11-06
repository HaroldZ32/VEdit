// Probe RM-5a step 3: is KParts::ReadOnlyPart::openUrl() a usable open path for
// a LOCAL file in this build, and does it leave the loaded state as the undo
// FLOOR (Ctrl+Z cannot walk back into the empty buffer)?
//
// The kernel deliberately avoided openUrl() (src/editor/kernel/document_io.h says
// the KIO path can spin under headless/offscreen). This probe re-measures that
// claim on the pinned image, for both offscreen and xcb, and compares the undo
// behaviour against the setText() path.
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QWidget>
#include <QtTest/QtTest>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }
bool undo(KTextEditor::Document *d) { return QMetaObject::invokeMethod(d, "undo"); }
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) { out(QStringLiteral("OPENURL editor=null FAIL")); return 2; }

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("top.sv"));
    const QString content = QStringLiteral("module top;\n  wire a;\nendmodule\n");
    { QFile file(path); file.open(QIODevice::WriteOnly); file.write(content.toUtf8()); }

    QWidget window;
    window.resize(900, 600);
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(&window);
    document->setParent(view);
    window.show();
    QTest::qWait(50);
    QApplication::processEvents();
    out(QStringLiteral("OPENURL platform=%1").arg(QApplication::platformName()));

    QElapsedTimer timer;
    timer.start();
    const bool opened = document->openUrl(QUrl::fromLocalFile(path));
    const qint64 openMs = timer.elapsed();
    out(QStringLiteral("OPENURL call returned=%1 elapsed_ms=%2 text=%3 match=%4 modified=%5 docname=%6")
            .arg(opened ? 1 : 0).arg(openMs)
            .arg(document->text().size())
            .arg(document->text() == content ? 1 : 0)
            .arg(document->isModified() ? 1 : 0)
            .arg(document->documentName()));

    // Three user edits, then walk the undo stack far past the bottom.
    for (int i = 0; i < 3; ++i) {
        document->insertText(KTextEditor::Cursor(document->lines() - 1, 0),
                             QStringLiteral("  wire b%1;\n").arg(i));
        QApplication::processEvents();
    }
    out(QStringLiteral("OPENURL after_edits text=%1 modified=%2")
            .arg(document->text().size()).arg(document->isModified() ? 1 : 0));
    for (int press = 1; press <= 8; ++press) {
        const bool ran = undo(document);
        QApplication::processEvents();
        out(QStringLiteral("OPENURL undo=%1 ran=%2 text=%3 floor_hit=%4")
                .arg(press).arg(ran ? 1 : 0).arg(document->text().size())
                .arg(document->text() == content ? 1 : 0));
    }

    // Typing after the undo walk still lands.
    view->setCursorPosition(KTextEditor::Cursor(document->lines() - 1, 0));
    const int before = document->text().size();
    QWidget *target = view->childAt(view->cursorToCoordinate(view->cursorPosition()));
    QWidget *deliver = target != nullptr ? target : view;
    deliver->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QTest::keyClicks(deliver, QStringLiteral("xyz"));
    QApplication::processEvents();
    out(QStringLiteral("OPENURL type before=%1 after=%2 landed=%3")
            .arg(before).arg(document->text().size())
            .arg(document->text().size() > before ? 1 : 0));
    return 0;
}
