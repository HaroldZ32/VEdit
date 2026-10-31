// C17 probe: can a host application overlay high-frequency incremental
// colouring ranges and semantic attributes on a KTextEditor document?
//
// Measured, not assumed:
//   (a) an overlay applied through the public host-side API is rendered
//       (screenshot + pixel analysis, plus the exact API names/paths printed),
//   (b) per-frame injection cost on a >= 100k line document while scrolling,
//       for three strategies, p50/p95 (full re-create per line / per token,
//       and an incremental pool that only touches lines entering/leaving the
//       viewport),
//   (c) attributes of an existing range are queryable (identity, range,
//       z-depth, document-anchored movement) and what is *not* queryable,
//   (d) the constraint that decides "conditional" vs "usable": the renderer's
//       hard cap on attribute ranges per line.
#include <KTextEditor/Attribute>
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QImage>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(const std::string &what, bool ok) {
  std::cout << "CHECK " << what << " " << (ok ? "PASS" : "FAIL") << "\n";
  if (!ok) {
    ++g_failures;
  }
}

void pump() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

QString makeSmallText() {
  return QStringLiteral("module\nendmodule\n");
}

QString makeBigText(int lines) {
  QString text;
  text.reserve(lines * 44);
  for (int i = 0; i < lines; ++i) {
    text += QStringLiteral("assign wire_%1 = a_%2 & b_%3; // row\n")
                .arg(i % 1000)
                .arg(i % 7)
                .arg(i % 5);
  }
  return text;
}

KTextEditor::Attribute::Ptr makeAttribute(const QColor &foreground, const QColor &background) {
  KTextEditor::Attribute::Ptr attribute(new KTextEditor::Attribute());
  attribute->setName(QStringLiteral("probe.overlay"));
  if (foreground.isValid()) {
    attribute->setForeground(QBrush(foreground));
  }
  if (background.isValid()) {
    attribute->setBackground(QBrush(background));
  }
  return attribute;
}

// ---------------------------------------------------------------------------
// pixel helpers
// ---------------------------------------------------------------------------
bool coloredPixel(QRgb pixel) {
  const int r = qRed(pixel);
  const int g = qGreen(pixel);
  const int b = qBlue(pixel);
  const int maxc = std::max({r, g, b});
  const int minc = std::min({r, g, b});
  return (maxc - minc) > 60 && maxc > 80;
}

long countNear(const QImage &image, QRgb target, int tolerance) {
  long count = 0;
  const int tr = qRed(target);
  const int tg = qGreen(target);
  const int tb = qBlue(target);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (std::abs(qRed(pixel) - tr) <= tolerance && std::abs(qGreen(pixel) - tg) <= tolerance &&
          std::abs(qBlue(pixel) - tb) <= tolerance) {
        ++count;
      }
    }
  }
  return count;
}

long countColored(const QImage &image) {
  long count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (coloredPixel(image.pixel(x, y))) {
        ++count;
      }
    }
  }
  return count;
}

// Dominant saturated colour of an image (quantised 32-level bins), used to
// learn the active theme's keyword colour from the baseline render.
QRgb dominantColor(const QImage &image) {
  std::map<int, long> histogram;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (!coloredPixel(pixel)) {
        continue;
      }
      const int key = (qRed(pixel) / 32) * 1024 + (qGreen(pixel) / 32) * 32 + (qBlue(pixel) / 32);
      ++histogram[key];
    }
  }
  if (histogram.empty()) {
    return qRgb(0, 0, 0);
  }
  auto best = std::max_element(histogram.begin(), histogram.end(),
                               [](const auto &a, const auto &b) { return a.second < b.second; });
  const int key = best->first;
  const int blue = key % 32;
  const int green = (key / 32) % 32;
  const int red = key / 1024;
  return qRgb(red * 32 + 16, green * 32 + 16, blue * 32 + 16);
}

// ---------------------------------------------------------------------------
// statistics
// ---------------------------------------------------------------------------
struct Stats {
  double p50 = 0;
  double p95 = 0;
  double min = 0;
  double max = 0;
  double mean = 0;
  std::size_t n = 0;
};

