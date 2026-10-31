// fold-interface probe (T9 / Open Q3): can KTextEditor folding be driven by an
// external range?
//
// The public KTextEditor API (installed headers) exposes no folding model:
// folding lives in KatePart's internal Kate::TextFolding and is populated from
// KSyntaxHighlighting fold regions. What *is* public is the editor command
// interface: `KTextEditor::Editor::commands()` returns KTextEditor::Command
// objects, and KatePart registers "fold", "tfold" and "unfold". This probe
// measures what that interface actually allows:
//
//   * can a range chosen by the caller (not by the syntax definition) be folded?
//   * is the result observable / restorable / updatable / queryable?
//   * does folding survive edits, overlapping ranges and out-of-range input?
//   * how expensive is one injection?
#include <KTextEditor/Command>
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <KTextEditor/Attribute>
#include <KTextEditor/MovingRange>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QScrollBar>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
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

QString verilogText(int modules) {
  QString text;
  for (int m = 0; m < modules; ++m) {
    text += QStringLiteral("module m%1 (input clk, output q);\n").arg(m);
    text += QStringLiteral("  wire t;\n");
    text += QStringLiteral("  reg state;\n");
    text += QStringLiteral("  assign t = clk & state;\n");
    text += QStringLiteral("  always @(posedge clk) begin\n");
    text += QStringLiteral("    assign q = t | state;\n");
    text += QStringLiteral("    assign state = clk;\n");
    text += QStringLiteral("  end\n");
    text += QStringLiteral("endmodule\n");
    text += QStringLiteral("\n");
  }
  return text;
}

struct Snapshot {
  int firstReal = -1;
  int lastReal = -1;
  int firstVisible = -1;
  int lastVisible = -1;
  int scrollMin = -1;
  int scrollMax = -1;
  int scrollValue = -1;
  int cursorLine = -1;
};

Snapshot takeSnapshot(KTextEditor::View *view) {
  Snapshot snapshot;
  snapshot.firstReal = view->firstDisplayedLine(KTextEditor::View::RealLine);
  snapshot.lastReal = view->lastDisplayedLine(KTextEditor::View::RealLine);
  snapshot.firstVisible = view->firstDisplayedLine(KTextEditor::View::VisibleLine);
  snapshot.lastVisible = view->lastDisplayedLine(KTextEditor::View::VisibleLine);
  QScrollBar *scroll = view->verticalScrollBar();
  snapshot.scrollMin = scroll->minimum();
  snapshot.scrollMax = scroll->maximum();
  snapshot.scrollValue = scroll->value();
  snapshot.cursorLine = view->cursorPosition().line();
  return snapshot;
}

void printSnapshot(const std::string &label, const Snapshot &snapshot) {
  std::cout << "SNAPSHOT " << label << " first_real=" << snapshot.firstReal
            << " last_real=" << snapshot.lastReal << " first_visible=" << snapshot.firstVisible
            << " last_visible=" << snapshot.lastVisible << " scroll_min=" << snapshot.scrollMin
            << " scroll_max=" << snapshot.scrollMax << " scroll_value=" << snapshot.scrollValue
            << " cursor_line=" << snapshot.cursorLine << "\n";
}

// "How many lines are hidden" as observable through the public API: the real
// line indices spanned by the viewport are larger than the visible count.
int hiddenInViewport(const Snapshot &snapshot) {
  return (snapshot.lastReal - snapshot.firstReal) - (snapshot.lastVisible - snapshot.firstVisible);
}

struct Execution {
  bool ok = false;
  QString message;
  double us = 0;
};

Execution execCommand(KTextEditor::Command *command, KTextEditor::View *view, const QString &cmd,
                      const KTextEditor::Range &range = KTextEditor::Range::invalid()) {
  Execution execution;
  QString message;
  QElapsedTimer timer;
  timer.start();
  execution.ok = command->exec(view, cmd, message, range);
  execution.us = static_cast<double>(timer.nsecsElapsed()) / 1000.0;
  execution.message = message;
  return execution;
}

