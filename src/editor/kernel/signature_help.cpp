#include "signature_help.h"

#include "multi_cursor.h" // installEditorEventFilter / isEditorOwnedWidget

#include <KTextEditor/Document>
#include <KTextEditor/Range>
#include <KTextEditor/View>

#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstring>

#if defined(VEDIT_EDIT_CORE_HAVE_FOLDING) && __has_include(<tree_sitter/api.h>)
#include <tree_sitter/api.h>
#define VEDIT_SIGNATURE_HELP_TREESITTER 1
extern "C" const TSLanguage *tree_sitter_verilog(void);
#else
#define VEDIT_SIGNATURE_HELP_TREESITTER 0
#endif

namespace vedit::editor {
namespace {

constexpr int kMaxIndexFiles = 400;
constexpr qint64 kMaxIndexFileBytes = 2 * 1024 * 1024;
constexpr int kMaxDocumentChars = 1200000;

bool isIdentStart(QChar character)
{
    return character.isLetter() || character == QLatin1Char('_') || character == QLatin1Char('$');
}

bool isIdentChar(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_') ||
           character == QLatin1Char('$');
}

// A copy of `text` with comments and string literals blanked (newlines kept).
// Positions are preserved, so a structural scan can run on it while the display
// text still comes from the original.
QString maskedText(const QString &text)
{
    QString masked = text;
    enum class State { Normal, LineComment, BlockComment, StringLiteral };
    State state = State::Normal;
    for (int index = 0; index < masked.size(); ++index) {
        const QChar current = masked.at(index);
        const QChar next = index + 1 < masked.size() ? masked.at(index + 1) : QChar();
        switch (state) {
        case State::Normal:
            if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
                masked[index] = QLatin1Char(' ');
                masked[index + 1] = QLatin1Char(' ');
                ++index;
                state = State::LineComment;
            } else if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
                masked[index] = QLatin1Char(' ');
                masked[index + 1] = QLatin1Char(' ');
                ++index;
                state = State::BlockComment;
            } else if (current == QLatin1Char('"')) {
                masked[index] = QLatin1Char(' ');
                state = State::StringLiteral;
            }
            break;
        case State::LineComment:
            if (current == QLatin1Char('\n')) {
                state = State::Normal;
            } else {
                masked[index] = QLatin1Char(' ');
            }
            break;
        case State::BlockComment:
            if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                masked[index] = QLatin1Char(' ');
                masked[index + 1] = QLatin1Char(' ');
                ++index;
                state = State::Normal;
            } else if (current != QLatin1Char('\n')) {
                masked[index] = QLatin1Char(' ');
            }
            break;
        case State::StringLiteral:
            if (current == QLatin1Char('\\')) {
                masked[index] = QLatin1Char(' ');
                if (index + 1 < masked.size()) {
                    masked[index + 1] = QLatin1Char(' ');
                    ++index;
                }
            } else if (current == QLatin1Char('"')) {
                masked[index] = QLatin1Char(' ');
                state = State::Normal;
            } else if (current != QLatin1Char('\n')) {
                masked[index] = QLatin1Char(' ');
            }
            break;
        }
    }
    return masked;
}

void skipSpaces(const QString &masked, int *position)
{
    while (*position < masked.size() && masked.at(*position).isSpace()) {
        ++*position;
    }
}

