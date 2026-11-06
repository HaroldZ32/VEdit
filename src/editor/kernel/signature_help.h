#pragma once

// Local signatureHelp synthesis (T16 slice, ADDENDUM A-8).
//
// Neither backend provides signatureHelp (verible: no; slang-server 0.3.1: no),
// so the editor synthesises it from syntax + a symbol index:
//   * the CALL CONTEXT at the cursor comes from tree-sitter-verilog;
//   * the SIGNATURE comes from a declaration scanned out of the current document
//     first, then a lightweight project symbol index;
//   * the popup is a child of the view, the current parameter is derived from the
//     top-level commas before the cursor (structure-aware: nested commas belong
//     to a child node, commas inside strings are part of a string token).
//
// It is deliberately SYNTAX-LEVEL, not semantic: no type resolution, no overload
// selection, and anything ambiguous (unknown symbol, several candidates,
// unresolvable macro/parameterisation) shows NOTHING rather than a guess.
//
// The tree-sitter dependency is the syntax lane already wired into this kernel
// (VEDIT_EDIT_CORE_HAVE_FOLDING); without it the slice degrades to "no popup".
#include <KTextEditor/Cursor>
#include <QHash>
#include <QMetaObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QFrame;
class QLabel;
class QKeyEvent;
class QWidget;

namespace KTextEditor {
class Document;
class View;
} // namespace KTextEditor

namespace vedit::editor {

// One resolved declaration: the display form is `parameters` (each item the
// declaration's own text, trimmed), plus the call kind for the popup label.
struct SignatureInfo {
    QString name;
    QStringList parameters; // display text per parameter; may be empty
    QString origin;         // "current file" or the index file path
    QString kind;           // "function", "task", "module", "macro", "system"
    bool parameterList = false; // true for a module's `#(...)` context
};

// The call context found at the cursor.
struct CallContext {
    bool found = false;
    QString callee;
    QString kind;             // function/task/system/macro/module
    bool parameterList = false; // inside a module's `#(...)` rather than its port list
    int activeParameter = 0;
    int argumentCount = 0;
    int startByte = 0;
    int endByte = 0;
};

// One editor view's signature-help controller.
class SignatureHelp final : public QObject
{
    Q_OBJECT
public:
    static SignatureHelp *install(KTextEditor::View *view);
    static SignatureHelp *instanceFor(QWidget *editorWidget);

    // Project symbol index root (recursively scanned for .v/.sv/.svh/.vh).
    void setProjectRoot(const QString &root);
    void refreshIndex();

    // Recompute the context/signature for the current cursor and paint or hide
    // the popup. Called on cursor moves and text changes; public for the test.
    void refresh();
    void dismiss();

    // Observables (the lane selftest asserts these).
    bool popupVisible() const;
    QString popupText() const;
    QString calleeName() const;
    QString callKind() const;
    int activeParameter() const;
    bool parameterListContext() const;
    QString statusText() const; // last reason nothing was shown (diagnostics)

    // Context detection is public so the test can check it without a popup.
    CallContext contextAtCursor() const;

    // Declaration scanner (exposed for the selftest and the index).
    static QList<SignatureInfo> scanDeclarations(const QString &text, const QString &origin);

    KTextEditor::View *view() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit SignatureHelp(KTextEditor::View *view);
    ~SignatureHelp() override;

    void ensurePopup();
    void paint(const SignatureInfo &signature, const CallContext &context);
    void hidePopup(const QString &reason);
    void rebuildIndex();
    // candidates in the current document (rescanned on text change) and the index
    QList<SignatureInfo> candidatesFor(const QString &callee, const QString &kind,
                                       bool parameterList, QString *origin) const;
    void refreshDocumentScan();
    int byteOffsetAt(const KTextEditor::Cursor &cursor) const;

    KTextEditor::View *view_ = nullptr;
    KTextEditor::Document *connectedDocument_ = nullptr;
    QMetaObject::Connection cursorConnection_;
    QMetaObject::Connection textConnection_;
    QFrame *popup_ = nullptr;
    QLabel *popupLabel_ = nullptr;
    QString projectRoot_;
    QHash<QString, QList<SignatureInfo>> index_;
    QList<SignatureInfo> documentSignatures_;
    QString scannedDocumentText_;
    QString popupText_;
    QString callee_;
    QString callKind_;
    int activeParameter_ = -1;
    bool parameterListContext_ = false;
    QString status_;

    // tree-sitter state (opaque here; see the implementation)
    struct TreeState;
    TreeState *tree_ = nullptr;
};

} // namespace vedit::editor
