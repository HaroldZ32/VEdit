// Read-only probe: does a real plain click (through KatePart's inner widget)
// collapse a pre-existing multi-cursor set, and does a Ctrl+click leave it?
#include "lsp_actions.h"
#include "multi_cursor.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QTest>
#include <QVariantMap>
#include <QWidget>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        return 77;
    }
    QMainWindow host;
    KTextEditor::Document *document = editor->createDocument(nullptr);
    KTextEditor::View *view = document->createView(&host);
    host.setCentralWidget(view);
    host.resize(820, 420);
    document->setText(QStringLiteral("alpha\nbravo\ncharlie\ndelta\n"));
    view->setProperty("vedit.openFilePath", QStringLiteral("/tmp/b4-definition.sv"));
    host.show();
    view->setFocus();
    QApplication::processEvents();
    QTest::qWait(80);
    QApplication::processEvents();

    QWidget *inner = nullptr;
    for (QWidget *w : view->findChildren<QWidget *>()) {
        if (QString::fromLatin1(w->metaObject()->className()) == QStringLiteral("KateViewInternal")) {
            inner = w;
        }
    }
    if (inner == nullptr) {
        return 1;
    }
    auto *action = new QAction(QStringLiteral("Def"), &host);
    action->setObjectName(QStringLiteral("action-diagnostics.definition"));
    action->setEnabled(true);
    host.addAction(action);
    QObject::connect(action, &QAction::triggered, &host, []() {});
    QVariantMap gates;
    QVariantMap row;
    row.insert(QStringLiteral("enabled"), true);
    row.insert(QStringLiteral("known"), true);
    row.insert(QStringLiteral("source"), QStringLiteral("backend"));
    row.insert(QStringLiteral("reason"), QString());
    gates.insert(QStringLiteral("definition"), row);
    host.setProperty("vedit.diagnostics.gates", QVariant(gates));
    vedit::editor::LspActionChannel::install(view);
    vedit::editor::MultiCursorController *cursors = vedit::editor::MultiCursorController::install(view);

    const auto positions = [&]() {
        QString out;
        for (const KTextEditor::Cursor &c : view->cursorPositions()) {
            out += QStringLiteral(" %1:%2").arg(c.line()).arg(c.column());
        }
        return out;
    };

    // plain click through the inner widget
    vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                                         KTextEditor::Cursor(2, 0)});
    const QPoint p = inner->mapFrom(view, view->cursorToCoordinate(KTextEditor::Cursor(2, 2)));
    QTest::mousePress(inner, Qt::LeftButton, Qt::NoModifier, p);
    QTest::mouseRelease(inner, Qt::LeftButton, Qt::NoModifier, p);
    QApplication::processEvents();
    std::printf("plain click on inner: count=%d positions:%s\n", cursors->cursorCount(),
                qPrintable(positions()));

    // Ctrl+click through the inner widget
    vedit::editor::setViewCursors(view, {KTextEditor::Cursor(0, 0), KTextEditor::Cursor(1, 0),
                                         KTextEditor::Cursor(2, 0)});
    QTest::mousePress(inner, Qt::LeftButton, Qt::ControlModifier, p);
    QTest::mouseRelease(inner, Qt::LeftButton, Qt::ControlModifier, p);
    QApplication::processEvents();
    std::printf("ctrl click on inner: count=%d positions:%s\n", cursors->cursorCount(),
                qPrintable(positions()));
    return 0;
}