// Index of the ')' matching the '(' at `open`, or -1.
int matchingParen(const QString &masked, int open)
{
    if (open < 0 || open >= masked.size() || masked.at(open) != QLatin1Char('(')) {
        return -1;
    }
    int depth = 0;
    for (int index = open; index < masked.size(); ++index) {
        const QChar character = masked.at(index);
        if (character == QLatin1Char('(')) {
            ++depth;
        } else if (character == QLatin1Char(')')) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    return -1;
}

// Split the ORIGINAL text between `start` and `end` on top-level commas found in
// the masked text (nested commas and commas inside strings are never top level).
QStringList splitTopLevel(const QString &text, const QString &masked, int start, int end)
{
    QStringList items;
    int depth = 0;
    int itemStart = start;
    for (int index = start; index < end; ++index) {
        const QChar character = masked.at(index);
        if (character == QLatin1Char('(') || character == QLatin1Char('[') ||
            character == QLatin1Char('{')) {
            ++depth;
        } else if (character == QLatin1Char(')') || character == QLatin1Char(']') ||
                   character == QLatin1Char('}')) {
            --depth;
        } else if (character == QLatin1Char(',') && depth == 0) {
            const QString item = text.mid(itemStart, index - itemStart).simplified();
            if (!item.isEmpty()) {
                items.append(item);
            }
            itemStart = index + 1;
        }
    }
    const QString last = text.mid(itemStart, end - itemStart).simplified();
    if (!last.isEmpty()) {
        items.append(last);
    }
    return items;
}

// A few common system tasks; an unknown `$` call degrades to no popup.
QHash<QString, QStringList> systemTaskTable()
{
    static const QHash<QString, QStringList> table{
        {QStringLiteral("$display"), {QStringLiteral("string format"), QStringLiteral("arguments...")}},
        {QStringLiteral("$write"), {QStringLiteral("string format"), QStringLiteral("arguments...")}},
        {QStringLiteral("$monitor"), {QStringLiteral("string format"), QStringLiteral("arguments...")}},
        {QStringLiteral("$strobe"), {QStringLiteral("string format"), QStringLiteral("arguments...")}},
        {QStringLiteral("$sformatf"), {QStringLiteral("string format"), QStringLiteral("arguments...")}},
        {QStringLiteral("$fatal"), {QStringLiteral("code"), QStringLiteral("arguments...")}},
        {QStringLiteral("$error"), {QStringLiteral("arguments...")}},
        {QStringLiteral("$warning"), {QStringLiteral("arguments...")}},
        {QStringLiteral("$info"), {QStringLiteral("arguments...")}},
        {QStringLiteral("$time"), {}},
        {QStringLiteral("$realtime"), {}},
        {QStringLiteral("$random"), {QStringLiteral("seed")}},
        {QStringLiteral("$finish"), {QStringLiteral("code")}},
        {QStringLiteral("$stop"), {QStringLiteral("code")}},
        {QStringLiteral("$readmemh"), {QStringLiteral("filename"), QStringLiteral("memory"),
                                       QStringLiteral("start"), QStringLiteral("end")}},
        {QStringLiteral("$readmemb"), {QStringLiteral("filename"), QStringLiteral("memory"),
                                       QStringLiteral("start"), QStringLiteral("end")}},
        {QStringLiteral("$test$plusargs"), {QStringLiteral("string argument")}},
        {QStringLiteral("$value$plusargs"), {QStringLiteral("string format"),
                                             QStringLiteral("variable")}},
    };
    return table;
}

#if VEDIT_SIGNATURE_HELP_TREESITTER
QString nodeText(TSNode node, const QByteArray &text)
{
    if (ts_node_is_null(node)) {
        return QString();
    }
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (start > end || end > static_cast<uint32_t>(text.size())) {
        return QString();
    }
    return QString::fromUtf8(text.constData() + start, static_cast<int>(end - start));
}

bool nodeTypeIs(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && std::strcmp(ts_node_type(node), type) == 0;
}

TSNode directChildOfType(TSNode node, const char *type)
{
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (nodeTypeIs(child, type)) {
            return child;
        }
    }
    return TSNode{};
}

TSNode descendantOfTypeContaining(TSNode node, const char *type, uint32_t byte)
{
    if (ts_node_is_null(node)) {
        return TSNode{};
    }
    if (nodeTypeIs(node, type) && ts_node_start_byte(node) <= byte &&
        byte <= ts_node_end_byte(node)) {
        return node;
    }
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode found = descendantOfTypeContaining(ts_node_child(node, index), type, byte);
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return TSNode{};
}

