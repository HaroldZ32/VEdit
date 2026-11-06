// Read-only review probe: enumerate the QActions a real KTextEditor
// Document/View creates, with their shortcuts, so the new registry sequence
// Ctrl+Shift+F can be checked against editor-internal bindings that are NOT in
// the product's command registry.
#include <QApplication>
#include <QAction>
#include <QString>
#include <cstdio>
#include <KTextEditor/Editor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("NO-KTEXTEDITOR-EDITOR\n");
        return 2;
    }
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    KTextEditor::View *view = doc != nullptr ? doc->createView(nullptr) : nullptr;
    if (doc == nullptr || view == nullptr) {
        std::printf("NO-DOCUMENT-OR-VIEW\n");
        return 3;
    }
    int count = 0;
    const auto dump = [&count](const char *owner, QObject *object) {
        for (QAction *action : object->findChildren<QAction *>()) {
            if (action == nullptr || action->shortcut().isEmpty()) {
                continue;
            }
            ++count;
            std::printf("%s ACTION shortcut='%s' text='%s'\n", owner,
                        qPrintable(action->shortcut().toString()),
                        qPrintable(action->text()));
        }
    };
    dump("VIEW", view);
    dump("DOC", doc);
    std::printf("TOTAL_ACTIONS_WITH_SHORTCUTS=%d\n", count);
    const QList<QAction *> all = view->findChildren<QAction *>() + doc->findChildren<QAction *>();
    bool hasCtrlShiftF = false;
    bool hasCtrlAltF = false;
    for (QAction *action : all) {
        for (const QKeySequence &sequence : action->shortcuts()) {
            if (sequence == QKeySequence(QStringLiteral("Ctrl+Shift+F"))) hasCtrlShiftF = true;
            if (sequence == QKeySequence(QStringLiteral("Ctrl+Alt+F"))) hasCtrlAltF = true;
        }
    }
    std::printf("KTEXTEDITOR_HAS_CTRL_SHIFT_F=%d\n", hasCtrlShiftF ? 1 : 0);
    std::printf("KTEXTEDITOR_HAS_CTRL_ALT_F=%d\n", hasCtrlAltF ? 1 : 0);
    return 0;
}
