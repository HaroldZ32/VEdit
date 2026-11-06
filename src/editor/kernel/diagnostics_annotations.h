#pragma once

// Inline diagnostics in the live editor (T131, extended for the inline squiggle
// AC-5.1..5.4): one KTextEditor::AnnotationModel per View for the gutter glyphs,
// plus one KTextEditor::MovingRange per diagnostic span for the wavy underline.
//
// The diagnostics lane pushes plain entries (line/column range/severity/message/
// rule); this file owns only the presentation:
//   * a short glyph in the annotation border (✖ error / ⚠ warning / ℹ info), kept
//     from T131 - the icon is NOT replaced by the underline,
//   * the message + rule name as the hover tooltip,
//   * a background tint on the annotation border,
//   * a wavy underline (QTextCharFormat::SpellCheckUnderline) under the span,
//     red for errors and orange for warnings, z-depth -90000 so the squiggle
//     never covers selections/highlights.
//
// Ranges are KTextEditor::MovingRange objects, so they follow edits; they are
// deleted when the entries are replaced, when the document's moving interface
// dies and when the annotations object dies (a closed file leaves none behind).
// No text is modified and the document is never touched.
#include <QColor>
#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>

namespace KTextEditor {
class Document;
class MovingRange;
class View;
} // namespace KTextEditor

namespace vedit::editor {

/// The severity palette, in ONE place: the gutter glyph, the inline squiggle and
/// the diagnostics panel's groups must agree (AC-6.2).
inline QColor diagnosticsSeverityColor(int severity)
{
    switch (severity) {
    case 1:
        return QColor(0xc6, 0x28, 0x28); // error red
    case 2:
        return QColor(0xb2, 0x6a, 0x00); // warning orange
    case 3:
        return QColor(0x15, 0x65, 0xc0); // information blue
    default:
        return QColor(0x60, 0x60, 0x60); // hint / unknown grey
    }
}

/// Human-readable category used by the panel's group titles and by the F8
/// status-bar line. The words are user-visible, so they are translated (T27):
/// the `VEdit` context in translations/vedit_zh_CN.ts carries 错误/警告/信息/提示/问题.
inline QString diagnosticsSeverityName(int severity)
{
    switch (severity) {
    case 1:
        return QCoreApplication::translate("VEdit", "error");
    case 2:
        return QCoreApplication::translate("VEdit", "warning");
    case 3:
        return QCoreApplication::translate("VEdit", "information");
    case 4:
        return QCoreApplication::translate("VEdit", "hint");
    default:
        return QCoreApplication::translate("VEdit", "problem");
    }
}

/// One diagnostic span, pushed by the diagnostics lane.
///
/// `line` is the only field the current product wiring fills (a line-level
/// entry): the fallback underlines the whole line. When the producer supplies
/// `column`/`endLine`/`endColumn` the squiggle covers exactly that span; a
/// negative `endLine`/`endColumn` means "end of the entry's own line".
struct DiagnosticAnnotation {
    int line = 0;       ///< zero-based start line
    int column = 0;     ///< zero-based UTF-16 start column
    int endLine = -1;   ///< zero-based end line; < line => end of `line`
    int endColumn = -1; ///< zero-based end column; < 0 => end of the end line
    int severity = 0;   ///< 1 error, 2 warning, 3 information, 4 hint, 0 unknown
    QString message;
    QString rule; ///< rule/code name, shown next to the message
};

class DiagnosticsAnnotations final : public QObject {
    Q_OBJECT
public:
    /// Install (idempotently) an annotation model on `view`. Returns the existing
    /// one when the view already has it.
    static DiagnosticsAnnotations *install(KTextEditor::View *view);
    /// The binding for a view, or nullptr.
    static DiagnosticsAnnotations *forView(const KTextEditor::View *view);

    /// Replace the annotations of `view`; a no-op when the view has none (yet).
    static void setForView(KTextEditor::View *view, const QList<DiagnosticAnnotation> &entries);

    ~DiagnosticsAnnotations() override;

    void setEntries(const QList<DiagnosticAnnotation> &entries);
    const QList<DiagnosticAnnotation> &entries() const;
    /// Number of distinct annotated lines (observable in tests).
    int annotatedLineCount() const;
    /// The tooltip the model would show for `line` (observable in tests).
    QString tooltipForLine(int line) const;

    // --- inline squiggle observables (AC-5.1..5.4 evidence) ----------------
    /// Attribute ranges currently attached to the document by this object.
    int underlineRangeCount() const;
    /// Attribute ranges whose start line is `line`.
    int underlineRangeCountForLine(int line) const;
    /// The underline color of the first range starting on `line` (invalid when
    /// the line has none).
    QColor underlineColorForLine(int line) const;
    /// QTextCharFormat::UnderlineStyle of the first range starting on `line`
    /// (SpellCheckUnderline when the wavy underline is installed).
    int underlineStyleForLine(int line) const;
    /// The z-depth of the first range starting on `line` (the squiggle sits at
    /// -90000, below selections and search highlights).
    qreal underlineZDepthForLine(int line) const;
    /// Whether a range starts exactly at (`line`, `column`); this is what proves
    /// a MovingRange followed an edit.
    bool underlineStartsAt(int line, int column) const;
    /// The document text covered by the first range starting on `line` (empty
    /// when there is none / the document is gone).
    QString underlineTextForLine(int line) const;
    /// Entries that could not get their own range because the per-line cap was
    /// reached (AC-5.4: they are merged into the last range of that line).
    int mergedRangeCount() const;
    /// The hard per-line cap (1024 attribute ranges).
    static int maxRangesPerLine();
    /// Live MovingRange objects across every annotations object in this process
    /// (0 after a view/annotations object is destroyed - no residual ranges).
    static int activeRangeCount();

    KTextEditor::View *view() const { return view_; }

private:
    explicit DiagnosticsAnnotations(KTextEditor::View *view);
    /// The annotation that applies to `line`, or nullptr.
    const DiagnosticAnnotation *annotationAt(int line) const;
    void rebuildUnderlines();
    /// Delete the ranges we own (the document is still alive) and re-arm the
    /// per-line counters.
    void clearUnderlines();
    /// Forget the ranges without deleting them (the document/moving interface
    /// already destroyed them along with itself).
    void forgetUnderlines();
    /// (Re)connect the document lifecycle guards when the view's document
    /// changes. Caller passes the current document.
    void attachDocument(KTextEditor::Document *document);
    /// First range whose start line is `line`, or nullptr.
    KTextEditor::MovingRange *firstRangeOnLine(int line) const;

    class Model;
    KTextEditor::View *view_ = nullptr;
    Model *model_ = nullptr;
    KTextEditor::Document *document_ = nullptr;
    QList<KTextEditor::MovingRange *> ranges_;
    QList<DiagnosticAnnotation> entries_;
    int mergedRangeCount_ = 0;
    QMetaObject::Connection invalidateConnection_;
    QMetaObject::Connection destroyedConnection_;
};

} // namespace vedit::editor