Stats summarize(std::vector<double> samples) {
  Stats stats;
  stats.n = samples.size();
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
  stats.min = samples.front();
  stats.max = samples.back();
  double sum = 0;
  for (double value : samples) {
    sum += value;
  }
  stats.mean = sum / static_cast<double>(samples.size());
  auto percentile = [&samples](double p) {
    const double index = p * static_cast<double>(samples.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(index));
    const std::size_t high = static_cast<std::size_t>(std::ceil(index));
    if (low == high) {
      return samples[low];
    }
    return samples[low] + (samples[high] - samples[low]) * (index - static_cast<double>(low));
  };
  stats.p50 = percentile(0.50);
  stats.p95 = percentile(0.95);
  return stats;
}

void report(const std::string &name, const Stats &stats) {
  std::cout << "MEASURE " << name << " n=" << stats.n << " p50_us=" << stats.p50
            << " p95_us=" << stats.p95 << " mean_us=" << stats.mean << " min_us=" << stats.min
            << " max_us=" << stats.max << "\n";
}

double usSince(const QElapsedTimer &timer) {
  return static_cast<double>(timer.nsecsElapsed()) / 1000.0;
}

// ---------------------------------------------------------------------------
// phases
// ---------------------------------------------------------------------------
struct Fixture {
  KTextEditor::Document *document = nullptr;
  KTextEditor::View *view = nullptr;
};

Fixture makeFixture(KTextEditor::Editor *editor, const QString &text, const QString &mode,
                    QSize size) {
  Fixture fixture;
  fixture.document = editor->createDocument(nullptr);
  fixture.document->setText(text);
  if (!mode.isEmpty()) {
    fixture.document->setHighlightingMode(mode);
  }
  fixture.view = fixture.document->createView(nullptr);
  fixture.view->resize(size);
  fixture.view->show();
  pump();
  return fixture;
}

