// T125 decisive experiment: does our own directory-loaded KSyntaxHighlighting
// definition (resources/syntax/vedit-*.xml, beginRegion="veditFold") make
// KTextEditor produce folding regions that its OWN commands can toggle, WITHOUT
// any command injection by VEdit?
//
// Usage: vedit-fold-native-probe <definitions-dir>
// It copies the two XMLs into a private share tree, narrows XDG_DATA_DIRS/
// XDG_DATA_HOME before QApplication (the delivered AppRun does the same), opens
// a 25-module SystemVerilog document, puts the cursor on a module line and runs
// KTextEditor's own `tfold` / `fold` command with NO range. It prints the view
// observation before/after; a changed scroll_max / hidden count proves native
// folding exists.
#include <KTextEditor/Command>
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

namespace {

// Does OUR XML produce KSyntaxHighlighting folding regions at all? That is the
// prerequisite for native folding (independent of KTextEditor's command quirks).
class FoldingCounter final : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    int begin = 0;
    int end = 0;

    void run(QStringView text, KSyntaxHighlighting::State &state)
    {
        state = highlightLine(text, state);
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override
    {
        (void)offset;
        (void)length;
        (void)format;
    }

    void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override
    {
        (void)offset;
        (void)length;
        if (region.type() == KSyntaxHighlighting::FoldingRegion::Begin) {
            ++begin;
        } else if (region.type() == KSyntaxHighlighting::FoldingRegion::End) {
            ++end;
        }
    }
};

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
    if (view == nullptr) {
        return m;
    }
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

void pump()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

void print(const char *label, const Metrics &m, double seconds)
{
    std::printf("NATIVE-PROBE %s first=%d last=%d hidden=%d scroll_max=%d at=%.3fs\n", label,
                m.firstReal, m.lastReal, m.hidden, m.scrollMax, seconds);
    std::fflush(stdout);
}

bool run(KTextEditor::Editor *editor, KTextEditor::View *view, const char *command, int line,
         double *seconds)
{
    KTextEditor::Command *cmd = editor->queryCommand(QString::fromLatin1(command));
    if (cmd == nullptr) {
        std::printf("NATIVE-PROBE command-missing %s\n", command);
        return false;
    }
    view->setCursorPosition(KTextEditor::Cursor(line, 0));
    pump();
    QElapsedTimer timer;
    timer.start();
    QString message;
    const bool ok = cmd->exec(view, QString::fromLatin1(command), message,
                              KTextEditor::Range::invalid());
    pump();
    *seconds = timer.nsecsElapsed() / 1'000'000'000.0;
    std::printf("NATIVE-PROBE run command=%s line=%d returned=%d\n", command, line, ok ? 1 : 0);
    return ok;
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
            std::printf("NATIVE-PROBE copy-failed %s\n", qPrintable(from));
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QString definitions = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (definitions.isEmpty()) {
        std::printf("NATIVE-PROBE usage=vedit-fold-native-probe <resources/syntax dir>\n");
        return 2;
    }
    QTemporaryDir share;
    if (!share.isValid() || !copyDefinitions(definitions, share.path())) {
        std::printf("NATIVE-PROBE setup=failed\n");
        return 2;
    }
    qputenv("XDG_DATA_DIRS", share.path().toLocal8Bit());
    qputenv("XDG_DATA_HOME", share.path().toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (share.path() + QStringLiteral("/config")).toLocal8Bit());
    std::printf("NATIVE-PROBE share=%s\n", qPrintable(share.path()));

    QApplication app(argc, argv);
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("NATIVE-PROBE no-editor\n");
        return 1;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        std::printf("NATIVE-PROBE no-document\n");
        return 1;
    }
    document->setText(sampleText());
    KSyntaxHighlighting::Repository repository;
    const KSyntaxHighlighting::Definition svDefinition =
        repository.definitionForName(QStringLiteral("SystemVerilog"));
    std::printf("NATIVE-PROBE sv-definition name='%s' author='%s' valid=%d\n",
                qPrintable(svDefinition.name()), qPrintable(svDefinition.author()),
                svDefinition.isValid() ? 1 : 0);
    // Layer 1: does the definition OUR XML produce folding regions for this text?
    KSyntaxHighlighting::Repository customRepository;
    customRepository.addCustomSearchPath(definitions);
    const KSyntaxHighlighting::Definition ourDefinition =
        customRepository.definitionForName(QStringLiteral("SystemVerilog"));
    FoldingCounter counter;
    counter.setDefinition(ourDefinition);
    KSyntaxHighlighting::State state;
    const QString svText = sampleText();
    const QStringList svLines = svText.split(QLatin1Char('\n'));
    for (const QString &line : svLines) {
        counter.run(QStringView(line), state);
    }
    std::printf("NATIVE-PROBE ksh-folding name='%s' author='%s' begin=%d end=%d\n",
                qPrintable(ourDefinition.name()), qPrintable(ourDefinition.author()), counter.begin,
                counter.end);

