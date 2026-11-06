// Probe RM-5a step 6: KParts::ReadWritePart::openUrl() calls closeUrl() first,
// and closeUrl() QUERIES THE USER when the document is modified — a modal
// "save changes?" dialog. That is what hung the kernel selftest (it calls
// EditorDocument::openFile() on a document that setText() had just marked
// modified) and it is a hazard for any non-interactive load.
//
//   mode=modified     : setText() (modified=true), then openUrl -> expect a hang
//   mode=cleared      : setText(), setModified(false), then openUrl -> expect it
//                       to return immediately (the load helper's own contract)
// Run each under `timeout 15`; a HANG comes back as exit 124.
#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <QApplication>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) return 2;
    const QString mode = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("cleared");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("top.sv"));
    { QFile f(path); if (!f.open(QIODevice::WriteOnly)) return 2; f.write("module top;\nendmodule\n"); }

    KTextEditor::Document *document = editor->createDocument(nullptr);
    document->setText(QStringLiteral("stale content that was modified\n"));
    QTextStream(stdout) << "MOD mode=" << mode << " modified_after_setText="
                        << (document->isModified() ? 1 : 0) << Qt::endl;
    if (mode == QStringLiteral("cleared")) {
        document->setModified(false);
    }
    QTextStream(stdout).flush();
    const bool opened = document->openUrl(QUrl::fromLocalFile(path));
    QTextStream(stdout) << "MOD mode=" << mode << " openUrl_returned=" << (opened ? 1 : 0)
                        << " text=" << document->text().size()
                        << " modified=" << (document->isModified() ? 1 : 0) << Qt::endl;
    return 0;
}
