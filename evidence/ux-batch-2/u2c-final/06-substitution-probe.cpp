// Cheap, real check for acceptance (4): the two diagnostics status strings still
// render their %1/%2/%3 substitutions AFTER translation, i.e. the translated
// body carried the placeholders and .arg() still fills them. It loads the
// compiled catalogue and calls exactly the strings the product now asks for.
#include <QCoreApplication>
#include <QString>
#include <QTranslator>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::printf("PROBE no-catalogue-argument\n");
        return 2;
    }
    QTranslator translator;
    if (!translator.load(QString::fromLocal8Bit(argv[1]))) {
        std::printf("PROBE load-failed %s\n", argv[1]);
        return 2;
    }
    QCoreApplication::installTranslator(&translator);
    const QString cancelled = QCoreApplication::translate("VEdit", "cancelled — %1/%2 file(s)")
                                  .arg(120)
                                  .arg(1000);
    const QString scanned =
        QCoreApplication::translate("VEdit", "%1/%2 file(s) scanned — %3 CLI diagnostics")
            .arg(120)
            .arg(1000)
            .arg(37);
    std::printf("PROBE cancelled=\"%s\"\n", qPrintable(cancelled));
    std::printf("PROBE scanned=\"%s\"\n", qPrintable(scanned));
    const bool ok = cancelled.contains(QStringLiteral("120/1000"))
        && scanned.contains(QStringLiteral("120/1000"))
        && scanned.contains(QStringLiteral("37"));
    std::printf("PROBE substitutions-ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 1;
}