void phaseOverlayRenderAndQuery(KTextEditor::Editor *editor, const QString &outDir,
                                const QString &mode) {
  std::cout << "\n== phase A: overlay rendering + attribute query ==\n";
  Fixture fixture = makeFixture(editor, makeSmallText(), mode, QSize(1000, 260));
  KTextEditor::Document *doc = fixture.document;
  KTextEditor::View *view = fixture.view;

  std::cout << "INFO highlightingModes=" << doc->highlightingModes().join(QLatin1Char(',')).toStdString()
            << "\n";
  std::cout << "INFO highlightingMode=" << doc->highlightingMode().toStdString() << "\n";
  std::cout << "INFO editor.commands=" << editor->commandList().size() << " commands="
            << editor->commandList().join(QLatin1Char(',')).left(400).toStdString() << "\n";

  const QImage baseline = view->grab().toImage();
  baseline.save(outDir + QStringLiteral("/10-baseline-verilog.png"));
  const QRgb baseColor = dominantColor(baseline);
  const long baselineColored = countColored(baseline);
  std::printf("INFO base_keyword_color=#%02x%02x%02x baseline_colored_px=%ld\n", qRed(baseColor),
              qGreen(baseColor), qBlue(baseColor), baselineColored);

  // Overlay line 0 ("module") with a magenta foreground and a green background.
  const QColor overlayForeground(255, 0, 255);
  const QColor overlayBackground(0, 255, 0);
  KTextEditor::Attribute::Ptr overlay = makeAttribute(overlayForeground, overlayBackground);
  overlay->setFontBold(true);
  KTextEditor::MovingRange *range =
      doc->newMovingRange(KTextEditor::Range(0, 0, 0, doc->lineLength(0)));
  range->setAttribute(overlay);
  range->setZDepth(-100.0);
  pump();

  const QImage overlaid = view->grab().toImage();
  overlaid.save(outDir + QStringLiteral("/11-overlay-rendered.png"));

  const long magenta = countNear(overlaid, qRgb(255, 0, 255), 60);
  const long green = countNear(overlaid, qRgb(0, 255, 0), 60);

  // Restrict the single-owner test to the rows the overlay actually covers:
  // rows of the overlaid render that contain the overlay foreground/background.
  int bandTop = overlaid.height();
  int bandBottom = -1;
  for (int y = 0; y < overlaid.height(); ++y) {
    long rowHits = 0;
    for (int x = 0; x < overlaid.width(); ++x) {
      const QRgb pixel = overlaid.pixel(x, y);
      if (std::abs(qRed(pixel) - 255) <= 60 && qGreen(pixel) <= 60 && std::abs(qBlue(pixel) - 255) <= 60) {
        ++rowHits;
      }
      if (qRed(pixel) <= 60 && std::abs(qGreen(pixel) - 255) <= 60 && qBlue(pixel) <= 60) {
        ++rowHits;
      }
      if (rowHits > 5) {
        break;
      }
    }
    if (rowHits > 5) {
      bandTop = std::min(bandTop, y);
      bandBottom = std::max(bandBottom, y);
    }
  }
  const QRect band(0, std::max(0, bandTop), overlaid.width(),
                   std::max(0, bandBottom - bandTop + 1));
  std::cout << "INFO overlay_band=(" << band.top() << "," << band.height() << "px)\n";

  long baselineBandColored = 0;
  long baselineBandNearBase = 0;
  long overlayBandBase = 0;
  long overlayBandMagenta = 0;
  for (int y = band.top(); y <= band.top() + band.height() - 1; ++y) {
    for (int x = 0; x < overlaid.width(); ++x) {
      if (coloredPixel(baseline.pixel(x, y))) {
        ++baselineBandColored;
      }
      if (std::abs(qRed(baseline.pixel(x, y)) - qRed(baseColor)) <= 48 &&
          std::abs(qGreen(baseline.pixel(x, y)) - qGreen(baseColor)) <= 48 &&
          std::abs(qBlue(baseline.pixel(x, y)) - qBlue(baseColor)) <= 48) {
        ++baselineBandNearBase;
      }
      if (std::abs(qRed(overlaid.pixel(x, y)) - qRed(baseColor)) <= 48 &&
          std::abs(qGreen(overlaid.pixel(x, y)) - qGreen(baseColor)) <= 48 &&
          std::abs(qBlue(overlaid.pixel(x, y)) - qBlue(baseColor)) <= 48) {
        ++overlayBandBase;
      }
      if (std::abs(qRed(overlaid.pixel(x, y)) - 255) <= 60 && qGreen(overlaid.pixel(x, y)) <= 60 &&
          std::abs(qBlue(overlaid.pixel(x, y)) - 255) <= 60) {
        ++overlayBandMagenta;
      }
    }
  }
  std::cout << "MEASURE overlay_foreground_px=" << magenta << "\n";
  std::cout << "MEASURE overlay_background_px=" << green << "\n";
  std::cout << "MEASURE baseline_band_colored_px=" << baselineBandColored << "\n";
  std::cout << "MEASURE baseline_band_near_base_color_px=" << baselineBandNearBase << "\n";
  std::cout << "MEASURE overlay_band_base_keyword_px=" << overlayBandBase << "\n";
  std::cout << "MEASURE overlay_band_overlay_foreground_px=" << overlayBandMagenta << "\n";
  check("overlay.foreground.rendered", magenta > 0);
  check("overlay.background.rendered", green > 0);
  check("overlay.foreground.inside.band", overlayBandMagenta > 0);
  // Single-owner criterion (A1g): the base keyword colour inside the overlay
  // band must be gone, i.e. the overlay replaces it rather than blending.
  check("overlay.replaces.base.foreground",
        baselineBandColored > 0 && overlayBandBase < std::max<long>(1, baselineBandColored / 5));

  // ---- (c) query the attributes that already exist ----
  check("query.range.attribute.identity", range->attribute().get() == overlay.get());
  check("query.range.start", range->start().line() == 0 && range->start().column() == 0);
  check("query.range.end.column", range->end().column() == doc->lineLength(0));
  check("query.range.zdepth", std::abs(range->zDepth() - (-100.0)) < 1e-9);
  check("query.range.attribute.name",
        range->attribute()->name() == QStringLiteral("probe.overlay"));

  // A1h: removing the overlay must give the base syntax colour back, with no
  // residual overlay colour anywhere in the band.
  delete range;
  pump();
  const QImage restored = view->grab().toImage();
  restored.save(outDir + QStringLiteral("/12b-overlay-removed.png"));
  long restoredBandBase = 0;
  long restoredBandMagenta = 0;
  for (int y = band.top(); y <= band.top() + band.height() - 1; ++y) {
    for (int x = 0; x < restored.width(); ++x) {
      const QRgb pixel = restored.pixel(x, y);
      if (std::abs(qRed(pixel) - qRed(baseColor)) <= 48 &&
          std::abs(qGreen(pixel) - qGreen(baseColor)) <= 48 &&
          std::abs(qBlue(pixel) - qBlue(baseColor)) <= 48) {
        ++restoredBandBase;
      }
      if (std::abs(qRed(pixel) - 255) <= 60 && qGreen(pixel) <= 60 &&
          std::abs(qBlue(pixel) - 255) <= 60) {
        ++restoredBandMagenta;
      }
    }
  }
  std::cout << "MEASURE restored_band_base_keyword_px=" << restoredBandBase << "\n";
  std::cout << "MEASURE restored_band_overlay_foreground_px=" << restoredBandMagenta << "\n";
  {
    const QImage bandImage = restored.copy(band);
    const QRgb restoredDominant = dominantColor(bandImage);
    std::printf("INFO restored_band_dominant_color=#%02x%02x%02x saturated_px=%ld\n",
                qRed(restoredDominant), qGreen(restoredDominant), qBlue(restoredDominant),
                countColored(bandImage));
  }
  check("overlay.removal.restores.base.foreground",
        baselineBandNearBase > 0 && restoredBandBase >= (baselineBandNearBase * 4) / 5);
  check("overlay.removal.leaves.no.overlay.colour", restoredBandMagenta == 0);

  // Document-anchored movement: insert a line above, the range must follow.
  KTextEditor::MovingRange *moving =
      doc->newMovingRange(KTextEditor::Range(0, 0, 0, doc->lineLength(0)));
  moving->setAttribute(overlay);
  moving->setZDepth(-100.0);
  pump();
  doc->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("// header\n"));
  pump();
  check("query.range.moves.with.document", moving->start().line() == 1);
  check("query.range.attribute.survives.edit", moving->attribute().get() == overlay.get());
  check("query.range.text.after.edit",
        doc->text(KTextEditor::Range(1, 0, 1, 6)) == QStringLiteral("module"));
  view->grab().save(outDir + QStringLiteral("/12-overlay-after-edit.png"));
  delete moving;
  pump();
}

