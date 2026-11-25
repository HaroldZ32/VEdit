#include "editor_area.h"
#include "kernel/ktexteditor_provider.h"
#include "runtime_init.h"

#include <KTextEditor/Attribute>
#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

using namespace Qt::StringLiterals;
using KTextEditor::Cursor;
using KTextEditor::View;
int failures = 0;

bool check(const QString &label, bool pass, const QString &detail = {})
{
    std::printf("%s %s %s\n", pass ? "PASS" : "FAIL", qPrintable(label), qPrintable(detail));
    std::fflush(stdout);
    failures += !pass;
    return pass;
}

bool writeFile(const QString &path, const QByteArray &contents = {})
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QStringList paths(View *view)
{
    for (const auto *model : view->codeCompletionModels()) {
        if (model->objectName() == u"filelist-path-completion") {
            QStringList items;
            for (int row = 0; row < model->rowCount(); ++row) {
                items.append(model->index(row, KTextEditor::CodeCompletionModel::Name).data().toString());
            }
            return items;
        }
    }
    return {};
}

bool waitFor(const auto &predicate)
{
    for (int i = 0; i < 150 && !predicate(); ++i) {
        QTest::qWait(10);
    }
    return predicate();
}

void capture(QWidget &widget, const QString &directory, const QString &name)
{
    if (directory.isEmpty()) {
        return;
    }
    const auto origin = widget.mapToGlobal(QPoint());
    const auto image = widget.screen()->grabWindow(0, origin.x(), origin.y(), widget.width(), widget.height());
    check(u"capture "_s + name, !image.isNull() && image.save(QDir(directory).filePath(name + u".png")));
}

void key(View *view, Qt::Key event, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QTest::keyClick(view->focusProxy(), event, modifiers);
}

void expectPopup(View *view, const QString &name, const QStringList &expected)
{
    const bool visible = waitFor([&] { return view->isCompletionActive() && paths(view) == expected; });
    check(name, visible, u"active=%1 entries=[%2]"_s.arg(view->isCompletionActive()).arg(paths(view).join(u", ")));
}

void selectFirst(View *view)
{
    // Kate deliberately delays accepting a freshly visible automatic candidate.
    QTest::qWait(500);
    key(view, Qt::Key_Return);
    QTest::qWait(40);
}

void expectLine(View *view, int line, const QString &expected)
{
    const QString actual = view->document()->line(line);
    check(u"replacement line %1"_s.arg(line), actual == expected, actual);
}

