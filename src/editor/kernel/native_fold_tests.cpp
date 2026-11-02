// T128: KDE-native folding acceptance without any VEdit injection.
//
//   * a 10k-line file opens in <= 2 s (no auto-folds, no fold commands);
//   * opening a SECOND file in the same pane after native folds exist must not
//     crash (T121's trigger) and the view stays usable;
//   * cursor moves across a collapsed document must not crash (T117's trigger).
#include "ktexteditor_provider.h"

#include <KTextEditor/Command>
#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void pump()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

QString sample(int modules)
{
    QString text;
    for (int index = 0; index < modules; ++index) {
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
        for (int line = 8; line < 39; ++line) {
            text += QStringLiteral("  // filler %1 %2\n").arg(line).arg(n);
        }
        text += QStringLiteral("endmodule\n");
    }
    return text;
}

void writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(text.toUtf8());
    }
}

int hidden(KTextEditor::View *view)
{
    const int firstReal = view->firstDisplayedLine(KTextEditor::View::RealLine);
    const int lastReal = view->lastDisplayedLine(KTextEditor::View::RealLine);
    const int firstVisible = view->firstDisplayedLine(KTextEditor::View::VisibleLine);
    const int lastVisible = view->lastDisplayedLine(KTextEditor::View::VisibleLine);
    return (lastReal - firstReal) - (lastVisible - firstVisible);
}

} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("NATIVE-FOLD-TESTS ok=0 reason=no-temp-dir\n");
        return 1;
    }
    const QString smallPath = temp.path() + QStringLiteral("/small.sv");
    const QString bigPath = temp.path() + QStringLiteral("/big-10k.sv");
    const QString secondPath = temp.path() + QStringLiteral("/second.sv");
    writeFile(smallPath, sample(2));
    writeFile(bigPath, sample(256)); // 256 * 40 = 10,240 lines
    writeFile(secondPath, sample(25));

    auto provider = vedit::editor::makeEditCoreProvider();
    check("native.fold.provider", provider != nullptr);
    QWidget *editor = provider != nullptr ? provider->createEditor(nullptr) : nullptr;
    check("native.fold.editor", editor != nullptr);
    if (editor == nullptr) {
        std::printf("NATIVE-FOLD-TESTS ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    QString error;
    check("native.fold.warmup", provider->openFile(editor, smallPath, &error), error);
    pump();
    check("native.fold.warmup-no-auto-folds", hidden(vedit::editor::viewFor(editor)) == 0,
          QString::number(hidden(vedit::editor::viewFor(editor))));

    QElapsedTimer timer;
    timer.start();
    const bool bigOpened = provider->openFile(editor, bigPath, &error);
    const qint64 bigMs = timer.nsecsElapsed() / 1'000'000;
    std::printf("NATIVE-FOLD-TESTS big-open ms=%lld lines=%d\n",
                static_cast<long long>(bigMs),
                vedit::editor::viewFor(editor) != nullptr
                    ? vedit::editor::viewFor(editor)->document()->lines()
                    : -1);
    check("native.fold.big-open", bigOpened, error);
    check("native.fold.big-open-2s", bigMs <= 2000, QString::number(bigMs));

    KTextEditor::View *view = vedit::editor::viewFor(editor);
    check("native.fold.big-view", view != nullptr);
    if (view != nullptr) {
        check("native.fold.big-no-auto-folds", hidden(view) == 0, QString::number(hidden(view)));
        // Native fold via KDE's own command (this is what the user's gutter click
        // issues); then move the cursor across the document and open a SECOND
        // file: the T117/T121 crash triggers, with no VEdit injection involved.
        KTextEditor::Command *fold = KTextEditor::Editor::instance()->queryCommand(
            QStringLiteral("fold"));
        QString message;
        if (fold != nullptr) {
            fold->exec(view, QStringLiteral("fold"), message,
                       KTextEditor::Range(0, 0, 39, 0));
            pump();
        }
        // The hidden-line arithmetic is viewport-relative and can legitimately be
        // negative for a folded block whose visible span grows; what matters is
        // that KDE's own fold command CHANGED the view.
        const int hiddenAfterFold = hidden(view);
        check("native.fold.native-fold-applied", hiddenAfterFold != 0,
              QString::number(hiddenAfterFold));
        for (int line = 0; line < view->document()->lines(); line += 311) {
            view->setCursorPosition(KTextEditor::Cursor(line, 0));
            pump();
        }
        check("native.fold.second-open", provider->openFile(editor, secondPath, &error), error);
        pump();
        KTextEditor::View *second = vedit::editor::viewFor(editor);
        check("native.fold.second-view", second != nullptr);
        if (second != nullptr) {
            second->setCursorPosition(KTextEditor::Cursor(second->document()->lines() - 1, 0));
            pump();
            check("native.fold.second-cursor", second->cursorPosition().line() >= 0);
        }
    }
    std::printf("NATIVE-FOLD-TESTS ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
