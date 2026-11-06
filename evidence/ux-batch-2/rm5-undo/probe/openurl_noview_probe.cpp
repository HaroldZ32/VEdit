// Probe RM-5a step 5: does KParts::ReadOnlyPart::openUrl() for a LOCAL file
// depend on a view being attached (and/or events being pumped)?
// The kernel selftest opens files on a BARE document (no view, no window, no
// event loop) and hung after the openUrl change, so this isolates the trigger.
//
//   probe noview-loop  : bare document, openUrl, with processEvents pumping
//   probe noview-nopump: bare document, openUrl, NO event pumping
//   probe view-nopump  : document WITH a view, NO event pumping
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QWidget>
#include <QtTest/QtTest>

namespace {
void out(const QString &line) { QTextStream(stdout) << line << Qt::endl; }
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) { out(QStringLiteral("NV editor=null FAIL")); return 2; }
    const QString mode = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("noview-nopump");

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("top.sv"));
    const QString content = QStringLiteral("module top;\nendmodule\n");
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write(content.toUtf8()); }

    KTextEditor::Document *document = editor->createDocument(nullptr);
    QWidget *window = nullptr;
    if (mode.startsWith(QStringLiteral("view"))) {
        window = new QWidget;
        auto *view = document->createView(window);
        document->setParent(view);
        window->show();
        QTest::qWait(50);
        QApplication::processEvents();
    }
    out(QStringLiteral("NV mode=%1 views=%2 pumping=%3 before_open")
            .arg(mode).arg(document->views().size())
            .arg(mode.endsWith(QStringLiteral("loop")) ? 1 : 0));
    QTextStream(stdout).flush();
    const bool opened = document->openUrl(QUrl::fromLocalFile(path));
    out(QStringLiteral("NV mode=%1 openUrl_returned=%2 text=%3")
            .arg(mode).arg(opened ? 1 : 0).arg(document->text().size()));
    if (mode.endsWith(QStringLiteral("loop"))) {
        for (int i = 0; i < 100 && document->text().isEmpty(); ++i) {
            QApplication::processEvents();
            QTest::qWait(5);
        }
        out(QStringLiteral("NV mode=%1 after_pump text=%2").arg(mode).arg(document->text().size()));
    }
    return 0;
}
