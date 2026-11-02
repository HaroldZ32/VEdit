// Minimal Qt 6 Widgets program for the glibc-2.17 feasibility spike (T76).
// Runs with QT_QPA_PLATFORM=offscreen so no X server is needed; it exercises
// QApplication + QWidget + QLabel, i.e. the same GUI stack the product needs.
#include <QApplication>
#include <QLabel>
#include <QWidget>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    window.resize(200, 80);
    QLabel *label = new QLabel(QStringLiteral("verilog"), &window);
    label->move(10, 10);
    std::printf("qt6=%s platform=%s widgets=%d\n", qVersion(),
                qPrintable(QGuiApplication::platformName()), window.size().width());
    return 0;
}
