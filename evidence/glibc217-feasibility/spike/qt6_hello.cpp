// Minimal Qt 6 console program used by the glibc-2.17 feasibility spike (T76).
// It exercises QtCore only: QCoreApplication, QString/QStringList, QFile.
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList list;
    list << QStringLiteral("verilog") << QStringLiteral("systemverilog");
    QFile file(QStringLiteral("/etc/hostname"));
    std::printf("qt6=%s list=%s hostname-readable=%d\n", qVersion(),
                qPrintable(list.join(QLatin1Char(','))), file.exists() ? 1 : 0);
    return 0;
}
