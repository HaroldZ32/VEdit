// Probe (read-only mechanism check, no product code): does an APPLICATION event
// filter — installed on qApp, exactly the way EditorArea installs its own — see a
// mouse press that Qt delivers through the WINDOW SYSTEM
// (QWindowSystemInterface -> QWidgetWindow -> child widget), i.e. the path a real
// X11 click takes? The deepest widget under the point (a QLabel child) is the
// analogue of KatePart's KateViewInternal.
//
// Two delivery routes are compared:
//   1. QTest::mousePress(QWidget*)   -> qApp->notify(widget, &event)
//   2. QTest::mouseClick(QWindow*)   -> qt_handleMouseEvent (spontaneous path)
//
// Build (inside the pinned image):
//   g++ -std=c++20 -fPIC probe_real_click_delivery.cpp -o /tmp/probe \
//       $(pkg-config --cflags --libs Qt6Widgets Qt6Test)
// Run under Xvfb to use the xcb platform, like the packaged product:
//   Xvfb :88 -screen 0 800x600x24 & DISPLAY=:88 /tmp/probe
#include <QApplication>
#include <QLabel>
#include <QTest>
#include <QWidget>

#include <cstdio>

namespace {

struct Filter : QObject {
    int seen = 0;
    int childSeen = 0;
    QObject *target = nullptr;
    QObject *child = nullptr;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event != nullptr && event->type() == QEvent::MouseButtonPress) {
            ++seen;
            target = watched;
            if (watched == child) {
                ++childSeen;
            }
            std::printf("APPFILTER press watched=%s\n",
                        watched != nullptr ? watched->metaObject()->className() : "null");
        }
        return false;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget host;
    host.resize(400, 300);
    auto *child = new QLabel(QStringLiteral("inner"), &host);
    child->setObjectName(QStringLiteral("inner"));
    child->setGeometry(50, 40, 200, 100);
    Filter filter;
    filter.child = child;
    app.installEventFilter(&filter);
    host.show();
    QTest::qWait(100);
    QApplication::processEvents();

    // Route 1: the widget overload used by the offscreen activation test.
    QTest::mousePress(child, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseRelease(child, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QApplication::processEvents();
    const int widgetRoute = filter.seen;
    const int widgetChild = filter.childSeen;
    std::printf("route=widget-notify seen=%d child=%d target=%s\n", widgetRoute, widgetChild,
                filter.target != nullptr ? filter.target->metaObject()->className() : "none");

    // Route 2: the WINDOW-SYSTEM route a real X11 click takes. The point is in
    // window coordinates and lands inside the child QLabel.
    filter.seen = 0;
    filter.childSeen = 0;
    filter.target = nullptr;
    QTest::mouseClick(host.windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 80));
    QApplication::processEvents();
    const int windowRoute = filter.seen;
    const int windowChild = filter.childSeen;
    std::printf("route=window-system seen=%d child=%d target=%s\n", windowRoute, windowChild,
                filter.target != nullptr ? filter.target->metaObject()->className() : "none");

    std::printf("SUMMARY widget-route=%d/%d window-route=%d/%d (child/total)\n", widgetChild,
                widgetRoute, windowChild, windowRoute);
    return (widgetChild > 0 && windowChild > 0) ? 0 : 1;
}
