// Tiny X11 root-window grabber for the T18 shipped-binary evidence: the pinned
// build image has no xwd/import, so the screenshot is taken through Qt itself.
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: grab-root <out.png>\n");
        return 2;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        std::printf("FAIL no screen\n");
        return 3;
    }
    const QPixmap pixmap = screen->grabWindow(0);
    const bool ok = pixmap.save(QString::fromLocal8Bit(argv[1]));
    std::printf("SHOT %s %s %dx%d\n", ok ? "OK" : "FAIL", argv[1], pixmap.width(),
                pixmap.height());
    return ok ? 0 : 4;
}
