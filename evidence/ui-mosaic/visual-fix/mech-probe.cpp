// V5 机制探针：确认 QSS 的 QMainWindow::separator width/height 是否分别控制
// 横向/纵向 dock 间隙，以及 QGraphicsDropShadowEffect 在 dock 上的绘制范围。
// 只用于确定改法，不参与产品构建。
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QMainWindow w;
    w.setStyleSheet("QMainWindow::separator { background: #ff0000; width: 8px; height: 24px; }");
    auto *central = new QWidget;
    w.setCentralWidget(central);
    auto *left = new QDockWidget(QStringLiteral("left"));
    left->setWidget(new QLabel(QStringLiteral("L")));
    w.addDockWidget(Qt::LeftDockWidgetArea, left);
    auto *r1 = new QDockWidget(QStringLiteral("r1"));
    r1->setWidget(new QLabel(QStringLiteral("one")));
    auto *r2 = new QDockWidget(QStringLiteral("r2"));
    r2->setWidget(new QLabel(QStringLiteral("two")));
    auto *r3 = new QDockWidget(QStringLiteral("r3"));
    r3->setWidget(new QLabel(QStringLiteral("three")));
    w.addDockWidget(Qt::RightDockWidgetArea, r1);
    w.addDockWidget(Qt::RightDockWidgetArea, r2);
    w.addDockWidget(Qt::RightDockWidgetArea, r3);
    w.splitDockWidget(r1, r2, Qt::Vertical);
    w.splitDockWidget(r2, r3, Qt::Vertical);
    w.resize(1280, 820);
    w.show();
    QTimer::singleShot(300, [&]() {
        std::printf("central.x=%d left.width=%d left.right=%d\n", central->x(),
                    left->width(), left->geometry().right());
        int prevBottom = -1;
        for (QDockWidget *d : {r1, r2, r3}) {
            std::printf("%s geom=(%d,%d %dx%d)\n", d->objectName().toUtf8().constData(),
                        d->x(), d->y(), d->width(), d->height());
            if (prevBottom >= 0) {
                std::printf("  vertical gap=%d\n", d->y() - prevBottom - 1);
            }
            prevBottom = d->y() + d->height() - 1;
        }
        app.quit();
    });
    return app.exec();
}
