// Evidence probe for the INLINE SQUIGGLE (T9, AC-5.1..5.4).
//
// Drives the product's DiagnosticsAnnotations against a real KTextEditor
// Document/View (headless, offscreen) and asserts:
//   * AC-5.1  an error span gets a wavy SpellCheckUnderline (red) and the gutter
//             annotation model is still installed;
//   * AC-5.2  a warning span is orange and the two colors differ;
//   * AC-5.3  replacing the entries removes the ranges; a text edit moves the
//             MovingRange with the text; closing (destroying) the view leaves
//             no live range behind (this is asserted AFTER teardown explicitly
//             because a teardown-only leak is easy to miss);
//   * AC-5.4  a line never carries more than 1024 attribute ranges - extra
//             entries are merged - and a 10k-line document scrolls without a
//             crash while keeping its ranges.
//
// It writes the error/warning screenshot to evidence/d-diagnostics/.
#include "diagnostics_annotations.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTextCharFormat>

#include <algorithm>

#include <cstdio>

using vedit::editor::DiagnosticAnnotation;
using vedit::editor::DiagnosticsAnnotations;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
}

void checkLong(const char *name, long long actual, long long expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual=%lld expected=%lld\n", name, ok ? "PASS" : "FAIL", actual,
                expected);
    std::fflush(stdout);
}

void checkString(const char *name, const QString &actual, const QString &expected)
{
    const bool ok = actual == expected;
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s actual='%s' expected='%s'\n", name, ok ? "PASS" : "FAIL",
                actual.toUtf8().constData(), expected.toUtf8().constData());
    std::fflush(stdout);
}

void pump(int ms = 40)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

DiagnosticAnnotation entry(int line, int column, int endLine, int endColumn, int severity,
                           const QString &message)
{
    DiagnosticAnnotation annotation;
    annotation.line = line;
    annotation.column = column;
    annotation.endLine = endLine;
    annotation.endColumn = endColumn;
    annotation.severity = severity;
    annotation.message = message;
    annotation.rule = QStringLiteral("fixture-rule");
    return annotation;
}

QString sampleText()
{
    return QStringLiteral(
        "module top;\n"          // 0
        "  logic a;\n"           // 1
        "  wire w = a b;\n"      // 2 - the error span is "b;"
        "  assign w = a;\n"      // 3
        "  always_comb begin\n"  // 4 - the warning span is "always_comb"
        "endmodule\n"           // 5
        "\n\n\n");                // 6..8 empty, used for the border-only baseline
}