void phaseSemanticAttribute(KTextEditor::Editor *editor, const QString &outDir) {
  std::cout << "\n== phase A2: semantic attributes (theme-resolved vs Theme::TextStyle hint) ==\n";

  KSyntaxHighlighting::Repository repository;
  const KSyntaxHighlighting::Definition definition = repository.definitionForName(QStringLiteral("Verilog"));
  std::cout << "INFO embedded_definitions=" << repository.definitions().size()
            << " embedded_themes=" << repository.themes().size()
            << " verilog_loaded=" << (definition.isValid() ? "yes" : "no") << "\n";

  Fixture fixture = makeFixture(editor, QStringLiteral("plainword\n"), QStringLiteral("Verilog"),
                                QSize(800, 200));
  KTextEditor::Document *doc = fixture.document;
  KTextEditor::View *view = fixture.view;

  auto coloredInTextBand = [&view]() {
    const QImage image = view->grab().toImage();
    const int top = view->textAreaRect().top();
    long colored = 0;
    for (int y = top; y < std::min(image.height(), top + 40); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (coloredPixel(image.pixel(x, y))) {
          ++colored;
        }
      }
    }
    return colored;
  };

  const long beforeColored = coloredInTextBand();

  // (i) the Theme::TextStyle hint alone, no explicit colour.
  KTextEditor::Attribute::Ptr hint(new KTextEditor::Attribute());
  hint->setName(QStringLiteral("probe.semantic.hint"));
  hint->setDefaultStyle(KSyntaxHighlighting::Theme::TextStyle::Keyword);
  KTextEditor::MovingRange *hintRange = doc->newMovingRange(KTextEditor::Range(0, 0, 0, 9));
  hintRange->setAttribute(hint);
  pump();
  const long hintColored = coloredInTextBand();
  view->grab().save(outDir + QStringLiteral("/13a-semantic-defaultstyle-hint.png"));
  delete hintRange;
  pump();

  // (ii) the app resolves the active theme itself and passes explicit colours.
  const KSyntaxHighlighting::Theme theme =
      repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
  const QColor normalColor(theme.textColor(KSyntaxHighlighting::Theme::TextStyle::Normal));
  const std::vector<KSyntaxHighlighting::Theme::TextStyle> candidates = {
      KSyntaxHighlighting::Theme::TextStyle::Keyword,
      KSyntaxHighlighting::Theme::TextStyle::Comment,
      KSyntaxHighlighting::Theme::TextStyle::Function,
      KSyntaxHighlighting::Theme::TextStyle::DecVal,
      KSyntaxHighlighting::Theme::TextStyle::String,
  };
  KSyntaxHighlighting::Theme::TextStyle chosen = KSyntaxHighlighting::Theme::TextStyle::Keyword;
  int bestDistance = -1;
  for (KSyntaxHighlighting::Theme::TextStyle candidate : candidates) {
    const QColor color(theme.textColor(candidate));
    const int distance = std::abs(color.red() - normalColor.red()) +
                         std::abs(color.green() - normalColor.green()) +
                         std::abs(color.blue() - normalColor.blue());
    std::printf("INFO theme_style_distance style=%d color=#%02x%02x%02x distance=%d\n",
                static_cast<int>(candidate), color.red(), color.green(), color.blue(), distance);
    if (distance > bestDistance) {
      bestDistance = distance;
      chosen = candidate;
    }
  }
  const QColor keywordColor(theme.textColor(chosen));
  std::cout << "INFO chosen_text_style=" << static_cast<int>(chosen)
            << " distance_from_normal=" << bestDistance << "\n";
  KTextEditor::Attribute::Ptr resolved = makeAttribute(keywordColor, QColor());
  resolved->setName(QStringLiteral("probe.semantic.resolved"));
  resolved->setFontBold(theme.isBold(chosen));
  KTextEditor::MovingRange *resolvedRange = doc->newMovingRange(KTextEditor::Range(0, 0, 0, 9));
  resolvedRange->setAttribute(resolved);
  pump();
  const QImage resolvedImage = view->grab().toImage();
  resolvedImage.save(outDir + QStringLiteral("/13b-semantic-theme-resolved.png"));
  const long resolvedColored = coloredInTextBand();
  const long resolvedExact =
      countNear(resolvedImage, keywordColor.rgb(), 40);

  std::cout << "MEASURE semantic_text_band_colored_px_baseline=" << beforeColored << "\n";
  std::cout << "MEASURE semantic_text_band_colored_px_defaultstyle_hint=" << hintColored << "\n";
  std::cout << "MEASURE semantic_text_band_colored_px_theme_resolved=" << resolvedColored << "\n";
  std::printf("INFO theme_keyword_color=#%02x%02x%02x\n", keywordColor.red(), keywordColor.green(),
              keywordColor.blue());
  std::cout << "MEASURE semantic_theme_resolved_exact_color_px=" << resolvedExact << "\n";
  std::cout << "INFO defaultstyle_hint_alone_effective="
            << ((hintColored > beforeColored + 50) ? "yes" : "no") << "\n";
  check("semantic.defaultstyle_hint.queryable",
        hint->defaultStyle() == KSyntaxHighlighting::Theme::TextStyle::Keyword);
  check("semantic.theme_resolved_colors.range", resolvedExact > 20);
  delete resolvedRange;
  pump();
}

