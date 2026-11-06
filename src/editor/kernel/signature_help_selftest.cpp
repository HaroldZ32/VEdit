// Lane selftest for T16 (AC-3.3 / ADDENDUM A-8): the local signatureHelp
// synthesiser.
//
// ADDENDUM A-8 grants this file. It is the lane's ctest target:
//
//   vedit-signature-help-selftest
//
// The assertions are the acceptance text, one check each:
//   * the popup shows the active parameter of the call the cursor is in;
//   * Esc closes ONLY the popup (typing keeps working afterwards, and the key is
//     not swallowed when no popup is visible);
//   * leaving the call context closes the popup immediately;
//   * top-level commas only: nested calls and commas inside strings do not move
//     the active parameter;
//   * no type resolution and no overload selection: an unknown callee and an
//     ambiguous one show NOTHING (with a reason in statusText()), never a guess;
//   * the module parameter list `#(...)` is its own context.
//
// Everything runs against the real KTextEditor document/view in the pinned Qt6
// image. When the KTextEditor plugin is unavailable the program exits 77 (the
// ctest SKIP_RETURN_CODE), never 0 - a skip must not look like a pass.
#include "signature_help.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QFrame>
#include <QSize>
#include <QStringList>
#include <QTest>

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const QString &name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("CHECK %s PASS\n", qPrintable(name));
        return;
    }
    ++g_failures;
    std::printf("CHECK %s FAIL%s\n", qPrintable(name),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(detail)));
}

KTextEditor::View *makeView(KTextEditor::Document *document, const QSize &size)
{
    KTextEditor::View *view = document->createView(nullptr);
    if (view == nullptr) {
        return nullptr;
    }
    view->resize(size);
    view->show();
    view->setFocus();
    return view;
}

/// Put the cursor at a zero-based position and let the controller refresh.
void placeCursor(KTextEditor::View *view, int line, int column)
{
    view->setCursorPosition(KTextEditor::Cursor(line, column));
    QApplication::processEvents();
}