bool containsByte(TSNode node, uint32_t byte)
{
    return !ts_node_is_null(node) && ts_node_start_byte(node) <= byte &&
           byte <= ts_node_end_byte(node);
}
#endif

} // namespace

struct SignatureHelp::TreeState {
#if VEDIT_SIGNATURE_HELP_TREESITTER
    TSParser *parser = nullptr;
    TSTree *tree = nullptr;
    QByteArray text;
    QString parsed;
#endif
};

// --- declaration scanner ---------------------------------------------------

QList<SignatureInfo> SignatureHelp::scanDeclarations(const QString &text, const QString &origin)
{
    QList<SignatureInfo> result;
    const QString masked = maskedText(text);

    static const QRegularExpression keyword(
        QStringLiteral("\\b(module|macromodule|function|task)\\b|`define\\s+([A-Za-z_][A-Za-z0-9_$]*)"));
    auto matches = keyword.globalMatch(masked);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (!match.captured(1).isEmpty()) {
            const QString word = match.captured(1);
            if (word == QLatin1String("module") || word == QLatin1String("macromodule")) {
                int position = match.capturedEnd(1);
                skipSpaces(masked, &position);
                const int nameStart = position;
                while (position < masked.size() && isIdentChar(masked.at(position))) {
                    ++position;
                }
                if (position == nameStart) {
                    continue;
                }
                const QString name = text.mid(nameStart, position - nameStart);
                int cursor = position;
                skipSpaces(masked, &cursor);
                QStringList parameters;
                if (cursor < masked.size() && masked.at(cursor) == QLatin1Char('#')) {
                    ++cursor;
                    skipSpaces(masked, &cursor);
                    if (cursor < masked.size() && masked.at(cursor) == QLatin1Char('(')) {
                        const int close = matchingParen(masked, cursor);
                        if (close < 0) {
                            continue;
                        }
                        parameters = splitTopLevel(text, masked, cursor + 1, close);
                        cursor = close + 1;
                    }
                }
                skipSpaces(masked, &cursor);
                QStringList ports;
                if (cursor < masked.size() && masked.at(cursor) == QLatin1Char('(')) {
                    const int close = matchingParen(masked, cursor);
                    if (close < 0) {
                        continue;
                    }
                    ports = splitTopLevel(text, masked, cursor + 1, close);
                }
                if (!parameters.isEmpty()) {
                    SignatureInfo info;
                    info.name = name;
                    info.parameters = parameters;
                    info.origin = origin;
                    info.kind = QStringLiteral("module");
                    info.parameterList = true;
                    result.append(info);
                }
                if (!ports.isEmpty()) {
                    SignatureInfo info;
                    info.name = name;
                    info.parameters = ports;
                    info.origin = origin;
                    info.kind = QStringLiteral("module");
                    info.parameterList = false;
                    result.append(info);
                }
                // A module with neither list (Verilog-2001 body ports) is not
                // synthesised: no signature is better than a wrong one.
            } else {
                // function / task: the name is the LAST identifier before '(' or ';'
                int position = match.capturedEnd(1);
                QString name;
                while (position < masked.size() && masked.at(position) != QLatin1Char('(') &&
                       masked.at(position) != QLatin1Char(';') &&
                       masked.at(position) != QLatin1Char('\n')) {
                    if (isIdentStart(masked.at(position))) {
                        const int start = position;
                        while (position < masked.size() && isIdentChar(masked.at(position))) {
                            ++position;
                        }
                        name = text.mid(start, position - start);
                    } else {
                        ++position;
                    }
                }
                if (name.isEmpty()) {
                    continue;
                }
                QStringList items;
                if (position < masked.size() && masked.at(position) == QLatin1Char('(')) {
                    const int close = matchingParen(masked, position);
                    if (close > position) {
                        items = splitTopLevel(text, masked, position + 1, close);
                    }
                }
                SignatureInfo info;
                info.name = name;
                info.parameters = items;
                info.origin = origin;
                info.kind = word;
                info.parameterList = false;
                result.append(info);
            }
        } else {
            const QString name = match.captured(2);
            int position = match.capturedEnd(2);
            if (position < masked.size() && masked.at(position) == QLatin1Char('(')) {
                const int close = matchingParen(masked, position);
                if (close > position) {
                    SignatureInfo info;
                    info.name = name;
                    info.parameters = splitTopLevel(text, masked, position + 1, close);
                    info.origin = origin;
                    info.kind = QStringLiteral("macro");
                    info.parameterList = false;
                    result.append(info);
                }
            }
        }
    }
    return result;
}