struct Stats {
  double p50 = 0;
  double p95 = 0;
  double mean = 0;
  double max = 0;
  std::size_t n = 0;
};

Stats summarize(std::vector<double> samples) {
  Stats stats;
  stats.n = samples.size();
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
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
  double sum = 0;
  for (double value : samples) {
    sum += value;
  }
  stats.mean = sum / static_cast<double>(samples.size());
  stats.max = samples.back();
  return stats;
}

void report(const std::string &name, const Stats &stats) {
  std::cout << "MEASURE " << name << " n=" << stats.n << " p50_us=" << stats.p50
            << " p95_us=" << stats.p95 << " mean_us=" << stats.mean << " max_us=" << stats.max
            << "\n";
}

struct Fixture {
  KTextEditor::Document *document = nullptr;
  KTextEditor::View *view = nullptr;
};

Fixture makeFixture(KTextEditor::Editor *editor, const QString &text, const QString &mode) {
  Fixture fixture;
  fixture.document = editor->createDocument(nullptr);
  fixture.document->setText(text);
  if (!mode.isEmpty()) {
    fixture.document->setHighlightingMode(mode);
  }
  fixture.view = fixture.document->createView(nullptr);
  fixture.view->resize(1200, 780);
  fixture.view->show();
  pump();
  return fixture;
}

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QString outDir = QStringLiteral(".");
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QLatin1String("--out") && i + 1 < argc) {
      outDir = QString::fromLocal8Bit(argv[++i]);
    } else if (arg == QLatin1String("--help")) {
      std::cout << "usage: fold-probe [--out DIR]\n";
      return 0;
    }
  }

  std::cout << "INFO qt=" << qVersion() << " platform=" << QApplication::platformName().toStdString()
            << "\n";

  KTextEditor::Editor *editor = KTextEditor::Editor::instance();
  if (editor == nullptr) {
    std::cout << "CHECK editor.instance.available FAIL\nRESULT=FAIL failures=1\n";
    return 1;
  }
  check("editor.instance.available", true);

  // ---- (a) API inventory through the public editor object ----
  QStringList foldCommands;
  for (KTextEditor::Command *command : editor->commands()) {
    for (const QString &name : command->cmds()) {
      if (name.contains(QLatin1String("fold"), Qt::CaseInsensitive)) {
        foldCommands << name;
      }
    }
  }
  foldCommands.sort();
  std::cout << "INFO fold_commands=" << foldCommands.join(QLatin1Char(',')).toStdString() << "\n";
  KTextEditor::Command *fold = editor->queryCommand(QStringLiteral("fold"));
  KTextEditor::Command *unfold = editor->queryCommand(QStringLiteral("unfold"));
  check("command.fold.registered", fold != nullptr);
  check("command.unfold.registered", unfold != nullptr);
  if (fold == nullptr || unfold == nullptr) {
    std::cout << "RESULT=FAIL failures=" << (g_failures + 1) << "\n";
    return 1;
  }
  check("command.fold.supportsRange", fold->supportsRange(QStringLiteral("fold")));
  const bool unfoldSupportsRange = unfold->supportsRange(QStringLiteral("unfold"));
  std::cout << "INFO command_unfold_supports_range=" << unfoldSupportsRange
            << " (unfold is cursor-based by design)\n";
  check("command.unfold.is.cursor.based", !unfoldSupportsRange);
  std::cout << "INFO fold_command_registrations="
            << [&editor] {
                 int n = 0;
                 for (KTextEditor::Command *command : editor->commands()) {
                   n += command->cmds().count(QStringLiteral("fold"));
                 }
                 return n;
               }()
            << " (one registration per live view: commands are per-view objects)\n";

  // ---- (b1) fold an explicit, caller-chosen range (not a definition region) ----
  std::cout << "\n== B1: explicit external range, syntax definition present ==\n";
  Fixture fixture = makeFixture(editor, verilogText(45), QStringLiteral("Verilog"));
  KTextEditor::View *view = fixture.view;
  const Snapshot baseline = takeSnapshot(view);
  printSnapshot("baseline_verilog", baseline);

  const KTextEditor::Range targetRange(2, 0, 10, 0);
  const Execution foldRun = execCommand(fold, view, QStringLiteral("fold"), targetRange);
  pump();
  const Snapshot folded = takeSnapshot(view);
  printSnapshot("after_fold_2_10", folded);
  std::cout << "INFO fold_external_range ok=" << foldRun.ok
            << " message=" << foldRun.message.toStdString() << " us=" << foldRun.us << "\n";
  check("fold.external.range.returned.true", foldRun.ok);
  check("fold.external.range.observable", hiddenInViewport(folded) > hiddenInViewport(baseline));
  view->grab().save(outDir + QStringLiteral("/10-fold-external-range.png"));

  // ---- (b2) unfold restores it ----
  view->setCursorPosition(KTextEditor::Cursor(2, 0));
  pump();
  const Execution unfoldRun = execCommand(unfold, view, QStringLiteral("unfold"));
  pump();
  const Snapshot unfolded = takeSnapshot(view);
  printSnapshot("after_unfold_line_2", unfolded);
  std::cout << "INFO unfold ok=" << unfoldRun.ok << " message=" << unfoldRun.message.toStdString()
            << " us=" << unfoldRun.us << "\n";
  check("unfold.returned.true", unfoldRun.ok);
  check("unfold.restores.visibility", hiddenInViewport(unfolded) == hiddenInViewport(baseline));

  // ---- (b3) arbitrary range with NO syntax definition at all ----
  std::cout << "\n== B2: explicit external range, no syntax definition (mode None) ==\n";
  Fixture plain = makeFixture(editor, verilogText(45), QString());
  const Snapshot plainBaseline = takeSnapshot(plain.view);
  printSnapshot("baseline_plain", plainBaseline);
  const Execution plainFold = execCommand(fold, plain.view, QStringLiteral("fold"),
                                          KTextEditor::Range(4, 0, 12, 0));
  pump();
  const Snapshot plainFolded = takeSnapshot(plain.view);
  printSnapshot("after_fold_4_12_plain", plainFolded);
  std::cout << "INFO fold_without_definition ok=" << plainFold.ok << "\n";
  check("fold.works.without.syntax.definition", plainFold.ok &&
                                                   hiddenInViewport(plainFolded) >
                                                       hiddenInViewport(plainBaseline));

  // ---- (b4) edits around / inside a folded range ----
  std::cout << "\n== B3: edits vs a folded range ==\n";
  Snapshot beforeEdit = takeSnapshot(plain.view);
  plain.document->insertText(KTextEditor::Cursor(0, 0), QStringLiteral("// header\n"));
  pump();
  Snapshot afterAbove = takeSnapshot(plain.view);
  printSnapshot("after_insert_above", afterAbove);
  std::cout << "INFO edit_above_fold hidden_before=" << hiddenInViewport(beforeEdit)
            << " hidden_after=" << hiddenInViewport(afterAbove) << "\n";

  plain.document->insertText(KTextEditor::Cursor(8, 0), QStringLiteral("// inside fold\n"));
  pump();
  Snapshot afterInside = takeSnapshot(plain.view);
  printSnapshot("after_insert_inside", afterInside);
  std::cout << "INFO edit_inside_fold hidden_after=" << hiddenInViewport(afterInside) << "\n";
  check("edit.inside.fold.no.crash", true);
  // Measured contract: editing inside a folded range silently unfolds it.
  check("edit.inside.fold.unfolds.range", hiddenInViewport(afterInside) == 0);

  // ---- (b5) overlapping and invalid ranges ----
  std::cout << "\n== B4: overlapping / invalid / out-of-range input ==\n";
  const Execution overlapA = execCommand(fold, plain.view, QStringLiteral("fold"),
                                         KTextEditor::Range(20, 0, 30, 0));
  const Execution overlapB = execCommand(fold, plain.view, QStringLiteral("fold"),
                                         KTextEditor::Range(22, 0, 25, 0));
  pump();
  std::cout << "INFO overlapping_folds a=" << overlapA.ok << " b=" << overlapB.ok << "\n";
  check("overlapping.folds.no.crash", true);

  const Execution emptyRange = execCommand(fold, plain.view, QStringLiteral("fold"),
                                           KTextEditor::Range(40, 0, 40, 0));
  const Execution invertedRange = execCommand(fold, plain.view, QStringLiteral("fold"),
                                              KTextEditor::Range(50, 0, 45, 0));
  const Execution beyondEnd = execCommand(fold, plain.view, QStringLiteral("fold"),
                                          KTextEditor::Range(10, 0, 5000, 0));
  pump();
  std::cout << "INFO empty_range_ok=" << emptyRange.ok << " inverted_range_ok=" << invertedRange.ok
            << " beyond_end_ok=" << beyondEnd.ok << "\n";

  // ---- (b6) injection cost of one fold command on a large document ----
  std::cout << "\n== B5: injection cost, large document ==\n";
  Fixture big = makeFixture(editor, verilogText(400), QStringLiteral("Verilog"));
  std::cout << "INFO big_doc_lines=" << big.document->lines() << "\n";
  check("big_doc.at.least.3000.lines", big.document->lines() >= 3000);

  std::vector<double> foldTimes;
  std::vector<double> unfoldTimes;
  for (int i = 0; i < 60; ++i) {
    const int startLine = 3 + i * 20;
    const Execution run = execCommand(fold, big.view, QStringLiteral("fold"),
                                      KTextEditor::Range(startLine, 0, startLine + 8, 0));
    if (run.ok) {
      foldTimes.push_back(run.us);
    }
    pump();
    big.view->setCursorPosition(KTextEditor::Cursor(startLine, 0));
    pump();
    const Execution back = execCommand(unfold, big.view, QStringLiteral("unfold"));
    if (back.ok) {
      unfoldTimes.push_back(back.us);
    }
    pump();
  }
  report("fold_command_exec", summarize(foldTimes));
  report("unfold_command_exec", summarize(unfoldTimes));
  const Snapshot bigSnapshot = takeSnapshot(big.view);
  printSnapshot("big_doc_after_series", bigSnapshot);
  big.view->grab().save(outDir + QStringLiteral("/11-bigdoc-fold-series.png"));

  // ---- (b7) is there any query / removal API for a specific fold? ----
  std::cout << "\n== B6: query / removal surface ==\n";
  std::cout << "INFO state_query_api=absent (no public API returns the fold ranges or their "
               "folded state; only unfold-at-cursor mutates)\n";
  std::cout << "INFO fold_command_scope=Persistent|Folded (command 'fold'); 'tfold' creates a "
               "Folded-only range\n";

  // ---- folded screenshot with a normal size document ----
  Fixture demo = makeFixture(editor, verilogText(6), QStringLiteral("Verilog"));
  for (int m = 0; m < 6; ++m) {
    execCommand(fold, demo.view, QStringLiteral("fold"), KTextEditor::Range(m * 10 + 1, 0, m * 10 + 8, 0));
  }
  pump();
  const Snapshot demoSnapshot = takeSnapshot(demo.view);
  printSnapshot("demo_folded", demoSnapshot);
  demo.view->grab().save(outDir + QStringLiteral("/12-folded-view.png"));

  std::cout << "\nRESULT=" << (g_failures == 0 ? "PASS" : "FAIL") << " failures=" << g_failures
            << "\n";
  return g_failures == 0 ? 0 : 1;
}
