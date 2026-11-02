#pragma once

// Inline diagnostics in the live editor (T131): one KTextEditor::AnnotationModel
// per View, installed when the view is created (same lifetime pattern as the
// fold/breadcrumb binding in fold_breadcrumb.cpp).
//
// The diagnostics lane pushes plain entries (line/severity/message/rule); this
// file owns only the presentation:
//   * a short glyph in the annotation border (✖ error / ⚠ warning / ℹ info),
//   * the message + rule name as the hover tooltip,
//   * a background tint so a line with an error is visible without hovering.
// No text is modified and the document is never touched.
#include <QList>
#include <QObject>
#include <QString>

namespace KTextEditor {
class View;
}

namespace vedit::editor {

/// One line-level annotation, pushed by the diagnostics lane.
struct DiagnosticAnnotation {
    int line = 0;      ///< zero-based
    int severity = 0;  ///< 1 error, 2 warning, 3 information, 4 hint, 0 unknown
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

    void setEntries(const QList<DiagnosticAnnotation> &entries);
    const QList<DiagnosticAnnotation> &entries() const;
    /// Number of distinct annotated lines (observable in tests).
    int annotatedLineCount() const;
    /// The tooltip the model would show for `line` (observable in tests).
    QString tooltipForLine(int line) const;

    KTextEditor::View *view() const { return view_; }

private:
    explicit DiagnosticsAnnotations(KTextEditor::View *view);
    /// The annotation that applies to `line`, or nullptr.
    const DiagnosticAnnotation *annotationAt(int line) const;

    class Model;
    KTextEditor::View *view_ = nullptr;
    Model *model_ = nullptr;
    QList<DiagnosticAnnotation> entries_;
};

} // namespace vedit::editor