    const bool modeOk = document->setHighlightingMode(QStringLiteral("SystemVerilog"));
    std::printf("NATIVE-PROBE mode=%s mode_set=%d\n",
                qPrintable(document->highlightingMode()), modeOk ? 1 : 0);
    KTextEditor::View *view = document->createView(nullptr);
    view->resize(1200, 900);
    view->show();
    // Native folding needs the folding bar / marker machinery enabled; check the
    // KTextEditor View config keys (view.h documents folding-bar, folding-preview,
    // folding-marker-color).
    view->setConfigValue(QStringLiteral("folding-bar"), true);
    view->setConfigValue(QStringLiteral("folding-preview"), true);
    pump();
    print("baseline", observe(view), 0.0);

    double seconds = 0.0;
    const Metrics before = observe(view);
    run(editor, view, "tfold", 0, &seconds);
    const Metrics afterTfold = observe(view);
    print("after-tfold", afterTfold, seconds);
    std::printf("NATIVE-PROBE verdict tfold_changed=%d\n",
                (afterTfold.scrollMax != before.scrollMax || afterTfold.hidden != before.hidden
                 || afterTfold.firstReal != before.firstReal
                 || afterTfold.lastReal != before.lastReal)
                    ? 1
                    : 0);

    const Metrics beforeFold = observe(view);
    run(editor, view, "fold", 0, &seconds);
    const Metrics afterFold = observe(view);
    print("after-fold", afterFold, seconds);
    std::printf("NATIVE-PROBE verdict fold_changed=%d\n",
                (afterFold.scrollMax != beforeFold.scrollMax || afterFold.hidden != beforeFold.hidden
                 || afterFold.firstReal != beforeFold.firstReal
                 || afterFold.lastReal != beforeFold.lastReal)
                    ? 1
                    : 0);

    // Negative control: line 1 is not a region start (input port line).
    const Metrics beforeNegative = observe(view);
    run(editor, view, "tfold", 1, &seconds);
    const Metrics afterNegative = observe(view);
    std::printf("NATIVE-PROBE verdict non-region-tfold_changed=%d\n",
                (afterNegative.scrollMax != beforeNegative.scrollMax
                 || afterNegative.hidden != beforeNegative.hidden)
                    ? 1
                    : 0);
    // Control: a KDE definition that definitely has folding regions (C++ braces).
    KTextEditor::Document *cppDocument = editor->createDocument(nullptr);
    cppDocument->setText(QStringLiteral("class X {\npublic:\n  void f() {\n    int y = 1;\n  }\n};\n"));
    cppDocument->setHighlightingMode(QStringLiteral("C++"));
    KTextEditor::View *cppView = cppDocument->createView(nullptr);
    cppView->resize(1200, 900);
    cppView->show();
    cppView->setConfigValue(QStringLiteral("folding-bar"), true);
    cppView->setConfigValue(QStringLiteral("folding-preview"), true);
    pump();
    KTextEditor::Command *cppTfold = editor->queryCommand(QStringLiteral("tfold"));
    cppView->setCursorPosition(KTextEditor::Cursor(0, 0));
    pump();
    QString cppMessage;
    const bool cppOk = cppTfold != nullptr
        && cppTfold->exec(cppView, QStringLiteral("tfold"), cppMessage,
                          KTextEditor::Range::invalid());
    pump();
    std::printf("NATIVE-PROBE cpp-control mode='%s' tfold_returned=%d\n",
                qPrintable(cppDocument->highlightingMode()), cppOk ? 1 : 0);
    std::printf("NATIVE-PROBE done\n");
    return 0;
}
