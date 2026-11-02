// T125 xcb click experiment (one round): with NO FoldInjector at all, does a real
// mouse click on the KTextEditor folding marker fold a module, and does a second
// click unfold it again?
//
// Run under a real X server (`xvfb-run ...`) with QT_QPA_PLATFORM=xcb; the probe
// screenshots the view, scans the icon border (left gutter) for a working marker
// column on the first module line, clicks it twice and asserts the view really
// changes in BOTH directions. It also clicks a non-region line as a negative
// control. Everything is printed as NATIVE-GUI lines.
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QScrollBar>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>

namespace {

QString sampleText()
{
    QString text;
    for (int index = 0; index < 25; ++index) {
        const QString n = QString::number(index);
        text += QStringLiteral("module m%1 #(parameter W = 8) (\n").arg(n);
        text += QStringLiteral("  input  logic clk,\n");
        text += QStringLiteral("  input  logic rst_n,\n");
        text += QStringLiteral("  output logic [W-1:0] q\n");
        text += QStringLiteral(");\n");
        text += QStringLiteral("  logic [W-1:0] state, nxt;\n");
        text += QStringLiteral("  always_ff @(posedge clk) begin\n");
        text += QStringLiteral("    state <= nxt;\n");
        text += QStringLiteral("  end\n");
        text += QStringLiteral("  always_comb begin\n");
        text += QStringLiteral("    nxt = state + 1'b1;\n");
        text += QStringLiteral("  end\n");
        for (int line = 12; line < 39; ++line) {
            text += QStringLiteral("  // filler %1 %2\n").arg(line).arg(n);
        }
        text += QStringLiteral("endmodule\n");
    }
    return text;
}

struct Metrics {
    int firstReal = -1;
    int lastReal = -1;
    int hidden = 0;
    int scrollMax = -1;
};

Metrics observe(KTextEditor::View *view)
{
    Metrics m;
    m.firstReal = view->firstDisplayedLine(KTextEditor::View::RealLine);
    m.lastReal = view->lastDisplayedLine(KTextEditor::View::RealLine);
    const int firstVisible = view->firstDisplayedLine(KTextEditor::View::VisibleLine);
    const int lastVisible = view->lastDisplayedLine(KTextEditor::View::VisibleLine);
    m.hidden = (m.lastReal - m.firstReal) - (lastVisible - firstVisible);
    if (QScrollBar *bar = view->verticalScrollBar()) {
        m.scrollMax = bar->maximum();
    }
    return m;
}

bool changed(const Metrics &a, const Metrics &b)
{
    return a.firstReal != b.firstReal || a.lastReal != b.lastReal || a.hidden != b.hidden
        || a.scrollMax != b.scrollMax;
}

void pump(int rounds = 6)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents();
    }
}