void filelist(View *view, QWidget &window, const QString &images, const QString &root)
{
    auto *document = view->document();
    view->setFocus();
    QTest::qWait(60);
    view->setCursorPosition(Cursor(0, 2));
    QTest::keyClicks(view->focusProxy(), "../rt");
    expectPopup(view, u"typed relative directory"_s, {u"rtl/"_s});
    selectFirst(view);
    expectLine(view, 0, u"  ../rtl/"_s);
    expectPopup(view, u"accepted directory opens next level"_s, {u"block space.sv"_s, u"sub/"_s});
    capture(window, images, u"filelist-completion"_s);
    QTest::keyClicks(view->focusProxy(), "bl");
    selectFirst(view);
    expectLine(view, 0, u"  ../rtl/block space.sv"_s);
    key(view, Qt::Key_Z, Qt::ControlModifier);
    expectLine(view, 0, u"  ../rtl/bl"_s);

    view->setCursorPosition(Cursor(1, document->line(1).indexOf(u"zz-unused") + 2));
    key(view, Qt::Key_Backspace);
    key(view, Qt::Key_Backspace);
    QTest::keyClicks(view->focusProxy(), "in");
    expectPopup(view, u"second +incdir+ argument only"_s, {u"include/"_s});
    selectFirst(view);
    expectLine(view, 1, u"+incdir+../keep+../include/+../after # retain"_s);
    key(view, Qt::Key_Escape);

    view->setCursorPosition(Cursor(2, document->line(2).indexOf(u"ch-old") + 2));
    key(view, Qt::Key_Space, Qt::ControlModifier);
    expectPopup(view, u"manual -F completion in existing token"_s, {u"child.f"_s, u"chip.f"_s});
    selectFirst(view);
    expectLine(view, 2, u"-F child.f  // retain"_s);

    view->setCursorPosition(Cursor(3, document->line(3).indexOf(u"in-old") + 2));
    key(view, Qt::Key_Space, Qt::ControlModifier);
    expectPopup(view, u"attached -I completion"_s, {u"include/"_s});
    selectFirst(view);
    expectLine(view, 3, u"-I../include/ # retain"_s);
    key(view, Qt::Key_Escape);

    view->setCursorPosition(Cursor(4, 0));
    QTest::keyClicks(view->focusProxy(), "-f ./ch");
    expectPopup(view, u"lowercase -f completion"_s, {u"child.f"_s, u"chip.f"_s});
    selectFirst(view);
    expectLine(view, 4, u"-f ./child.f"_s);

    view->setCursorPosition(Cursor(5, 0));
    QTest::keyClicks(view->focusProxy(), qPrintable(root + u"/rtl/s"));
    expectPopup(view, u"absolute path completion"_s, {u"sub/"_s});
    selectFirst(view);
    expectPopup(view, u"absolute directory descends"_s, {u"deep.v"_s});
    key(view, Qt::Key_Escape);
    expectLine(view, 5, root + u"/rtl/sub/");

    auto *model = view->findChild<KTextEditor::CodeCompletionModel *>(u"filelist-path-completion"_s);
    auto *controller = qobject_cast<KTextEditor::CodeCompletionModelControllerInterface *>(model);
    for (int line = 6; line <= 8; ++line) {
        const QString old = document->line(line);
        view->setCursorPosition(Cursor(line, old.size()));
        key(view, Qt::Key_Space, Qt::ControlModifier);
        QTest::qWait(160);
        // Manual completion may still offer the editor's existing word candidates.
        check(u"no path candidates for comment, define and variable lines %1"_s.arg(line), controller
              && !controller->completionRange(view, view->cursorPosition()).isValid() && document->line(line) == old);
        key(view, Qt::Key_Escape);
        check(u"Escape dismisses any native candidates"_s, waitFor([&] { return !view->isCompletionActive(); }));
    }
    const int lineCount = document->lines();
    key(view, Qt::Key_Return);
    check(u"Return still inserts a line after dismissal"_s, document->lines() == lineCount + 1);

    const int existingPath = lineCount - 1;
    view->setCursorPosition(Cursor(existingPath, 5));
    key(view, Qt::Key_Space, Qt::ControlModifier);
    expectPopup(view, u"directory replacement before an existing path suffix"_s, {u"rtl/"_s});
    selectFirst(view);
    expectLine(view, existingPath, u"../rtl/sub/deep.v # retain"_s);
    key(view, Qt::Key_Escape);

    const int literal = document->lines() - 1;
    view->setCursorPosition(Cursor(literal, 0));
    QTest::keyClicks(view->focusProxy(), "out");
    expectPopup(view, u"literal dollar in a file name"_s, {u"out$.sv"_s});
    selectFirst(view);
    expectLine(view, literal, u"out$.sv"_s);
}

KTextEditor::Attribute::Ptr attribute(View *view, int line, const QString &token)
{
    const int column = view->document()->line(line).indexOf(token);
    for (const auto &block : view->lineAttributes(line)) {
        if (block.start <= column && column < block.start + block.length) {
            return block.attribute;
        }
    }
    return {};
}

QString style(const KTextEditor::Attribute::Ptr &value)
{
    return value ? u"%1 %2 bold=%3 italic=%4"_s.arg(value->name(), value->foreground().color().name())
                       .arg(value->fontBold()).arg(value->fontItalic()) : u"<unformatted>"_s;
}