/// A document with the declarations the checks call: two functions with plain
/// parameters, a nested call, a string with a comma, a system task and a module
/// with a parameter list.
QString fullText()
{
    // The shapes are the grammar's real ones (evidence/e5-advanced/
    // 02-fixture-shape.log): a call in an expression is a `tf_call`, the module
    // parameter list is a `list_of_parameter_assignments`.
    return QStringLiteral(
        "module sub #(parameter int W = 8, parameter int D = 1)\n"
        "  (input logic clk, output logic q);\n"
        "endmodule\n"
        "\n"
        "module top;\n"
        "  logic x;\n"
        "  function void foo(int a, int b);\n"
        "  endfunction\n"
        "  function void bar(int p, int q);\n"
        "  endfunction\n"
        "  sub #(8, 1) u_sub (.clk(clk), .q(q));\n"
        "  initial begin\n"
        "    x = 0;\n"
        "    x = foo(1, 2);\n"
        "    x = foo(1, bar(2, 3), 4);\n"
        "    x = foo(\"a,b\", 2);\n"
        "    $display(\"x\", 1);\n"
        "  end\n"
        "endmodule\n");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-signature-help-selftest"));

    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (editor == nullptr) {
        std::printf("SKIP: KTextEditor::Editor::instance() is null (katepart missing)\n");
        return 77;
    }
    KTextEditor::Document *document = editor->createDocument(nullptr);
    if (document == nullptr) {
        std::printf("SKIP: KTextEditor could not create a document (katepart missing)\n");
        return 77;
    }
    document->setText(fullText());
    KTextEditor::View *view = makeView(document, QSize(900, 420));
    if (view == nullptr) {
        std::printf("SKIP: KTextEditor could not create a view (katepart missing)\n");
        return 77;
    }
    QApplication::processEvents();

    vedit::editor::SignatureHelp *help = vedit::editor::SignatureHelp::install(view);
    check(QStringLiteral("sig.install"), help != nullptr);
    if (help == nullptr) {
        std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    check(QStringLiteral("sig.install-idempotent"),
          vedit::editor::SignatureHelp::install(view) == help);
    check(QStringLiteral("sig.instance-for-view"),
          vedit::editor::SignatureHelp::instanceFor(view) == help);
    check(QStringLiteral("sig.popup-is-a-view-child"),
          view->findChild<QFrame *>(QStringLiteral("vedit-signature-help")) != nullptr);

    const auto keyTarget = [view]() -> QWidget * {
        QWidget *focus = QApplication::focusWidget();
        return focus != nullptr ? focus : view;
    };

    // --- 1. a plain call: the active parameter follows the caret ------------
    placeCursor(view, 13, 14); // `    x = foo(1, 2);` after the first comma
    help->refresh();
    check(QStringLiteral("sig.popup-visible-in-call"), help->popupVisible(),
          help->statusText());
    check(QStringLiteral("sig.callee"), help->calleeName() == QStringLiteral("foo"),
          help->calleeName());
    check(QStringLiteral("sig.kind"), help->callKind() == QStringLiteral("function"),
          help->callKind());
    check(QStringLiteral("sig.active-parameter-1"), help->activeParameter() == 1,
          QString::number(help->activeParameter()));
    check(QStringLiteral("sig.popup-text"),
          help->popupText() == QStringLiteral("foo(int a, int b)"), help->popupText());

    placeCursor(view, 13, 12); // before the first comma -> parameter 0
    check(QStringLiteral("sig.active-parameter-0"), help->activeParameter() == 0,
          QString::number(help->activeParameter()));

    // --- 2. cursor leaving the call context closes it IMMEDIATELY -----------
    placeCursor(view, 13, 14);
    check(QStringLiteral("sig.popup-visible-before-leaving"), help->popupVisible());
    placeCursor(view, 4, 0); // `module top;` - outside every call
    check(QStringLiteral("sig.closes-on-cursor-leave"), !help->popupVisible(),
          help->statusText());

    // --- 3. Esc closes ONLY the popup ---------------------------------------
    placeCursor(view, 13, 14);
    check(QStringLiteral("sig.popup-visible-before-esc"), help->popupVisible());
    QTest::keyClick(keyTarget(), Qt::Key_Escape);
    QApplication::processEvents();
    check(QStringLiteral("sig.esc-closes-popup"), !help->popupVisible());
    // The popup being gone, the next keystroke must reach the document: Esc did
    // not leave the editor in a swallowing state.
    const QString beforeTyping = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_X);
    QApplication::processEvents();
    check(QStringLiteral("sig.typing-after-esc"), document->text() != beforeTyping,
          QStringLiteral("text unchanged"));
    // Restore the fixture text for the later checks (the inserted character is
    // not part of the scenario).
    document->setText(fullText());
    QApplication::processEvents();
    // Esc with NO popup visible must not be consumed: typing afterwards works.
    QTest::keyClick(keyTarget(), Qt::Key_Escape);
    QApplication::processEvents();
    const QString beforeSecondTyping = document->text();
    QTest::keyClick(keyTarget(), Qt::Key_Y);
    QApplication::processEvents();
    check(QStringLiteral("sig.esc-without-popup-passes-through"),
          document->text() != beforeSecondTyping);
    document->setText(fullText());
    QApplication::processEvents();

    // --- 4. top-level commas only: nested calls ----------------------------
    placeCursor(view, 14, 21); // inside `bar(2, 3`, after its comma
    help->refresh();
    check(QStringLiteral("sig.nested-callee"), help->calleeName() == QStringLiteral("bar"),
          help->calleeName());
    check(QStringLiteral("sig.nested-active-parameter"), help->activeParameter() == 1,
          QString::number(help->activeParameter()));

    placeCursor(view, 14, 25); // after `bar(2, 3)`, before the outer 4
    help->refresh();
    check(QStringLiteral("sig.outer-callee-after-nested-call"),
          help->calleeName() == QStringLiteral("foo"), help->calleeName());
    check(QStringLiteral("sig.outer-active-parameter"), help->activeParameter() == 2,
          QString::number(help->activeParameter()));

    // --- 5. a comma inside a string literal does not count -----------------
    placeCursor(view, 15, 18); // `x = foo("a,b", 2);` after the real comma
    help->refresh();
    check(QStringLiteral("sig.string-comma-ignored"), help->activeParameter() == 1,
          QString::number(help->activeParameter()));
    check(QStringLiteral("sig.string-call-visible"), help->popupVisible());

    // --- 6. nothing is guessed: unknown and ambiguous callees --------------
    document->setText(QStringLiteral("module m;\n  logic x;\n  initial begin\n    x = 0;\n"
                                     "    x = nothing(1, 2);\n  end\nendmodule\n"));
    QApplication::processEvents();
    placeCursor(view, 4, 19);
    help->refresh();
    check(QStringLiteral("sig.unknown-no-popup"), !help->popupVisible());
    check(QStringLiteral("sig.unknown-reason"),
          help->statusText().contains(QStringLiteral("no declaration")), help->statusText());

    document->setText(QStringLiteral(
        "module m;\n"
        "  logic x;\n"
        "  function void dup(int a);\n"
        "  endfunction\n"
        "  function void dup(int b);\n"
        "  endfunction\n"
        "  initial begin\n"
        "    x = 0;\n"
        "    x = dup(1);\n"
        "  end\n"
        "endmodule\n"));
    QApplication::processEvents();
    placeCursor(view, 8, 12);
    help->refresh();
    check(QStringLiteral("sig.ambiguous-no-popup"), !help->popupVisible());
    check(QStringLiteral("sig.ambiguous-reason"),
          help->statusText().contains(QStringLiteral("ambiguous")), help->statusText());

    // --- 7. system task and module parameter list --------------------------
    document->setText(fullText());
    QApplication::processEvents();
    placeCursor(view, 16, 17); // `$display("x", 1);` after its comma
    help->refresh();
    check(QStringLiteral("sig.system-callee"), help->calleeName() == QStringLiteral("$display"),
          help->calleeName());
    check(QStringLiteral("sig.system-kind"), help->callKind() == QStringLiteral("system"),
          help->callKind());
    check(QStringLiteral("sig.system-popup"), help->popupVisible(), help->statusText());

    placeCursor(view, 10, 10); // `sub #(8, 1)` after the parameter comma
    help->refresh();
    check(QStringLiteral("sig.module-callee"), help->calleeName() == QStringLiteral("sub"),
          help->calleeName());
    check(QStringLiteral("sig.module-kind"), help->callKind() == QStringLiteral("module"),
          help->callKind());
    check(QStringLiteral("sig.parameter-list-context"), help->parameterListContext());
    check(QStringLiteral("sig.parameter-list-active"), help->activeParameter() == 1,
          QString::number(help->activeParameter()));

    // --- 8. a call outside any call context has no popup -------------------
    document->setText(QStringLiteral("module m;\nendmodule\n"));
    QApplication::processEvents();
    placeCursor(view, 0, 8);
    help->refresh();
    check(QStringLiteral("sig.no-context-no-popup"), !help->popupVisible());
    check(QStringLiteral("sig.no-context-reason"),
          help->statusText().contains(QStringLiteral("no call context")), help->statusText());

    // --- 9. the declaration scanner (the index source) --------------------
    const QList<vedit::editor::SignatureInfo> declarations =
        vedit::editor::SignatureHelp::scanDeclarations(fullText(), QStringLiteral("current file"));
    bool sawFoo = false;
    bool sawBar = false;
    bool sawSub = false;
    for (const vedit::editor::SignatureInfo &info : declarations) {
        if (info.name == QStringLiteral("foo") && info.kind == QStringLiteral("function")
            && info.parameters.size() == 2) {
            sawFoo = true;
        }
        if (info.name == QStringLiteral("bar") && info.parameters.size() == 2) {
            sawBar = true;
        }
        if (info.name == QStringLiteral("sub") && info.kind == QStringLiteral("module")
            && info.parameters.size() == 2) {
            sawSub = true;
        }
    }
    check(QStringLiteral("sig.scan-function-foo"), sawFoo);
    check(QStringLiteral("sig.scan-function-bar"), sawBar);
    check(QStringLiteral("sig.scan-module-sub"), sawSub);

    std::printf("SUMMARY checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
