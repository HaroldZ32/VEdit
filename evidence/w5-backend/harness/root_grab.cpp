// Capture the root window of the current X display (used with Xvfb + vedit-app).
// The pinned image has no xwd/import, so the screenshot is taken with Qt itself:
//   xvfb-run -s "-screen 0 1600x1000x24" bash -c 'vedit-app ... & sleep 5; root-grab out.png'
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("root.png");
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        std::fprintf(stderr, "root-grab: no primary screen\n");
        return 1;
    }
    const QPixmap shot = screen->grabWindow(0);
    std::printf("root-grab: %dx%d -> %s\n", shot.width(), shot.height(),
                path.toUtf8().constData());
    return shot.isNull() ? 1 : (shot.save(path) ? 0 : 1);
}