void hdl(View *view, const QString &label)
{
    const auto definition = attribute(view, 0, u"board"_s);
    const auto instance = attribute(view, 2, u"u_simple"_s);
    const auto parameterized = attribute(view, 5, u"u_parameterized"_s);
    const auto repeated = attribute(view, 6, u"u_next"_s);
    const auto plain = attribute(view, 7, u"state"_s);
    const auto conditional = attribute(view, 8, u"if"_s);
    const auto otherBranch = attribute(view, 10, u"else"_s);
    const auto begin = attribute(view, 8, u"begin"_s);
    const auto end = attribute(view, 10, u"end"_s);
    const auto square = attribute(view, 7, u"["_s);
    const auto curly = attribute(view, 9, u"{"_s);
    const auto comment = attribute(view, 11, u"["_s);
    const auto string = attribute(view, 12, u"["_s);
    const auto blockComment = attribute(view, 14, u"{"_s);
    const auto named = [](const auto &value, QStringView name) { return value && value->name().endsWith(name); };
    check(label + u" definition and instance "_s, named(definition, u"Module Definition")
        && named(instance, u"Module Instance") && named(parameterized, u"Module Instance")
        && named(repeated, u"Module Instance") && definition->foreground() != instance->foreground(),
        style(definition) + u" | " + style(instance) + u" | " + style(parameterized) + u" | " + style(repeated));
    check(label + u" if/else vs begin/end "_s, named(conditional, u"Conditional") && named(otherBranch, u"Conditional")
        && named(begin, u"Block Delimiter") && named(end, u"Block Delimiter")
        && conditional->fontItalic() && !begin->fontItalic(), style(conditional) + u" | " + style(begin));
    const QColor plainColor = plain ? plain->foreground().color()
        : QColor::fromRgb(view->theme().textColor(KSyntaxHighlighting::Theme::Normal));
    check(label + u" square/curly brackets "_s, named(square, u"Bracket") && named(curly, u"Bracket")
        && square->foreground().color() != plainColor && curly->foreground().color() != plainColor,
        style(square) + u" | " + style(curly) + u" | plain " + plainColor.name());
    check(label + u" comment/string intact "_s, named(comment, u"Comment") && named(string, u"String")
        && named(blockComment, u"Comment"), style(comment) + u" | " + style(string) + u" | " + style(blockComment));
    const auto multiline = attribute(view, 18, u"u_multiline"_s);
    const auto withComment = attribute(view, 19, u"u_commented"_s);
    const auto routine = attribute(view, 20, u"calculate"_s);
    check(label + u" mixed declarations "_s, named(multiline, u"Module Instance") && named(withComment, u"Module Instance")
        && !named(routine, u"Module Instance"), style(multiline) + u" | " + style(withComment) + u" | " + style(routine));
}

void markdown(View *view, const QString &label)
{
    const auto heading = attribute(view, 0, u"Notes"_s);
    const auto code = attribute(view, 2, u"inline"_s);
    const auto bold = attribute(view, 2, u"emphasis"_s);
    const auto link = attribute(view, 2, u"example.test"_s);
    const auto prose = attribute(view, 2, u"Read"_s);
    const auto fence = attribute(view, 4, u"```"_s);
    const QColor proseColor = prose ? prose->foreground().color()
        : QColor::fromRgb(view->theme().textColor(KSyntaxHighlighting::Theme::Normal));
    check(label + u" Markdown theme spans "_s, view->document()->highlightingMode() == u"Markdown" && heading
        && (heading->fontBold() || heading->foreground().color() != proseColor) && code && link && fence
        && code->foreground().color() != proseColor && bold && bold->fontBold()
        && (link->foreground().color() != proseColor || link->fontUnderline()),
        style(heading) + u" | " + style(code) + u" | " + style(bold) + u" | " + style(link) + u" | " + style(fence));
}

constexpr auto kHdl = R"(module board;
  // real instances
  child u_simple(.a(a));
  child #(
    .WIDTH(2 * (1 + 3))
  ) u_parameterized [1:0] (.a(a)),
    u_next(.a(a));
  reg [7:0] state;
  always @* if (a) begin
    state = {state[6:0], a};
  end else begin
    // [ comment with if else begin end {}
    $display("[ string if else begin end {}");
    /*
       { comment inside block []
    */
  end
  child
    u_multiline(.a(a));
  child /* ports */ u_commented(.a(a));
  function automatic result_t calculate(input reg a);
    calculate = a;
  endfunction
endmodule
)";

constexpr auto kMarkdown = R"(# Notes