// --- controller ------------------------------------------------------------

SignatureHelp::SignatureHelp(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    setObjectName(QStringLiteral("vedit-signature-help-controller"));
}

SignatureHelp::~SignatureHelp()
{
#if VEDIT_SIGNATURE_HELP_TREESITTER
    if (tree_ != nullptr) {
        if (tree_->tree != nullptr) {
            ts_tree_delete(tree_->tree);
        }
        if (tree_->parser != nullptr) {
            ts_parser_delete(tree_->parser);
        }
        delete tree_;
    }
#endif
}

SignatureHelp *SignatureHelp::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (SignatureHelp *existing = instanceFor(view)) {
        return existing;
    }
    auto *help = new SignatureHelp(view);
    installEditorEventFilter(view, help);
    help->ensurePopup();
    QObject::connect(view, &KTextEditor::View::cursorPositionChanged, help,
                     [help](KTextEditor::View *, const KTextEditor::Cursor &) { help->refresh(); });
    help->connectedDocument_ = view->document();
    if (help->connectedDocument_ != nullptr) {
        help->textConnection_ =
            QObject::connect(help->connectedDocument_, &KTextEditor::Document::textChanged, help,
                             [help](KTextEditor::Document *) { help->refresh(); });
    }
    return help;
}

SignatureHelp *SignatureHelp::instanceFor(QWidget *editorWidget)
{
    if (editorWidget == nullptr) {
        return nullptr;
    }
    return editorWidget->findChild<SignatureHelp *>(
        QStringLiteral("vedit-signature-help-controller"));
}

KTextEditor::View *SignatureHelp::view() const
{
    return view_;
}

void SignatureHelp::ensurePopup()
{
    if (popup_ != nullptr || view_ == nullptr) {
        return;
    }
    popup_ = new QFrame(view_);
    popup_->setObjectName(QStringLiteral("vedit-signature-help"));
    popup_->setFrameShape(QFrame::StyledPanel);
    popup_->setFrameShadow(QFrame::Raised);
    popup_->setAutoFillBackground(true);
    popup_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    popup_->setFocusPolicy(Qt::NoFocus);
    auto *layout = new QVBoxLayout(popup_);
    layout->setContentsMargins(8, 4, 8, 4);
    popupLabel_ = new QLabel(popup_);
    popupLabel_->setObjectName(QStringLiteral("vedit-signature-help-text"));
    popupLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(popupLabel_);
    popup_->hide();
}

void SignatureHelp::setProjectRoot(const QString &root)
{
    projectRoot_ = root;
    rebuildIndex();
}

void SignatureHelp::refreshIndex()
{
    rebuildIndex();
}

void SignatureHelp::rebuildIndex()
{
    index_.clear();
    if (projectRoot_.isEmpty()) {
        return;
    }
    QDirIterator iterator(projectRoot_,
                          {QStringLiteral("*.v"), QStringLiteral("*.sv"), QStringLiteral("*.svh"),
                           QStringLiteral("*.vh")},
                          QDir::Files, QDirIterator::Subdirectories);
    int files = 0;
    while (iterator.hasNext() && files < kMaxIndexFiles) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.size() > kMaxIndexFileBytes) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        for (const SignatureInfo &signature : scanDeclarations(text, path)) {
            index_[signature.name].append(signature);
        }
        ++files;
    }
}