void phaseAttributeRangeCap(KTextEditor::Editor *editor, const QString &outDir) {
  std::cout << "\n== phase A3: attribute range cap per line ==\n";
  QString longLine;
  const int tokens = 1400;
  for (int i = 0; i < tokens; ++i) {
    longLine += QStringLiteral("tok%1 ").arg(i % 10);
  }
  Fixture fixture = makeFixture(editor, longLine + QStringLiteral("\n"), QString(), QSize(1200, 200));
  KTextEditor::Document *doc = fixture.document;
  KTextEditor::View *view = fixture.view;

  const QColor marker(255, 0, 255);
  std::vector<KTextEditor::MovingRange *> ranges;

  auto applyCount = [&](int count) {
    for (KTextEditor::MovingRange *range : ranges) {
      delete range;
    }
    ranges.clear();
    int column = 0;
    for (int i = 0; i < count; ++i) {
      const int length = 4;  // "tokN"
      KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(0, column, 0, column + length));
      range->setAttribute(makeAttribute(QColor(), marker));
      ranges.push_back(range);
      column += 5;
    }
    pump();
    return countNear(view->grab().toImage(), qRgb(255, 0, 255), 60);
  };

  const long at1000 = applyCount(1000);
  view->grab().save(outDir + QStringLiteral("/14-1000-ranges-per-line.png"));
  const long at1100 = applyCount(1100);
  view->grab().save(outDir + QStringLiteral("/15-1100-ranges-per-line.png"));
  std::cout << "MEASURE cap_test_1000_ranges_marker_px=" << at1000 << "\n";
  std::cout << "MEASURE cap_test_1100_ranges_marker_px=" << at1100 << "\n";
  check("cap.ranges.render.below.limit", at1000 > 0);
  check("cap.ranges.silently.drop.above.limit", at1100 == 0);
  for (KTextEditor::MovingRange *range : ranges) {
    delete range;
  }
  ranges.clear();
  pump();
}

