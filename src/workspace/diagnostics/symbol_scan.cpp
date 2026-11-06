#include "symbol_scan.h"

#include <QChar>
#include <QSet>

#include <algorithm>

namespace vedit::workspace::diagnostics {

namespace {

bool isIdentifierChar(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_')
           || character == QLatin1Char('$');
}

enum class LexState { Code, LineComment, BlockComment, String };

/// The lexical state at (line, column), scanned from the start of the document
/// so a multi-line `/* ... */` comment or a string opened on an earlier line is
/// classified correctly. A string is closed by an unescaped `"` or by the end of
/// its line (SystemVerilog strings do not span lines without a continuation).
LexState stateAt(const QString &text, int targetLine, int targetColumn)
{
    LexState state = LexState::Code;
    int line = 0;
    int column = 0;
    for (int index = 0; index < text.size(); ++index) {
        if (line == targetLine && column == targetColumn) {
            return state;
        }
        const QChar character = text.at(index);
        const QChar next = index + 1 < text.size() ? text.at(index + 1) : QChar();
        if (character == QLatin1Char('\n')) {
            ++line;
            column = 0;
            if (state == LexState::LineComment || state == LexState::String) {
                state = LexState::Code;
            }
            continue;
        }
        switch (state) {
        case LexState::Code:
            if (character == QLatin1Char('/') && next == QLatin1Char('/')) {
                state = LexState::LineComment;
                ++index; // the second '/' is part of the comment marker
                ++column;
            } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
                state = LexState::BlockComment;
                ++index;
                ++column;
            } else if (character == QLatin1Char('"')) {
                state = LexState::String;
            }
            break;
        case LexState::LineComment:
            break;
        case LexState::BlockComment:
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = LexState::Code;
                ++index;
                ++column;
            }
            break;
        case LexState::String:
            if (character == QLatin1Char('\\')) {
                // Skip the escaped character so a `\"` does not end the string.
                if (index + 1 < text.size()) {
                    const QChar escaped = text.at(index + 1);
                    if (escaped == QLatin1Char('\n')) {
                        ++line;
                        column = 0;
                    } else {
                        ++column;
                    }
                    ++index;
                }
            } else if (character == QLatin1Char('"')) {
                state = LexState::Code;
            }
            break;
        }
        ++column;
    }
    return state;
}

} // namespace