void SignatureHelp::refreshDocumentScan()
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr) {
        documentSignatures_.clear();
        scannedDocumentText_.clear();
        return;
    }
    if (connectedDocument_ != document) {
        if (textConnection_) {
            QObject::disconnect(textConnection_);
        }
        connectedDocument_ = document;
        textConnection_ =
            QObject::connect(document, &KTextEditor::Document::textChanged, this,
                             [this](KTextEditor::Document *) { refresh(); });
    }
    const QString text = document->text();
    if (text == scannedDocumentText_) {
        return;
    }
    scannedDocumentText_ = text;
    documentSignatures_ = scanDeclarations(text, QStringLiteral("current file"));
}

QList<SignatureInfo> SignatureHelp::candidatesFor(const QString &callee, const QString &kind,
                                                  bool parameterList, QString *origin) const
{
    const auto matches = [&](const SignatureInfo &info) {
        if (info.name != callee) {
            return false;
        }
        if (kind == QLatin1String("module")) {
            return info.kind == QLatin1String("module") && info.parameterList == parameterList;
        }
        if (kind == QLatin1String("macro")) {
            return info.kind == QLatin1String("macro");
        }
        // A statement-level call is parsed as a checker instantiation by the
        // grammar; accept both function and task declarations for it.
        return info.kind == QLatin1String("function") || info.kind == QLatin1String("task");
    };
    QList<SignatureInfo> current;
    for (const SignatureInfo &info : documentSignatures_) {
        if (matches(info)) {
            current.append(info);
        }
    }
    if (!current.isEmpty()) {
        if (origin != nullptr) {
            *origin = QStringLiteral("current file");
        }
        return current;
    }
    QList<SignatureInfo> indexed;
    for (const SignatureInfo &info : index_.value(callee)) {
        if (matches(info)) {
            indexed.append(info);
        }
    }
    if (!indexed.isEmpty() && origin != nullptr) {
        *origin = indexed.first().origin;
    }
    return indexed;
}

int SignatureHelp::byteOffsetAt(const KTextEditor::Cursor &cursor) const
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr) {
        return -1;
    }
    const QString prefix = document->text(KTextEditor::Range(KTextEditor::Cursor(0, 0), cursor));
    return prefix.toUtf8().size();
}

