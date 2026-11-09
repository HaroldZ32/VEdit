// V5 机制探针 2：分离 separator width/height 的影响 + QDockWidget contentsMargins。
// 用法：probe2 <width> <height> [topMargin]
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int sepW = argc > 1 ? std::atoi(argv[1]) : 8;
    const int sepH = argc > 2 ? std::atoi(argv[2]) : 8;
    const int mTop = argc > 3 ? std::atoi(argv[3]) : 0;
    const int mBottom = argc > 4 ? std::atoi(argv[4]) : 0;
    QMainWindow w;
    w.setStyleSheet(
        QStringLiteral("QMainWindow::separator { background: #ff0000; width: %1px; height: %2px; }")
            .arg(sepW)
            .arg(sepH));
    auto *central = new QWidget;
    w.setCentralWidget(central);
    auto *left = new QDockWidget(QStringLiteral("left"));
    left->setWidget(new QLabel(QStringLiteral("L")));
    w.addDockWidget(Qt::LeftDockWidgetArea, left);
    auto *r1 = new QDockWidget(QStringLiteral("r1"));
    r1->setWidget(new QLabel(QStringLiteral("one")));
    auto *r2 = new QDockWidget(QStringLiteral("r2"));
    r2->setWidget(new QLabel(QStringLiteral("two")));
    w.addDockWidget(Qt::RightDockWidgetArea, r1);
    w.addDockWidget(Qt::RightDockWidgetArea, r2);
    w.splitDockWidget(r1, r2, Qt::Vertical);
    if (mTop > 0 || mBottom > 0) {
        r1->setContentsMargins(0, mTop, 0, mBottom);
        r2->setContentsMargins(0, mTop, 0, mBottom);
    }
    w.resize(1280, 820);
    w.show();
    QTimer::singleShot(300, [&]() {
        QWidget *c1 = r1->widget();
        std::printf("sep=(w%d,h%d) m=%d: central.x=%d left.right=%d | "
                    "r1=(%d,%d %dx%d) r2=(%d,%d %dx%d) vgap=%d | content1=(%d,%d %dx%d)\n",
                    sepW, sepH, mTop * 100 + mBottom, central->x(), left->geometry().right(),
                    r1->x(), r1->y(), r1->width(), r1->height(), r2->x(), r2->y(),
                    r2->width(), r2->height(), r2->y() - (r1->y() + r1->height()),
                    c1->x(), c1->y(), c1->width(), c1->height());
        QApplication::quit();
    });
    return app.exec();
}