bool isVerilogKeyword(const QString &identifier)
{
    // Verilog-2001 + the SystemVerilog keywords an interactive edit lands on
    // most often (module/endmodule, always*, logic, assign, initial, ...).
    static const QSet<QString> keywords = {
        QStringLiteral("accept_on"), QStringLiteral("alias"), QStringLiteral("always"),
        QStringLiteral("always_comb"), QStringLiteral("always_ff"), QStringLiteral("always_latch"),
        QStringLiteral("and"), QStringLiteral("assert"), QStringLiteral("assign"),
        QStringLiteral("assume"), QStringLiteral("automatic"), QStringLiteral("before"),
        QStringLiteral("begin"), QStringLiteral("bind"), QStringLiteral("bins"),
        QStringLiteral("binsof"), QStringLiteral("bit"), QStringLiteral("break"),
        QStringLiteral("buf"), QStringLiteral("bufif0"), QStringLiteral("bufif1"),
        QStringLiteral("byte"), QStringLiteral("case"), QStringLiteral("casex"),
        QStringLiteral("casez"), QStringLiteral("cell"), QStringLiteral("chandle"),
        QStringLiteral("checker"), QStringLiteral("class"), QStringLiteral("clocking"),
        QStringLiteral("cmos"), QStringLiteral("config"), QStringLiteral("const"),
        QStringLiteral("constraint"), QStringLiteral("context"), QStringLiteral("continue"),
        QStringLiteral("cover"), QStringLiteral("covergroup"), QStringLiteral("coverpoint"),
        QStringLiteral("cross"), QStringLiteral("deassign"), QStringLiteral("default"),
        QStringLiteral("defparam"), QStringLiteral("design"), QStringLiteral("disable"),
        QStringLiteral("dist"), QStringLiteral("do"), QStringLiteral("edge"),
        QStringLiteral("else"), QStringLiteral("end"), QStringLiteral("endcase"),
        QStringLiteral("endchecker"), QStringLiteral("endclass"), QStringLiteral("endclocking"),
        QStringLiteral("endconfig"), QStringLiteral("endfunction"), QStringLiteral("endgenerate"),
        QStringLiteral("endgroup"), QStringLiteral("endinterface"), QStringLiteral("endmodule"),
        QStringLiteral("endpackage"), QStringLiteral("endprimitive"), QStringLiteral("endprogram"),
        QStringLiteral("endproperty"), QStringLiteral("endspecify"), QStringLiteral("endsequence"),
        QStringLiteral("endtable"), QStringLiteral("endtask"), QStringLiteral("enum"),
        QStringLiteral("event"), QStringLiteral("eventually"), QStringLiteral("expect"),
        QStringLiteral("export"), QStringLiteral("extends"), QStringLiteral("extern"),
        QStringLiteral("final"), QStringLiteral("first_match"), QStringLiteral("for"),
        QStringLiteral("force"), QStringLiteral("foreach"), QStringLiteral("forever"),
        QStringLiteral("fork"), QStringLiteral("forkjoin"), QStringLiteral("function"),
        QStringLiteral("generate"), QStringLiteral("genvar"), QStringLiteral("global"),
        QStringLiteral("highz0"), QStringLiteral("highz1"), QStringLiteral("if"),
        QStringLiteral("iff"), QStringLiteral("ifnone"), QStringLiteral("ignore_bins"),
        QStringLiteral("illegal_bins"), QStringLiteral("implements"), QStringLiteral("implies"),
        QStringLiteral("import"), QStringLiteral("incdir"), QStringLiteral("include"),
        QStringLiteral("initial"), QStringLiteral("inout"), QStringLiteral("input"),
        QStringLiteral("inside"), QStringLiteral("instance"), QStringLiteral("int"),
        QStringLiteral("integer"), QStringLiteral("interconnect"), QStringLiteral("interface"),
        QStringLiteral("intersect"), QStringLiteral("join"), QStringLiteral("join_any"),
        QStringLiteral("join_none"), QStringLiteral("large"), QStringLiteral("let"),
        QStringLiteral("liblist"), QStringLiteral("library"), QStringLiteral("local"),
        QStringLiteral("localparam"), QStringLiteral("logic"), QStringLiteral("longint"),
        QStringLiteral("macromodule"), QStringLiteral("matches"), QStringLiteral("medium"),
        QStringLiteral("modport"), QStringLiteral("module"), QStringLiteral("nand"),
        QStringLiteral("negedge"), QStringLiteral("nettype"), QStringLiteral("new"),
        QStringLiteral("nexttime"), QStringLiteral("nmos"), QStringLiteral("nor"),
        QStringLiteral("noshowcancelled"), QStringLiteral("not"), QStringLiteral("notif0"),
        QStringLiteral("notif1"), QStringLiteral("null"), QStringLiteral("or"),
        QStringLiteral("output"), QStringLiteral("package"), QStringLiteral("packed"),
        QStringLiteral("parameter"), QStringLiteral("pmos"), QStringLiteral("posedge"),
        QStringLiteral("primitive"), QStringLiteral("priority"), QStringLiteral("program"),
        QStringLiteral("property"), QStringLiteral("protected"), QStringLiteral("pull0"),
        QStringLiteral("pull1"), QStringLiteral("pulldown"), QStringLiteral("pullup"),
        QStringLiteral("pulsestyle_ondetect"), QStringLiteral("pulsestyle_onevent"),
        QStringLiteral("pure"), QStringLiteral("rand"), QStringLiteral("randc"),
        QStringLiteral("randcase"), QStringLiteral("randsequence"), QStringLiteral("rcmos"),
        QStringLiteral("real"), QStringLiteral("realtime"), QStringLiteral("ref"),
        QStringLiteral("reg"), QStringLiteral("reject_on"), QStringLiteral("release"),
        QStringLiteral("repeat"), QStringLiteral("restrict"), QStringLiteral("return"),
        QStringLiteral("rnmos"), QStringLiteral("rpmos"), QStringLiteral("rtran"),
        QStringLiteral("rtranif0"), QStringLiteral("rtranif1"), QStringLiteral("s_always"),
        QStringLiteral("s_eventually"), QStringLiteral("s_nexttime"), QStringLiteral("s_until"),
        QStringLiteral("s_until_with"), QStringLiteral("scalared"), QStringLiteral("sequence"),
        QStringLiteral("shortint"), QStringLiteral("shortreal"), QStringLiteral("showcancelled"),
        QStringLiteral("signed"), QStringLiteral("small"), QStringLiteral("soft"),
        QStringLiteral("solve"), QStringLiteral("specify"), QStringLiteral("specparam"),
        QStringLiteral("static"), QStringLiteral("string"), QStringLiteral("strong"),
        QStringLiteral("strong0"), QStringLiteral("strong1"), QStringLiteral("struct"),
        QStringLiteral("super"), QStringLiteral("supply0"), QStringLiteral("supply1"),
        QStringLiteral("sync_accept_on"), QStringLiteral("sync_reject_on"),
        QStringLiteral("table"), QStringLiteral("tagged"), QStringLiteral("task"),
        QStringLiteral("this"), QStringLiteral("throughout"), QStringLiteral("time"),
        QStringLiteral("timeprecision"), QStringLiteral("timeunit"), QStringLiteral("tran"),
        QStringLiteral("tranif0"), QStringLiteral("tranif1"), QStringLiteral("tri"),
        QStringLiteral("tri0"), QStringLiteral("tri1"), QStringLiteral("triand"),
        QStringLiteral("trior"), QStringLiteral("trireg"), QStringLiteral("type"),
        QStringLiteral("typedef"), QStringLiteral("union"), QStringLiteral("unique"),
        QStringLiteral("unique0"), QStringLiteral("unsigned"), QStringLiteral("until"),
        QStringLiteral("until_with"), QStringLiteral("untyped"), QStringLiteral("use"),
        QStringLiteral("uwire"), QStringLiteral("var"), QStringLiteral("vectored"),
        QStringLiteral("virtual"), QStringLiteral("void"), QStringLiteral("wait"),
        QStringLiteral("wait_order"), QStringLiteral("wand"), QStringLiteral("weak"),
        QStringLiteral("weak0"), QStringLiteral("weak1"), QStringLiteral("while"),
        QStringLiteral("wildcard"), QStringLiteral("wire"), QStringLiteral("with"),
        QStringLiteral("within"), QStringLiteral("wor"), QStringLiteral("xnor"),
        QStringLiteral("xor"),
    };
    return keywords.contains(identifier);
}