CallContext SignatureHelp::contextAtCursor() const
{
    CallContext context;
#if VEDIT_SIGNATURE_HELP_TREESITTER
    if (tree_ == nullptr || tree_->tree == nullptr || view_ == nullptr || view_->document() == nullptr) {
        return context;
    }
    const int offset = byteOffsetAt(view_->cursorPosition());
    if (offset < 0) {
        return context;
    }
    const uint32_t byte = static_cast<uint32_t>(offset);
    TSNode node = ts_node_named_descendant_for_byte_range(ts_tree_root_node(tree_->tree), byte, byte);
    // Inside a comment or a string literal there is no call to complete.
    for (TSNode walk = node; !ts_node_is_null(walk); walk = ts_node_parent(walk)) {
        if (nodeTypeIs(walk, "comment") || nodeTypeIs(walk, "string_literal")) {
            return context;
        }
    }

    for (TSNode walk = node; !ts_node_is_null(walk); walk = ts_node_parent(walk)) {
        const char *type = ts_node_type(walk);
        if (std::strcmp(type, "tf_call") == 0 || std::strcmp(type, "system_tf_call") == 0 ||
            std::strcmp(type, "text_macro_usage") == 0 ||
            std::strcmp(type, "module_instantiation") == 0 ||
            std::strcmp(type, "checker_instantiation") == 0) {
            TSNode list{};
            QString kind;
            bool parameterList = false;
            if (std::strcmp(type, "tf_call") == 0) {
                kind = QStringLiteral("function");
                list = descendantOfTypeContaining(walk, "list_of_arguments_parent", byte);
            } else if (std::strcmp(type, "system_tf_call") == 0) {
                kind = QStringLiteral("system");
                list = descendantOfTypeContaining(walk, "list_of_arguments_parent", byte);
            } else if (std::strcmp(type, "text_macro_usage") == 0) {
                kind = QStringLiteral("macro");
                list = descendantOfTypeContaining(walk, "list_of_actual_arguments", byte);
            } else if (std::strcmp(type, "module_instantiation") == 0) {
                kind = QStringLiteral("module");
                const TSNode parameterValue =
                    descendantOfTypeContaining(walk, "parameter_value_assignment", byte);
                if (!ts_node_is_null(parameterValue)) {
                    list = descendantOfTypeContaining(parameterValue,
                                                      "list_of_parameter_assignments", byte);
                    parameterList = true;
                } else {
                    list = descendantOfTypeContaining(walk, "list_of_port_connections", byte);
                }
            } else {
                // Grammar quirk: a statement-level `name(args);` is parsed as a
                // checker_instantiation (with an empty instance name), not a
                // tf_call. Treat it as a function/task call and count its commas.
                kind = QStringLiteral("function");
                list = walk;
            }
            if (ts_node_is_null(list)) {
                continue;
            }
            if (!containsByte(list, byte)) {
                continue;
            }
            // The cursor must be inside the parentheses: a ')' directly before
            // it means the call is complete and the popup must not appear.
            int probe = offset - 1;
            while (probe >= 0 && (tree_->text.at(probe) == ' ' || tree_->text.at(probe) == '\t' ||
                                  tree_->text.at(probe) == '\n' || tree_->text.at(probe) == '\r')) {
                --probe;
            }
            if (probe >= 0 && tree_->text.at(probe) == ')') {
                continue;
            }
            // Count the top-level commas before the cursor (direct children of
            // the list node: nested commas belong to a child node, commas inside
            // strings are part of a string token).
            int commas = 0;
            bool hasOpenParen = false;
            bool hasContent = false;
            const uint32_t childCount = ts_node_child_count(list);
            for (uint32_t index = 0; index < childCount; ++index) {
                const TSNode child = ts_node_child(list, index);
                const char *childType = ts_node_type(child);
                if (std::strcmp(childType, ",") == 0) {
                    if (ts_node_start_byte(child) < byte) {
                        ++commas;
                    }
                    hasContent = true;
                } else if (std::strcmp(childType, "(") == 0) {
                    if (ts_node_start_byte(child) < byte) {
                        hasOpenParen = true;
                    }
                } else if (std::strcmp(childType, ")") != 0 && ts_node_is_named(child)) {
                    hasContent = true;
                }
            }
            if (std::strcmp(type, "checker_instantiation") == 0) {
                // No list node: the '(' is a direct child, content is the event
                // expressions between the parentheses.
                for (uint32_t index = 0; index < childCount; ++index) {
                    const TSNode child = ts_node_child(list, index);
                    if (std::strcmp(ts_node_type(child), "(") == 0 &&
                        ts_node_start_byte(child) < byte) {
                        hasOpenParen = true;
                    }
                }
            }
            if (parameterList) {
                // T16 bug fix: for a module's `#( ... )` the parentheses are
                // siblings of `list_of_parameter_assignments` (children of the
                // `parameter_value_assignment`), so the loop above never sees a
                // '(' and the context was dropped. Measured shape:
                // evidence/e5-advanced/02-fixture-shape.log.
                const TSNode parameterValue =
                    descendantOfTypeContaining(walk, "parameter_value_assignment", byte);
                const uint32_t parameterChildren = ts_node_child_count(parameterValue);
                for (uint32_t index = 0; index < parameterChildren; ++index) {
                    const TSNode child = ts_node_child(parameterValue, index);
                    if (std::strcmp(ts_node_type(child), "(") == 0 &&
                        ts_node_start_byte(child) < byte) {
                        hasOpenParen = true;
                    }
                }
            }
            if (!hasOpenParen) {
                continue;
            }
            QString callee;
            if (std::strcmp(type, "tf_call") == 0) {
                callee = nodeText(directChildOfType(walk, "simple_identifier"), tree_->text);
                if (callee.isEmpty()) {
                    callee = nodeText(directChildOfType(walk, "escaped_identifier"), tree_->text);
                }
            } else if (std::strcmp(type, "system_tf_call") == 0) {
                callee = nodeText(directChildOfType(walk, "system_tf_identifier"), tree_->text);
            } else if (std::strcmp(type, "text_macro_usage") == 0) {
                callee = nodeText(directChildOfType(walk, "text_macro_identifier"), tree_->text);
            } else if (std::strcmp(type, "module_instantiation") == 0) {
                callee = nodeText(directChildOfType(walk, "simple_identifier"), tree_->text);
            } else {
                callee = nodeText(directChildOfType(walk, "checker_identifier"), tree_->text);
            }
            if (callee.isEmpty()) {
                continue;
            }
            context.found = true;
            context.callee = callee;
            context.kind = kind;
            context.parameterList = parameterList;
            context.activeParameter = commas;
            context.argumentCount = hasContent ? commas + 1 : 0;
            context.startByte = static_cast<int>(ts_node_start_byte(list));
            context.endByte = static_cast<int>(ts_node_end_byte(list));
            return context;
        }
    }
#else
    Q_UNUSED(offset);
#endif
    return context;
}