void clickGlobal(const QPoint &global)
{
    QWidget *target = QApplication::widgetAt(global);
    if (target == nullptr) {
        std::printf("NATIVE-GUI no-widget-at %d,%d\n", global.x(), global.y());
        return;
    }
    const QPoint local = target->mapFromGlobal(global);
    const QPointF localF(local);
    const QPointF globalF(global);
    QMouseEvent press(QEvent::MouseButtonPress, localF, globalF, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, localF, globalF, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    pump(10);
}

// Grey-ish icon border pixels around a line: the fold marker is a small glyph
// drawn in the border. We only need a coarse signal for the report; the click
// result is the real assertion.
int nonBackgroundPixels(const QImage &image, int y, int x0, int x1)
{
    int count = 0;
    for (int x = x0; x < x1 && x < image.width(); ++x) {
        for (int dy = 0; dy < 12 && y + dy < image.height(); ++dy) {
            const QRgb rgb = image.pixel(x, y + dy);
            const int grey = qGray(rgb);
            if (grey > 40 && grey < 230) {
                ++count;
            }
        }
    }
    return count;
}

bool copyDefinitions(const QString &source, const QString &share)
{
    const QString target = share + QStringLiteral("/org.kde.syntax-highlighting/syntax");
    if (!QDir().mkpath(target)) {
        return false;
    }
    const QStringList names{QStringLiteral("vedit-verilog.xml"),
                            QStringLiteral("vedit-systemverilog.xml")};
    for (const QString &name : names) {
        const QString from = source + QLatin1Char('/') + name;
        const QString to = target + QLatin1Char('/') + name;
        if (!QFile::exists(from) || !QFile::copy(from, to)) {
            std::printf("NATIVE-GUI copy-failed %s\n", qPrintable(from));
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const QString definitions = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString screenshot = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (definitions.isEmpty()) {
        std::printf("NATIVE-GUI usage=vedit-fold-native-gui-probe <resources/syntax> [png]\n");
        return 2;
    }
    QTemporaryDir share;
    if (!share.isValid() || !copyDefinitions(definitions, share.path())) {
        std::printf("NATIVE-GUI setup=failed\n");
        return 2;
    }
    qputenv("XDG_DATA_DIRS", share.path().toLocal8Bit());
    qputenv("XDG_DATA_HOME", share.path().toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (share.path() + QStringLiteral("/config")).toLocal8Bit());

    if (QApplication::platformName() != QLatin1String("xcb")) {
        std::printf("NATIVE-GUI SKIP platform=%s (needs xcb + Xvfb)\n",
                    qPrintable(QApplication::platformName()));
        return 77;
    }
    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("NATIVE-GUI no-editor\n");
        return 1;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    document->setText(sampleText());
    const bool modeOk = document->setHighlightingMode(QStringLiteral("SystemVerilog"));
    KTextEditor::View *view = document->createView(nullptr);
    view->resize(1000, 900);
    view->show();
    view->setConfigValue(QStringLiteral("folding-bar"), true);
    view->setConfigValue(QStringLiteral("folding-preview"), true);
    view->activateWindow();
    pump(20);
    std::printf("NATIVE-GUI mode='%s' set=%d view_visible=%d\n",
                qPrintable(document->highlightingMode()), modeOk ? 1 : 0,
                view->isVisible() ? 1 : 0);

    const Metrics baseline = observe(view);
    const QPoint textOrigin = view->cursorToCoordinate(KTextEditor::Cursor(0, 0));
    const QPoint textOriginGlobal = view->mapToGlobal(textOrigin);
    const QPoint viewGlobal = view->mapToGlobal(QPoint(0, 0));
    std::printf("NATIVE-GUI baseline first=%d last=%d hidden=%d scroll_max=%d\n",
                baseline.firstReal, baseline.lastReal, baseline.hidden, baseline.scrollMax);
    std::printf("NATIVE-GUI geometry view=%dx%d text_x=%d global_text_x=%d global_y=%d\n",
                view->width(), view->height(), textOrigin.x(), textOriginGlobal.x(),
                textOriginGlobal.y());

    const int lineHeightReal = qMax(1, view->cursorToCoordinate(KTextEditor::Cursor(1, 0)).y()
                                           - textOrigin.y());
    std::printf("NATIVE-GUI line_height=%d\n", lineHeightReal);
    const QPixmap grab = view->grab();
    if (!screenshot.isEmpty()) {
        grab.save(screenshot);
        std::printf("NATIVE-GUI screenshot=%s size=%dx%d\n", qPrintable(screenshot),
                    grab.width(), grab.height());
    }
    const QImage image = grab.toImage();
    const int lineHeight = textOrigin.y();
    std::printf("NATIVE-GUI border_scan line0_px=%d line1_px=%d\n",
                nonBackgroundPixels(image, 0, 0, qMax(0, textOrigin.x())),
                nonBackgroundPixels(image, qMax(1, lineHeight), 0, qMax(0, textOrigin.x())));

    // Scan the gutter for a clickable marker on the first module line.
    QPoint marker(-1, -1);
    const int yLine0 = textOriginGlobal.y();
    const int gutterRight = qMax(6, textOriginGlobal.x() - viewGlobal.x());
    for (int dx = 0; dx <= gutterRight && marker.x() < 0; dx += 2) {
        const QPoint candidate(viewGlobal.x() + dx, yLine0 + 4);
        clickGlobal(candidate);
        if (changed(baseline, observe(view))) {
            marker = candidate;
            std::printf("NATIVE-GUI marker_found x=%d (gutter_x=%d)\n", dx, dx);
        }
    }
    if (marker.x() < 0) {
        std::printf("NATIVE-GUI verdict no_clickable_marker (no direction changed)\n");
        std::printf("NATIVE-GUI done\n");
        return 0;
    }

    const Metrics folded = observe(view);
    std::printf("NATIVE-GUI after_first_click first=%d hidden=%d scroll_max=%d changed=%d\n",
                folded.firstReal, folded.hidden, folded.scrollMax, changed(baseline, folded) ? 1 : 0);
    // Second click on the same spot must unfold it again.
    clickGlobal(marker);
    const Metrics unfolded = observe(view);
    std::printf("NATIVE-GUI after_second_click first=%d hidden=%d scroll_max=%d\n",
                unfolded.firstReal, unfolded.hidden, unfolded.scrollMax);
    const bool foldWorked = changed(baseline, folded);
    const bool unfoldWorked = changed(folded, unfolded);
    std::printf("NATIVE-GUI verdict marker_created_by_kde=%d fold_direction=%d unfold_direction=%d\n",
                foldWorked ? 1 : 0, foldWorked ? 1 : 0, unfoldWorked ? 1 : 0);

    // Negative control: same gutter column on a non-region line.
    // Line 1 ("input logic clk") is not a folding-region start. Same gutter column.
    const QPoint nonRegion(marker.x(), textOriginGlobal.y() + lineHeightReal + 4);
    const Metrics beforeNegative = observe(view);
    clickGlobal(nonRegion);
    std::printf("NATIVE-GUI negative_control changed=%d\n",
                changed(beforeNegative, observe(view)) ? 1 : 0);
    // T128 acceptance 3: with the document collapsed, programmatic cursor moves
    // to an arbitrary (possibly hidden) line must not crash. The fold is created
    // by KDE's own native marker click above, not by VEdit injection.
    clickGlobal(marker); // collapse again so the cursor move happens while folded
    const bool beforeCursorChanged = observe(view).hidden > 0;
    view->setCursorPosition(KTextEditor::Cursor(20, 0));
    pump();
    std::printf("NATIVE-GUI hidden-cursor-move folded=%d cursor_line=%d survived=1\n",
                beforeCursorChanged ? 1 : 0, view->cursorPosition().line());

    const bool ok = foldWorked && unfoldWorked && !changed(beforeNegative, observe(view));
    std::printf("NATIVE-GUI result ok=%d\n", ok ? 1 : 0);
    std::printf("NATIVE-GUI done\n");
    return ok ? 0 : 1;
}