Read `inline` with **emphasis** and [link](https://example.test).

```
code block
```
)";

} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QTemporaryDir profile;
    if (!profile.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(profile.filePath(u"config"_s)));
    if (qEnvironmentVariableIsEmpty("VEDIT_SYNTAX_DIR")) {
        qputenv("XDG_DATA_HOME", QFile::encodeName(profile.filePath(u"data"_s)));
    }
    QApplication app(argc, argv);
    const QString images = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QTemporaryDir fixture;
    const QDir directory(fixture.path());
    const auto path = [&directory](QStringView file) { return directory.filePath(file.toString()); };
    if (!check(u"fixtures"_s, fixture.isValid() && directory.mkpath(u"lists"_s) && directory.mkpath(u"rtl/sub"_s)
        && directory.mkpath(u"include"_s) && writeFile(path(u"rtl/block space.sv"))
        && writeFile(path(u"rtl/sub/deep.v")) && writeFile(path(u"lists/child.f")) && writeFile(path(u"lists/out$.sv"))
        && writeFile(path(u"lists/chip.f"), "  \n+incdir+../keep+../zz-unused+../after # retain\n"
             "-F ch-old.f  // retain\n-I../in-old # retain\n\n\n// ../rtl/bl\n+define+PATH=../rtl/bl\n$CUSTOM/rtl/bl\n"
             "../rt-old/sub/deep.v # retain\n")
        && writeFile(path(u"notes.md"), kMarkdown) && writeFile(path(u"notes.markdown"), kMarkdown)
        && writeFile(path(u"board.sv"), kHdl) && writeFile(path(u"board.v"), kHdl))) {
        return 1;
    }
    auto provider = vedit::editor::makeEditCoreProvider();
    vedit::app::EditorArea window;
    window.setProvider(provider.get());
    window.resize(1000, 620);
    window.show();
    window.activateWindow();
    auto *pane = window.ensurePane();
    auto open = [&](QStringView file) -> View * {
        QString error;
        const bool opened = pane->openFile(path(file), &error);
        QTest::qWait(80);
        if (!check(u"open "_s + file, opened, error)) {
            return nullptr;
        }
        return vedit::editor::viewFor(pane->editorWidget());
    };
    if (auto *view = open(u"lists/chip.f")) {
        filelist(view, window, images, fixture.path());
        QFile disk(path(u"lists/chip.f"));
        check(u"completion only edits the live buffer"_s, disk.open(QIODevice::ReadOnly) && disk.readLine() == "  \n");
    }
    provider->applySyntaxTheme(u"Breeze Light"_s);
    for (const auto &name : {u"notes.md"_s, u"notes.markdown"_s}) {
        if (auto *view = open(name)) {
            markdown(view, name);
            check(name + u" path model absent"_s, !view->findChild<KTextEditor::CodeCompletionModel *>(u"filelist-path-completion"_s)
                || !view->codeCompletionModels().contains(view->findChild<KTextEditor::CodeCompletionModel *>(u"filelist-path-completion"_s)));
            capture(window, images, name + u"-light");
        }
    }
    for (const auto &name : {u"board.sv"_s, u"board.v"_s}) {
        if (auto *view = open(name)) {
            const auto mode = view->document()->highlightingMode();
            const auto definition = KTextEditor::Editor::instance()->repository().definitionForName(mode);
            check(name + u" live native repository"_s,
                  mode == (name.endsWith(u".sv") ? u"SystemVerilog" : u"Verilog") && definition.author() == u"VEdit",
                  mode + u" " + definition.filePath());
            hdl(view, name + u" light");
            capture(window, images, name + u"-light");
            view->setFocus();
            view->setCursorPosition(Cursor(0, 12));
            view->setSelection(KTextEditor::Range(0, 7, 0, 12));
            QTest::keyClicks(view->focusProxy(), "renamed_board");
            const bool refreshed = waitFor([&] {
                const auto live = attribute(view, 0, u"renamed_board"_s);
                return live && live->name().endsWith(u"Module Definition");
            });
            check(name + u" render refreshes on typed edit"_s,
                  refreshed && view->document()->line(0) == u"module renamed_board;",
                  view->document()->line(0) + u" | " + style(attribute(view, 0, u"renamed_board"_s)));
            key(view, Qt::Key_Z, Qt::ControlModifier);
            if (view->document()->line(0) != u"module board;") {
                key(view, Qt::Key_Z, Qt::ControlModifier);
            }
            view->removeSelection();
            view->setCursorPosition(Cursor(0, 0));
        }
    }
    provider->applySyntaxTheme(u"Breeze Dark"_s);
    for (int tab : {3, 4, 2, 1}) {
        pane->setActiveTab(tab);
        QTest::qWait(80);
        if (auto *view = vedit::editor::viewFor(pane->editorWidget())) {
            const QString filename = QFileInfo(view->document()->url().toLocalFile()).fileName();
            const QString label = filename + u" dark after tab switch";
            if (tab >= 3) {
                hdl(view, label);
            } else {
                markdown(view, label);
            }
            check(filename + u" existing view applied dark theme", view->theme().name() == u"Breeze Dark");
            capture(window, images, filename + u"-dark");
        }
    }
    check(u"close Markdown tab"_s, pane->closeTab(1));
    if (auto *view = open(u"notes.md")) {
        markdown(view, u"notes.md reopened"_s);
    }
    check(u"close SystemVerilog tab"_s, pane->closeTab(2));
    if (auto *view = open(u"board.sv")) {
        hdl(view, u"board.sv reopened"_s);
    }
    pane->setActiveTab(0);
    if (auto *view = vedit::editor::viewFor(pane->editorWidget())) {
        check(u"filelist tab retained its edits on switch"_s, view->document()->line(2) == u"-F child.f  // retain");
    }
    std::printf("LANGUAGE TEST failures=%d\n", failures);
    return failures ? 1 : 0;
}