void SignatureHelp::hidePopup(const QString &reason)
{
    status_ = reason;
    popupText_.clear();
    callee_.clear();
    callKind_.clear();
    activeParameter_ = -1;
    parameterListContext_ = false;
    if (popup_ != nullptr) {
        popup_->hide();
    }
}

void SignatureHelp::dismiss()
{
    hidePopup(QStringLiteral("dismissed"));
}

void SignatureHelp::paint(const SignatureInfo &signature, const CallContext &context)
{
    ensurePopup();
    if (popup_ == nullptr || popupLabel_ == nullptr || view_ == nullptr) {
        return;
    }
    QStringList display;
    display.reserve(signature.parameters.size());
    for (const QString &parameter : signature.parameters) {
        display.append(parameter.toHtmlEscaped());
    }
    QString html = signature.name.toHtmlEscaped();
    html += signature.parameterList ? QStringLiteral(" #(") : QStringLiteral("(");
    for (int index = 0; index < display.size(); ++index) {
        if (index > 0) {
            html += QStringLiteral(", ");
        }
        if (index == context.activeParameter) {
            html += QStringLiteral("<b><u>") + display.at(index) + QStringLiteral("</u></b>");
        } else {
            html += display.at(index);
        }
    }
    html += signature.parameterList ? QStringLiteral(")") : QStringLiteral(")");

    QString plain = signature.name;
    plain += signature.parameterList ? QStringLiteral(" #(") : QStringLiteral("(");
    plain += signature.parameters.join(QStringLiteral(", "));
    plain += QStringLiteral(")");

    popupLabel_->setText(html);
    popup_->adjustSize();
    const QPoint point = view_->cursorToCoordinate(view_->cursorPosition());
    int x = point.x();
    int y = point.y() + 18;
    if (x + popup_->width() > view_->width() - 8) {
        x = view_->width() - popup_->width() - 8;
    }
    if (y + popup_->height() > view_->height() - 8) {
        y = point.y() - popup_->height() - 4;
    }
    popup_->move(std::max(0, x), std::max(0, y));
    popup_->raise();
    popup_->show();

    popupText_ = plain;
    callee_ = context.callee;
    callKind_ = context.kind;
    activeParameter_ = context.activeParameter;
    parameterListContext_ = context.parameterList;
    status_ = signature.origin.isEmpty() ? QStringLiteral("shown") : signature.origin;
}