int runFixture(const QString &outDir)
{
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    check("editor.instance", editor != nullptr);
    if (editor == nullptr) {
        return g_failures;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    check("document.created", document != nullptr);
    if (document == nullptr) {
        return g_failures;
    }
    QWidget host;
    KTextEditor::View *view = document->createView(&host);
    check("view.created", view != nullptr);
    if (view == nullptr) {
        delete document;
        return g_failures;
    }
    // The product shape: the document dies with its view (ktexteditor_provider).
    document->setParent(view);
    host.resize(920, 460);
    view->resize(900, 420);
    host.show();
    view->show();
    pump(100);

    const bool textSet = document->setText(sampleText());
    check("document.text-set", textSet);
    pump(50);

    DiagnosticsAnnotations *annotations = DiagnosticsAnnotations::install(view);
    check("annotations.installed", annotations != nullptr);
    if (annotations == nullptr) {
        delete view;
        return g_failures;
    }

    annotations->setEntries({
        entry(2, 13, 2, 15, 1, QStringLiteral("undeclared identifier")),   // "b;"
        entry(4, 2, 4, 13, 2, QStringLiteral("unsized constant")),        // "always_comb"
        entry(1, 0, -1, -1, 3, QStringLiteral("line-level information")), // whole line
    });
    pump(60);

    // AC-5.1: the gutter annotation model stays; the squiggle is a moving range.
    checkLong("underline.range-count", annotations->underlineRangeCount(), 3);
    checkLong("gutter.annotated-lines", annotations->annotatedLineCount(), 3);
    check("gutter.tooltip-kept",
          annotations->tooltipForLine(2).contains(QStringLiteral("undeclared identifier"))
              && annotations->tooltipForLine(2).contains(QStringLiteral("fixture-rule")));

    checkLong("error.style-is-spellcheck",
              annotations->underlineStyleForLine(2),
              static_cast<int>(QTextCharFormat::SpellCheckUnderline));
    checkLong("warning.style-is-spellcheck",
              annotations->underlineStyleForLine(4),
              static_cast<int>(QTextCharFormat::SpellCheckUnderline));
    const QColor errorColor = annotations->underlineColorForLine(2);
    const QColor warningColor = annotations->underlineColorForLine(4);
    std::printf("OBSERVED underline colours error=%s warning=%s z=%g\n",
                qPrintable(errorColor.name()), qPrintable(warningColor.name()),
                annotations->underlineZDepthForLine(2));
    check("error.colour-red", errorColor == QColor(0xc6, 0x28, 0x28));
    check("warning.colour-orange", warningColor == QColor(0xb2, 0x6a, 0x00));
    check("colors.differ", errorColor != warningColor);
    check("underline.z-depth",
          qAbs(annotations->underlineZDepthForLine(2) + 90000.0) < 0.5);
    checkString("error.exact-span-text", annotations->underlineTextForLine(2),
                QStringLiteral("b;"));
    checkString("line-level.whole-line", annotations->underlineTextForLine(1),
                QStringLiteral("  logic a;"));

    // AC-5.1/5.2 pixel evidence: diff the rendered view WITH and WITHOUT the
    // annotations, restricted to the text area (the gutter always changes). This
    // proves the squiggle is actually PAINTED, not merely attached as an object.
    {
        // The baseline keeps the SAME annotation border (three glyphs on empty
        // lines, which draw no underline), so the diff is only the squiggle and
        // not a text re-layout caused by the border width.
        view->setCursorPosition(KTextEditor::Cursor(8, 0));
        annotations->setEntries({
            entry(6, 0, -1, -1, 1, QStringLiteral("baseline")),
            entry(7, 0, -1, -1, 2, QStringLiteral("baseline")),
            entry(8, 0, -1, -1, 3, QStringLiteral("baseline")),
        });
        pump(40);
        const QImage bare = view->grab().toImage();
        annotations->setEntries({
            entry(2, 13, 2, 15, 1, QStringLiteral("undeclared identifier")),
            entry(4, 2, 4, 13, 2, QStringLiteral("unsized constant")),
            entry(1, 0, -1, -1, 3, QStringLiteral("line-level information")),
        });
        pump(60);
        const QImage painted = view->grab().toImage();
        int changed = 0;
        int redPixels = 0;
        int orangePixels = 0;
        int minX = painted.width();
        int minY = painted.height();
        int maxX = -1;
        int maxY = -1;
        for (int y = 0; y < painted.height() && y < bare.height(); ++y) {
            for (int x = 50; x < painted.width() && x < bare.width(); ++x) {
                const QColor before = bare.pixelColor(x, y);
                const QColor after = painted.pixelColor(x, y);
                if (before == after) {
                    continue;
                }
                ++changed;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                if (after.red() > after.green() + 40 && after.red() > after.blue() + 40) {
                    ++redPixels;
                }
                if (after.red() > 150 && after.green() > 60 && after.green() < 160
                    && after.blue() < 70) {
                    ++orangePixels;
                }
            }
        }
        // Restrict the proof to the diagnostic's own line band: the squiggle
        // must repaint THAT line, and a diagnostic-free line is the control.
        const auto countChangedBand = [&bare, &painted, view](int line) {
            // The band of `line` runs from its top to the next line's top, so
            // neighbouring lines can never leak into the count.
            const int top = std::max(0, view->cursorToCoordinate(KTextEditor::Cursor(line, 0)).y());
            const int nextTop =
                view->cursorToCoordinate(KTextEditor::Cursor(line + 1, 0)).y();
            const int bottom = std::min(painted.height() - 1, nextTop - 1);
            int count = 0;
            for (int y = top; y <= bottom; ++y) {
                for (int x = 50; x < painted.width(); ++x) {
                    if (bare.pixelColor(x, y) != painted.pixelColor(x, y)) {
                        ++count;
                    }
                }
            }
            return count;
        };
        const int errorBand = countChangedBand(2);
        const int warningBand = countChangedBand(4);
        const int controlBand = countChangedBand(3);
        std::printf("PIXELS text-area changed=%d bbox=(%d,%d)-(%d,%d) red-ish=%d orange-ish=%d "
                    "bands(error=%d warning=%d control=%d)\n",
                    changed, minX, minY, maxX, maxY, redPixels, orangePixels, errorBand,
                    warningBand, controlBand);
        check("pixels.squiggle-painted", changed > 0);
        check("pixels.red-present", redPixels > 0);
        check("pixels.orange-present", orangePixels > 0);
        check("pixels.error-line-repainted", errorBand > 0);
        check("pixels.warning-line-repainted", warningBand > 0);
        check("pixels.control-line-untouched", controlBand == 0);
        // A zoomed crop of the error line makes the wavy underline readable by a
        // human reviewer without opening the full window.
        const QImage crop = painted.copy(std::max(0, minX - 10), std::max(0, minY - 16),
                                         std::min(painted.width() - minX + 10, 420), 90);
        const QString cropPath = outDir + QStringLiteral("/underline-zoom.png");
        const bool cropSaved = crop.scaled(crop.width() * 3, crop.height() * 3,
                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .save(cropPath);
        std::printf("SHOT underline-zoom '%s' saved=%d\n", cropPath.toUtf8().constData(),
                    cropSaved ? 1 : 0);
        check("shot.underline-zoom-saved", cropSaved);
    }

    // AC-5.1/5.2 screenshot: both severities visible in one frame.
    const QString shot = outDir + QStringLiteral("/underline-error-warning.png");
    const bool saved = view->grab().save(shot);
    std::printf("SHOT underline '%s' saved=%d\n", shot.toUtf8().constData(), saved ? 1 : 0);
    check("shot.underline-saved", saved);

    // AC-5.3 (edit tracking): a MovingRange follows the inserted line.
    const bool inserted = document->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("// header\n"));
    check("edit.inserted", inserted);
    pump(60);
    std::printf("OBSERVED after-edit ranges=%d start(3,13)=%d text='%s'\n",
                annotations->underlineRangeCount(), annotations->underlineStartsAt(3, 13) ? 1 : 0,
                annotations->underlineTextForLine(3).toUtf8().constData());
    check("edit.range-moved", annotations->underlineStartsAt(3, 13));
    checkString("edit.range-text-unchanged", annotations->underlineTextForLine(3),
                QStringLiteral("b;"));

    // AC-5.3 (fixed diagnostic): replacing the entries releases the ranges.
    annotations->setEntries({});
    pump(40);
    checkLong("fix.ranges-cleared", annotations->underlineRangeCount(), 0);
    checkLong("fix.no-live-ranges", DiagnosticsAnnotations::activeRangeCount(), 0);

    // AC-5.4 (per-line cap): 1500 entries on one line -> 1024 ranges + merges.
    QString wide;
    wide.reserve(4000);
    for (int index = 0; index < 4000; ++index) {
        wide.append(QLatin1Char('x'));
    }
    document->setText(wide + QLatin1Char('\n'));
    pump(40);
    QList<DiagnosticAnnotation> many;
    for (int index = 0; index < 1500; ++index) {
        many.append(entry(0, index, 0, index + 1, 1, QStringLiteral("many")));
    }
    annotations->setEntries(many);
    pump(80);
    const int perLine = annotations->underlineRangeCountForLine(0);
    std::printf("OBSERVED cap ranges=%d per-line=%d merged=%d max=%d\n",
                annotations->underlineRangeCount(), perLine, annotations->mergedRangeCount(),
                DiagnosticsAnnotations::maxRangesPerLine());
    checkLong("cap.per-line-limit", perLine, 1024);
    checkLong("cap.max-ranges-per-line", DiagnosticsAnnotations::maxRangesPerLine(), 1024);
    checkLong("cap.merged-count", annotations->mergedRangeCount(), 1500 - 1024);
    check("cap.line-still-underlined", perLine > 0);
    annotations->setEntries({});
    pump(20);
    checkLong("cap.cleared", DiagnosticsAnnotations::activeRangeCount(), 0);

    // AC-5.4 (large file): 10k lines, 500 spans, scroll and keep them.
    QStringList lines;
    for (int index = 0; index < 10000; ++index) {
        lines.append(QStringLiteral("  logic signal_%1;").arg(index));
    }
    document->setText(lines.join(QLatin1Char('\n')));
    pump(60);
    QList<DiagnosticAnnotation> manyLines;
    for (int index = 0; index < 500; ++index) {
        manyLines.append(entry(index * 10, 8, index * 10, 16, index % 2 == 0 ? 1 : 2,
                               QStringLiteral("span %1").arg(index)));
    }
    annotations->setEntries(manyLines);
    pump(80);
    checkLong("large.ranges", annotations->underlineRangeCount(), 500);
    view->setCursorPosition(KTextEditor::Cursor(9000, 4));
    pump(120);
    const QString largeShot = outDir + QStringLiteral("/underline-large-file.png");
    const bool largeSaved = view->grab().save(largeShot);
    std::printf("SHOT large-file '%s' saved=%d ranges-after-scroll=%d\n",
                largeShot.toUtf8().constData(), largeSaved ? 1 : 0,
                annotations->underlineRangeCount());
    check("large.scroll-survived", annotations->underlineRangeCount() == 500);
    check("shot.large-file-saved", largeSaved);

    // AC-5.3 (closed file): destroying the view must release every range. This
    // is checked AFTER the teardown on purpose - a teardown-only leak must not
    // hide behind the earlier green checks.
    delete view;
    pump(60);
    std::printf("OBSERVED after-close live-ranges=%d\n",
                DiagnosticsAnnotations::activeRangeCount());
    checkLong("close.no-residual-ranges", DiagnosticsAnnotations::activeRangeCount(), 0);
    return g_failures;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString outDir =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("evidence/d-diagnostics");
    QDir().mkpath(outDir);

    runFixture(outDir);

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
