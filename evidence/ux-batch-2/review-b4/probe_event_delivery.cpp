// Read-only probe: does a filter installed on the KTextEditor::View (the way
// EditorPane::wireEditor installs one) see focus / mouse events when the user
// clicks and focuses KatePart's inner widget?
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QTest>
#include <QWidget>

#include <cstdio>

class Spy : public QObject
{
public:
    Spy(const char *name, QObject *parent = nullptr)
        : QObject(parent)
        , name_(name)
    {
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusAboutToChange
            || event->type() == QEvent::MouseButtonPress) {
            std::printf("SPY %s watched=%s type=%d\n", name_,
                        watched != nullptr ? watched->metaObject()->className() : "?",
                        static_cast<int>(event->type()));
        }
        return false;
    }

private:
    const char *name_;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return 77;
    }
    QMainWindow host;
    auto *central = new QWidget(&host);
    auto *layout = new QHBoxLayout(central);
    host.setCentralWidget(central);
    KTextEditor::Document *docA = editor->createDocument(nullptr);
    KTextEditor::Document *docB = editor->createDocument(nullptr);
    KTextEditor::View *a = docA->createView(central);
    KTextEditor::View *b = docB->createView(central);
    layout->addWidget(a);
    layout->addWidget(b);
    docA->setText(QStringLiteral("alpha one\nalpha two\n"));
    docB->setText(QStringLiteral("beta one\nbeta two\n"));
    host.resize(900, 400);
    host.show();
    QApplication::processEvents();
    QTest::qWait(60);

    Spy spyA("A");
    Spy spyB("B");
    a->installEventFilter(&spyA);
    b->installEventFilter(&spyB);

    a->setFocus();
    QApplication::processEvents();
    std::printf("focus after A: %s A.hasFocus=%d B.hasFocus=%d\n",
                QApplication::focusWidget() != nullptr
                    ? QApplication::focusWidget()->metaObject()->className()
                    : "none",
                a->hasFocus() ? 1 : 0, b->hasFocus() ? 1 : 0);

    QWidget *innerB = nullptr;
    for (QWidget *w : b->findChildren<QWidget *>()) {
        if (QString::fromLatin1(w->metaObject()->className()) == QStringLiteral("KateViewInternal")) {
            innerB = w;
        }
    }
    if (innerB == nullptr) {
        std::printf("no inner B\n");
        return 1;
    }
    std::printf("--- click inner B ---\n");
    const QPoint point = innerB->mapFrom(b, b->cursorToCoordinate(KTextEditor::Cursor(1, 2)));
    QTest::mousePress(innerB, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseRelease(innerB, Qt::LeftButton, Qt::NoModifier, point);
    QApplication::processEvents();
    std::printf("after click: focus=%s A.hasFocus=%d B.hasFocus=%d\n",
                QApplication::focusWidget() != nullptr
                    ? QApplication::focusWidget()->metaObject()->className()
                    : "none",
                a->hasFocus() ? 1 : 0, b->hasFocus() ? 1 : 0);

    std::printf("--- b->setFocus() (what dispatchDefinitionAt does) ---\n");
    b->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    std::printf("after b setFocus: focus=%s A.hasFocus=%d B.hasFocus=%d\n",
                QApplication::focusWidget() != nullptr
                    ? QApplication::focusWidget()->metaObject()->className()
                    : "none",
                a->hasFocus() ? 1 : 0, b->hasFocus() ? 1 : 0);
    return 0;
}