void SignatureHelp::refresh()
{
    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    if (document == nullptr) {
        hidePopup(QStringLiteral("no document"));
        return;
    }
    if (document->text().size() > kMaxDocumentChars) {
        hidePopup(QStringLiteral("document too large for local signature help"));
        return;
    }
    refreshDocumentScan();

#if VEDIT_SIGNATURE_HELP_TREESITTER
    if (tree_ == nullptr) {
        tree_ = new TreeState;
    }
    const QString text = document->text();
    if (tree_->tree == nullptr || tree_->parsed != text) {
        if (tree_->parser == nullptr) {
            tree_->parser = ts_parser_new();
            ts_parser_set_language(tree_->parser, tree_sitter_verilog());
        }
        if (tree_->parser == nullptr) {
            hidePopup(QStringLiteral("tree-sitter parser unavailable"));
            return;
        }
        if (tree_->tree != nullptr) {
            ts_tree_delete(tree_->tree);
            tree_->tree = nullptr;
        }
        tree_->text = text.toUtf8();
        tree_->parsed = text;
        tree_->tree = ts_parser_parse_string(tree_->parser, nullptr, tree_->text.constData(),
                                             static_cast<uint32_t>(tree_->text.size()));
    }
    if (tree_->tree == nullptr) {
        hidePopup(QStringLiteral("tree-sitter parse failed"));
        return;
    }
#endif

    const CallContext context = contextAtCursor();
    if (!context.found) {
        hidePopup(QStringLiteral("no call context"));
        return;
    }
    if (context.kind == QLatin1String("system")) {
        const QHash<QString, QStringList> table = systemTaskTable();
        const auto entry = table.constFind(context.callee);
        if (entry == table.constEnd()) {
            hidePopup(QStringLiteral("unknown system task"));
            return;
        }
        SignatureInfo signature;
        signature.name = context.callee;
        signature.parameters = entry.value();
        signature.kind = QStringLiteral("system");
        signature.origin = QStringLiteral("system table");
        paint(signature, context);
        return;
    }
    QString origin;
    const QList<SignatureInfo> candidates =
        candidatesFor(context.callee, context.kind, context.parameterList, &origin);
    if (candidates.isEmpty()) {
        hidePopup(QStringLiteral("no declaration for %1").arg(context.callee));
        return;
    }
    if (candidates.size() > 1) {
        hidePopup(QStringLiteral("ambiguous declaration for %1").arg(context.callee));
        return;
    }
    paint(candidates.first(), context);
}

// --- event filter (Esc owns exactly one effect) ----------------------------

bool SignatureHelp::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded && isEditorOwnedWidget(view_, watched)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (child != nullptr && child->isWidgetType()) {
            installEditorEventFilter(view_, this);
        }
    }
    if (!isEditorOwnedWidget(view_, watched) || !popupVisible()) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            dismiss();
            event->accept();
            return true; // one keystroke, one effect: nothing underneath runs
        }
    }
    return QObject::eventFilter(watched, event);
}

bool SignatureHelp::popupVisible() const
{
    return popup_ != nullptr && popup_->isVisible();
}

QString SignatureHelp::popupText() const
{
    return popupText_;
}

QString SignatureHelp::calleeName() const
{
    return callee_;
}

QString SignatureHelp::callKind() const
{
    return callKind_;
}

int SignatureHelp::activeParameter() const
{
    return activeParameter_;
}

bool SignatureHelp::parameterListContext() const
{
    return parameterListContext_;
}

QString SignatureHelp::statusText() const
{
    return status_;
}

} // namespace vedit::editor