struct ScrollProbe {
  int moved_steps = 0;
  long visible_lines_sum = 0;
  int steps = 0;
};

ScrollProbe scrollSeries(KTextEditor::View *view, int firstLine, int steps, int stepLines) {
  ScrollProbe probe;
  for (int step = 0; step < steps; ++step) {
    const int line = firstLine + step * stepLines;
    view->setCursorPosition(KTextEditor::Cursor(line, 0));
    pump();
    int first = view->firstDisplayedLine(KTextEditor::View::RealLine);
    if (std::abs(first - line) > 10) {
      view->verticalScrollBar()->setValue(line);
      pump();
      first = view->firstDisplayedLine(KTextEditor::View::RealLine);
    }
    if (std::abs(first - line) <= 10) {
      ++probe.moved_steps;
    }
    const int last = view->lastDisplayedLine(KTextEditor::View::RealLine);
    probe.visible_lines_sum += std::max(0, last - first + 1);
    ++probe.steps;
  }
  return probe;
}

void phasePerformance(KTextEditor::Editor *editor, const QString &outDir, int lines, int steps,
                     int stepLines, const QString &mode) {
  std::cout << "\n== phase B: per-frame injection cost, doc lines=" << lines << " mode="
            << (mode.isEmpty() ? "None" : mode.toStdString()) << " steps=" << steps
            << " step_lines=" << stepLines << " ==\n";
  const QString big = makeBigText(lines);
  QElapsedTimer loadTimer;
  loadTimer.start();
  Fixture fixture = makeFixture(editor, big, mode, QSize(1400, 900));
  const double loadMs = static_cast<double>(loadTimer.elapsed());
  std::cout << "MEASURE document_load_ms=" << loadMs << " lines=" << fixture.document->lines() << "\n";
  check("bigdoc.at.least.100k.lines", fixture.document->lines() >= 100000);

  KTextEditor::Document *doc = fixture.document;
  KTextEditor::View *view = fixture.view;
  KTextEditor::Attribute::Ptr attribute = makeAttribute(QColor(200, 0, 0), QColor(255, 240, 170));

  // Baseline render cost without any overlay range.
  std::vector<double> baselineGrab;
  for (int step = 0; step < steps; ++step) {
    const int line = 5 + step * stepLines;
    view->setCursorPosition(KTextEditor::Cursor(line, 0));
    pump();
    QElapsedTimer timer;
    timer.start();
    const QImage image = view->grab().toImage();
    baselineGrab.push_back(usSince(timer));
    (void)image;
  }
  report("grab_baseline_no_overlay", summarize(baselineGrab));

  // Strategy 1: full re-create, one range per visible line.
  {
    std::vector<KTextEditor::MovingRange *> ranges;
    std::vector<double> inject;
    std::vector<double> grab;
    ScrollProbe scroll;
    for (int step = 0; step < steps; ++step) {
      const int line = 5 + step * stepLines;
      view->setCursorPosition(KTextEditor::Cursor(line, 0));
      pump();
      int first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      if (std::abs(first - line) > 10) {
        view->verticalScrollBar()->setValue(line);
        pump();
        first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      }
      const int last = view->lastDisplayedLine(KTextEditor::View::RealLine);
      ++scroll.steps;
      if (std::abs(first - line) <= 10) {
        ++scroll.moved_steps;
      }
      scroll.visible_lines_sum += std::max(0, last - first + 1);

      QElapsedTimer timer;
      timer.start();
      for (KTextEditor::MovingRange *range : ranges) {
        delete range;
      }
      ranges.clear();
      for (int l = std::max(0, first); l <= last && l < doc->lines(); ++l) {
        const int length = doc->lineLength(l);
        if (length <= 0) {
          continue;
        }
        KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(l, 0, l, length));
        range->setAttribute(attribute);
        ranges.push_back(range);
      }
      inject.push_back(usSince(timer));

      timer.start();
      const QImage image = view->grab().toImage();
      grab.push_back(usSince(timer));
      (void)image;
    }
    std::cout << "INFO strategy=full_recreate_per_line visible_lines_avg="
              << (scroll.steps ? static_cast<double>(scroll.visible_lines_sum) / scroll.steps : 0)
              << " scroll_moved_steps=" << scroll.moved_steps << "/" << scroll.steps << "\n";
    report("full_recreate_per_line.inject", summarize(inject));
    report("full_recreate_per_line.inject_plus_grab",
           summarize([&] {
             std::vector<double> total;
             for (std::size_t i = 0; i < inject.size(); ++i) {
               total.push_back(inject[i] + grab[i]);
             }
             return total;
           }()));
    report("full_recreate_per_line.grab_with_overlay", summarize(grab));
    for (KTextEditor::MovingRange *range : ranges) {
      delete range;
    }
    pump();
  }

  // Strategy 2: full re-create, four token ranges per visible line.
  {
    std::vector<KTextEditor::MovingRange *> ranges;
    std::vector<double> inject;
    std::vector<double> grab;
    for (int step = 0; step < steps; ++step) {
      const int line = 5 + step * stepLines;
      view->setCursorPosition(KTextEditor::Cursor(line, 0));
      pump();
      int first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      if (std::abs(first - line) > 10) {
        view->verticalScrollBar()->setValue(line);
        pump();
        first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      }
      const int last = view->lastDisplayedLine(KTextEditor::View::RealLine);

      QElapsedTimer timer;
      timer.start();
      for (KTextEditor::MovingRange *range : ranges) {
        delete range;
      }
      ranges.clear();
      for (int l = std::max(0, first); l <= last && l < doc->lines(); ++l) {
        const int length = doc->lineLength(l);
        if (length < 4) {
          continue;
        }
        for (int token = 0; token < 4; ++token) {
          const int start = token * (length / 4);
          const int end = (token == 3) ? length : (token + 1) * (length / 4);
          if (end <= start) {
            continue;
          }
          KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(l, start, l, end));
          range->setAttribute(attribute);
          ranges.push_back(range);
        }
      }
      inject.push_back(usSince(timer));

      timer.start();
      const QImage image = view->grab().toImage();
      grab.push_back(usSince(timer));
      (void)image;
    }
    report("full_recreate_per_token.inject", summarize(inject));
    report("full_recreate_per_token.grab_with_overlay", summarize(grab));
    for (KTextEditor::MovingRange *range : ranges) {
      delete range;
    }
    pump();
  }

  // Strategy 3: incremental pool, only lines leaving/entering the viewport.
  {
    std::map<int, KTextEditor::MovingRange *> pool;
    std::vector<double> inject;
    std::vector<double> grab;
    for (int step = 0; step < steps; ++step) {
      const int line = 5 + step * stepLines;
      view->setCursorPosition(KTextEditor::Cursor(line, 0));
      pump();
      int first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      if (std::abs(first - line) > 10) {
        view->verticalScrollBar()->setValue(line);
        pump();
        first = view->firstDisplayedLine(KTextEditor::View::RealLine);
      }
      const int last = view->lastDisplayedLine(KTextEditor::View::RealLine);

      QElapsedTimer timer;
      timer.start();
      for (auto it = pool.begin(); it != pool.end();) {
        if (it->first < first || it->first > last) {
          delete it->second;
          it = pool.erase(it);
        } else {
          ++it;
        }
      }
      for (int l = std::max(0, first); l <= last && l < doc->lines(); ++l) {
        if (pool.count(l) != 0) {
          continue;
        }
        const int length = doc->lineLength(l);
        if (length <= 0) {
          continue;
        }
        KTextEditor::MovingRange *range = doc->newMovingRange(KTextEditor::Range(l, 0, l, length));
        range->setAttribute(attribute);
        pool.emplace(l, range);
      }
      inject.push_back(usSince(timer));

      timer.start();
      const QImage image = view->grab().toImage();
      grab.push_back(usSince(timer));
      (void)image;
    }
    std::cout << "INFO strategy=incremental_pool pool_size=" << pool.size() << "\n";
    report("incremental_pool.inject", summarize(inject));
    report("incremental_pool.grab_with_overlay", summarize(grab));
    for (auto &entry : pool) {
      delete entry.second;
    }
    pool.clear();
    pump();
  }

  view->grab().save(outDir + QStringLiteral("/20-bigdoc-overlay-%1.png")
                        .arg(mode.isEmpty() ? QStringLiteral("plain") : mode));
}

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  QString outDir = QStringLiteral(".");
  int lines = 120000;
  int steps = 200;
  int stepLines = 3;
  QString mode;
  QString perfMode;
  bool perfOnly = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QLatin1String("--out") && i + 1 < argc) {
      outDir = QString::fromLocal8Bit(argv[++i]);
    } else if (arg == QLatin1String("--lines") && i + 1 < argc) {
      lines = QString::fromLocal8Bit(argv[++i]).toInt();
    } else if (arg == QLatin1String("--steps") && i + 1 < argc) {
      steps = QString::fromLocal8Bit(argv[++i]).toInt();
    } else if (arg == QLatin1String("--step-lines") && i + 1 < argc) {
      stepLines = QString::fromLocal8Bit(argv[++i]).toInt();
    } else if (arg == QLatin1String("--mode") && i + 1 < argc) {
      mode = QString::fromLocal8Bit(argv[++i]);
    } else if (arg == QLatin1String("--perf-mode") && i + 1 < argc) {
      perfMode = QString::fromLocal8Bit(argv[++i]);
      if (perfMode == QLatin1String("none")) {
        perfMode.clear();
      }
    } else if (arg == QLatin1String("--perf-only")) {
      perfOnly = true;
    } else if (arg == QLatin1String("--help")) {
      std::cout << "usage: c17-overlay [--out DIR] [--lines N] [--steps N] [--step-lines N] "
                   "[--mode NAME]\n";
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg.toStdString() << "\n";
      return 2;
    }
  }

  std::cout << "INFO qt=" << qVersion() << "\n";
  std::cout << "INFO platform=" << QApplication::platformName().toStdString() << "\n";

  KTextEditor::Editor *editor = KTextEditor::Editor::instance();
  if (editor == nullptr) {
    std::cout << "CHECK editor.instance.available FAIL\n";
    std::cout << "RESULT=FAIL failures=1\n";
    return 1;
  }
  check("editor.instance.available", editor != nullptr);

  QString resolvedMode = mode;
  if (resolvedMode.isEmpty()) {
    // Prefer the probe's Verilog definition when the data directory provides
    // one; otherwise stay on Plain ("None") highlighting.
    for (const QString &candidate : QStringList{QStringLiteral("Verilog")}) {
      if (editor->createDocument(nullptr)->highlightingModes().contains(candidate)) {
        resolvedMode = candidate;
        break;
      }
    }
  }

  if (!perfOnly) {
    phaseOverlayRenderAndQuery(editor, outDir, resolvedMode);
    phaseSemanticAttribute(editor, outDir);
    phaseAttributeRangeCap(editor, outDir);
  }
  phasePerformance(editor, outDir, lines, steps, stepLines, perfMode);

  std::cout << "\nRESULT=" << (g_failures == 0 ? "PASS" : "FAIL") << " failures=" << g_failures
            << "\n";
  return g_failures == 0 ? 0 : 1;
}
