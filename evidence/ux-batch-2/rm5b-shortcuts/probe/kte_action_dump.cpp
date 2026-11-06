// RM-5b probe: which key sequences does KTextEditor itself already own on a
// view? A registry default that duplicates one of these is AMBIGUOUS for Qt and
// makes BOTH actions dead, so the default scheme has to avoid them (or the
// editor action has to be stripped, as T80 does for Ctrl+S).
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QString>
#include <QTextStream>
#include <QWidget>

namespace {
void dump(const QString &owner, const QList<QAction *> &actions)
{
    for (QAction *action : actions) {
        if (action == nullptr || action->shortcuts().isEmpty()) {
            continue;
        }
        QStringList sequences;
        for (const QKeySequence &sequence : action->shortcuts()) {
            sequences.append(sequence.toString(QKeySequence::PortableText));
        }
        QTextStream(stdout) << "KTE-ACTION owner=" << owner
                            << " objectName=" << action->objectName()
                            << " text=" << action->text()
                            << " shortcuts=" << sequences.join(QLatin1Char(','))
                            << " context=" << static_cast<int>(action->shortcutContext())
                            << "\n";
    }
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        QTextStream(stdout) << "KTE-ACTION editor=null\n";
        return 2;
    }
    QWidget window;
    window.resize(800, 500);
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(&window);
    document->setParent(view);
    window.show();
    QApplication::processEvents();
    dump(QStringLiteral("view"), view->actions());
    for (QWidget *child : view->findChildren<QWidget *>()) {
        dump(QString::fromLatin1(child->metaObject()->className()), child->actions());
    }
    for (QAction *action : view->findChildren<QAction *>()) {
        if (!action->shortcuts().isEmpty()) {
            QStringList sequences;
            for (const QKeySequence &sequence : action->shortcuts()) {
                sequences.append(sequence.toString(QKeySequence::PortableText));
            }
            QTextStream(stdout) << "KTE-ACTION owner=qobject objectName=" << action->objectName()
                                << " text=" << action->text()
                                << " shortcuts=" << sequences.join(QLatin1Char(',')) << "\n";
        }
    }
    return 0;
}
