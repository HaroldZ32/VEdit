// Locale/CJK probe for T115 (headless; same Qt/fontconfig environment as the product).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLocale>
#include <QStringList>
#include <QTextLayout>
#include <QGlyphRun>
#include <QTextLine>
#include <cstdio>


static void report_font(const char *label, const QFont &font)
{
    const QFontInfo info(font);
    const QFontMetrics metrics(font);
    const int ascii = metrics.horizontalAdvance(QChar('A'));
    const int cjk = metrics.horizontalAdvance(QChar(0x6D4B));
    std::printf(
        "FONT[%s] family=%s inFont_CJK=%d inFontUcs4_CJK=%d adv_A=%d adv_CJK=%d "
        "ratio_CJK_over_A=%.2f ascii_adv=%d\n",
        label, qPrintable(info.family()), metrics.inFont(QChar(0x6D4B)),
        metrics.inFontUcs4(0x6D4B), ascii, cjk,
        ascii > 0 ? static_cast<double>(cjk) / static_cast<double>(ascii) : 0.0,
        metrics.horizontalAdvance(QStringLiteral("module wire assign;")));
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::printf("LOCALE system=%s\n", qPrintable(QLocale::system().name()));
    const QString utf8 = QString::fromUtf8("测试模块.sv");
    const QString local = QString::fromLocal8Bit("测试模块.sv");
    std::printf("FROM_UTF8 len=%lld hex=%s\n", static_cast<long long>(utf8.size()),
                utf8.toUtf8().toHex().constData());
    std::printf("FROM_LOCAL len=%lld eq_utf8=%d hex=%s\n", static_cast<long long>(local.size()),
                local == utf8, local.toUtf8().toHex().constData());

    const QString dir = QDir::tempPath() + QStringLiteral("/vedit-cjk-probe");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/测试模块.sv");
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    if (opened) {
        file.write("module 测试模块;\n// 中文注释\nendmodule\n");
        file.close();
    }
    std::printf("FILE_CREATE open=%d exists=%d fileinfo_exists=%d\n", opened,
                file.exists(), QFileInfo::exists(path));
    const QStringList names = QDir(dir).entryList(QDir::Files);
    for (const QString &name : names) {
        std::printf("DIR_ENTRY hex=%s eq_utf8=%d\n", name.toUtf8().toHex().constData(),
                    name == QString::fromUtf8("测试模块.sv"));
    }

    const QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    const QFontInfo info(font);
    const QFontMetrics metrics(font);
    const QStringList families = QFontDatabase::families();
    std::printf("FONT default_family=%s pixel_size=%d families=%lld\n", qPrintable(info.family()),
                metrics.height(), static_cast<long long>(families.size()));
    std::printf("FONT inFont_CJK=%d inFontUcs4_CJK=%d inFont_A=%d\n",
                metrics.inFont(QChar(0x6D4B)), metrics.inFontUcs4(0x6D4B), metrics.inFont(QChar('A')));
    const auto report_runs = [](const char *label, const QFont &font) {
        QTextLayout layout(QStringLiteral("测试A"), font);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            std::printf("RUNS[%s] no-line\n", label);
            return;
        }
        line.setLineWidth(500);
        layout.endLayout();
        std::printf("RUNS[%s]", label);
        for (const QGlyphRun &run : line.glyphRuns()) {
            std::printf(" family=%s glyphs=%lld", qPrintable(run.rawFont().familyName()),
                        static_cast<long long>(run.glyphIndexes().size()));
        }
        std::printf("\n");
    };
    report_runs("UI-DejaVuSans", QFont(QStringLiteral("DejaVu Sans")));
    report_runs("EDITOR-DejaVuSansMono", QFont(QStringLiteral("DejaVu Sans Mono")));
    report_font("UI-DejaVuSans", QFont(QStringLiteral("DejaVu Sans")));
    report_font("EDITOR-DejaVuSansMono", QFont(QStringLiteral("DejaVu Sans Mono")));
    report_font("EXPLICIT-NotoSansCJKSC", QFont(QStringLiteral("Noto Sans CJK SC")));
    report_font("EXPLICIT-NotoSansMonoCJKSC", QFont(QStringLiteral("Noto Sans Mono CJK SC")));
    report_font("DEFAULT", font);
    std::printf("FONT family_sample=");
    for (int i = 0; i < qMin(6, static_cast<int>(families.size())); ++i) {
        std::printf("%s,", qPrintable(families.at(i)));
    }
    std::printf("\n");
    return 0;
}
