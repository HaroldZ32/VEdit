// Probe RM-5a step 2: full invokable-method dump of the runtime
// KTextEditor::Document (finding a public way to make the loaded text the undo
// FLOOR: opening a file must never be an undoable step).
#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <QApplication>
#include <QMetaMethod>
#include <QString>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        QTextStream(stdout) << "API editor=null FAIL\n";
        return 2;
    }
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    QTextStream out(stdout);
    for (const QMetaObject *mo = doc->metaObject(); mo != nullptr; mo = mo->superClass()) {
        out << "API class=" << mo->className() << "\n";
        for (int i = 0; i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            out << "API   kind=" << static_cast<int>(m.methodType())
                << " sig=" << QByteArray(m.methodSignature()) << "\n";
        }
    }
    // Direct runtime probes of the two candidates.
    out << "API probe setUndoMergeAllEdits idx="
        << doc->metaObject()->indexOfMethod("setUndoMergeAllEdits(bool)") << "\n";
    delete doc;
    return 0;
}
