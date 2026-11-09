// V8 机制探针（只用于确定改法，不参与产品构建）：
//   1. QWidget 上的 QSS `padding` 是否会把带 layout 的子控件内缩；
//   2. QWidget::setContentsMargins() 是否会把 layout 的子控件内缩；
//   3. QDockWidget::setWidget() 之后会不会在 dock 上收到 LayoutRequest
//      （决定 PanelRegistry 能不能用一个事件过滤器接住「lane 替换占位内容」）；
//   4. QGraphicsDropShadowEffect 在浅色下是否真的向父控件外画出像素。
#include <QApplication>
#include <QByteArray>
#include <QDockWidget>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QStyle>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <cstdio>

namespace {

class Watch : public QObject {
public:
    explicit Watch(QWidget *owner) : QObject(owner), owner_(owner) {}
    int layoutRequests = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == owner_ && event->type() == QEvent::LayoutRequest) {
            ++layoutRequests;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *owner_ = nullptr;
};

QImage grabWindow(QWidget *w)
{
    QImage image(w->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    w->render(&image);
    return image;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setStyleSheet(QStringLiteral(
        "QMainWindow { background: #202020; }"
        "QWidget[veditDockCard=\"true\"] { background: #FFFFFF; border: 1px solid #333333;"
        "  border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }"
        "QWidget[veditDockCard=\"true\"][padTest=\"qss\"] { padding: 3px; }"));
    auto *central = new QWidget;
    window.setCentralWidget(central);
    window.resize(600, 400);

    auto *dock = new QDockWidget(QStringLiteral("Files"));
    dock->setObjectName(QStringLiteral("dock-probe"));
    auto *body = new QWidget;
    body->setProperty("veditDockCard", true);
    body->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *tree = new QTreeView(body);
    tree->setObjectName(QStringLiteral("probeTree"));
    layout->addWidget(tree);
    dock->setWidget(body);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto *watch = new Watch(dock);
    dock->installEventFilter(watch);

    window.show();
    QTimer::singleShot(200, [&]() {
        std::printf("variant=baseline body=%dx%d tree geom=(%d,%d %dx%d) layoutMargins=(%d,%d,%d,%d)\n",
                    body->width(), body->height(), tree->x(), tree->y(), tree->width(), tree->height(),
                    layout->contentsMargins().left(), layout->contentsMargins().top(),
                    layout->contentsMargins().right(), layout->contentsMargins().bottom());

        // 变体 1：QWidget::setContentsMargins()
        body->setContentsMargins(3, 0, 3, 3);
        layout->invalidate();
        window.repaint();
        QCoreApplication::processEvents();
        std::printf("variant=widget-margins tree geom=(%d,%d %dx%d)\n", tree->x(), tree->y(),
                    tree->width(), tree->height());

        // 变体 2：QSS padding（动态属性 + repolish）
        body->setContentsMargins(0, 0, 0, 0);
        body->setProperty("padTest", QStringLiteral("qss"));
        body->style()->unpolish(body);
        body->style()->polish(body);
        layout->invalidate();
        window.repaint();
        QCoreApplication::processEvents();
        std::printf("variant=qss-padding tree geom=(%d,%d %dx%d)\n", tree->x(), tree->y(),
                    tree->width(), tree->height());

        // 变体 3：layout->setContentsMargins()
        body->setProperty("padTest", QVariant());
        body->style()->unpolish(body);
        body->style()->polish(body);
        layout->setContentsMargins(3, 0, 3, 3);
        layout->invalidate();
        window.repaint();
        QCoreApplication::processEvents();
        std::printf("variant=layout-margins tree geom=(%d,%d %dx%d)\n", tree->x(), tree->y(),
                    tree->width(), tree->height());

        // 变体 4：setWidget 替换内容后 dock 是否收到 LayoutRequest
        auto *replacement = new QWidget;
        auto *replacementLayout = new QVBoxLayout(replacement);
        replacementLayout->setContentsMargins(0, 0, 0, 0);
        replacementLayout->addWidget(new QTreeView(replacement));
        const int before = watch->layoutRequests;
        dock->setWidget(replacement);
        QCoreApplication::processEvents();
        std::printf("variant=setWidget dock-layout-requests-before=%d after=%d\n", before,
                    watch->layoutRequests);

        // 变体 5：浅色阴影是否画出 dock 之外
        auto *shadowDock = new QDockWidget(QStringLiteral("Shadow"));
        auto *shadowBody = new QWidget;
        shadowBody->setAutoFillBackground(true);
        QPalette pal = shadowBody->palette();
        pal.setColor(QPalette::Window, QColor(0xFF, 0xFF, 0xFF));
        shadowBody->setPalette(pal);
        shadowDock->setWidget(shadowBody);
        window.addDockWidget(Qt::RightDockWidgetArea, shadowDock);
        auto *effect = new QGraphicsDropShadowEffect(shadowDock);
        effect->setBlurRadius(12);
        effect->setOffset(0, 1);
        effect->setColor(QColor(0, 0, 0, 22));
        shadowDock->setGraphicsEffect(effect);
        window.repaint();
        QCoreApplication::processEvents();
        const QImage shot = grabWindow(&window);
        const int dockRight = shadowDock->geometry().right();
        int outside = 0;
        for (int x = dockRight + 1; x < qMin(dockRight + 20, shot.width()); ++x) {
            for (int y = shadowDock->geometry().top(); y <= shadowDock->geometry().bottom(); ++y) {
                const QRgb c = shot.pixel(x, y);
                if (qAbs(qGray(c) - 0x20) >= 3) {
                    ++outside;
                }
            }
        }
        std::printf("variant=shadow outside-dock-pixels=%d (canvas gray=32)\n", outside);
        app.quit();
    });
    return app.exec();
}