RenameTarget renameTargetAt(const QString &text, int line, int column)
{
    RenameTarget target;
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (line < 0 || line >= lines.size()) {
        target.reason = QStringLiteral("the position is outside the document");
        return target;
    }
    const QString &lineText = lines.at(line);
    const int length = lineText.size();

    // The identifier run under the cursor: the same left/right walk the editor
    // uses, so a cursor at either end of the token finds the whole token.
    int begin = std::max(0, std::min(column, length));
    int end = begin;
    while (begin > 0 && isIdentifierChar(lineText.at(begin - 1))) {
        --begin;
    }
    while (end < length && isIdentifierChar(lineText.at(end))) {
        ++end;
    }
    target.symbol = lineText.mid(begin, end - begin);
    if (target.symbol.isEmpty()) {
        target.reason = QStringLiteral("there is no symbol under the cursor");
        return target;
    }

    const LexState state = stateAt(text, line, begin);
    if (state == LexState::LineComment || state == LexState::BlockComment) {
        target.reason = QStringLiteral("the cursor is inside a comment, not on a symbol");
        return target;
    }
    if (state == LexState::String) {
        target.reason = QStringLiteral("the cursor is inside a string, not on a symbol");
        return target;
    }

    const QChar first = target.symbol.at(0);
    // A run right after the base marker (`1'b0`, `32'hFF`) is the tail of a
    // number literal, not an identifier.
    const bool afterBaseMarker = begin > 0 && lineText.at(begin - 1) == QLatin1Char('\'');
    if (afterBaseMarker || first.isDigit()) {
        target.reason = QStringLiteral("'%1' is a number literal, not a renameable symbol")
                            .arg(target.symbol);
        return target;
    }
    if (first == QLatin1Char('$')) {
        target.reason = QStringLiteral("'%1' is a system task/function, not a renameable symbol")
                            .arg(target.symbol);
        return target;
    }
    if (!(first.isLetter() || first == QLatin1Char('_'))) {
        target.reason = QStringLiteral("'%1' is not an identifier").arg(target.symbol);
        return target;
    }
    if (isVerilogKeyword(target.symbol)) {
        target.reason = QStringLiteral("'%1' is a language keyword, not a renameable symbol")
                            .arg(target.symbol);
        return target;
    }
    target.valid = true;
    return target;
}

} // namespace vedit::workspace::diagnostics
