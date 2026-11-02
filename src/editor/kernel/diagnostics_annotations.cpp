#include "diagnostics_annotations.h"

#include <KTextEditor/AnnotationInterface>
#include <KTextEditor/View>

#include <QBrush>
#include <QColor>
#include <QHash>
#include <QVariant>

namespace vedit::editor {

namespace {

QString glyphFor(int severity)
{
    switch (severity) {
    case 1:
        return QStringLiteral("✖");
    case 2:
        return QStringLiteral("⚠");
    case 3:
        return QStringLiteral("ℹ");
    case 4:
        return QStringLiteral("·");
    default:
        return QStringLiteral("•");
    }
}

QColor colorFor(int severity)
{
    switch (severity) {
    case 1:
        return QColor(0xc6, 0x28, 0x28);
    case 2:
        return QColor(0xb2, 0x6a, 0x00);
    case 3:
        return QColor(0x15, 0x65, 0xc0);
    default:
        return QColor(0x60, 0x60, 0x60);
    }
}

} // namespace

/// The model KatePart asks for the border of each line.
class DiagnosticsAnnotations::Model final : public KTextEditor::AnnotationModel {
public:
    explicit Model(DiagnosticsAnnotations *owner)
        : owner_(owner)
    {
        setParent(owner);
    }

    QVariant data(int line, Qt::ItemDataRole role) const override
    {
        const DiagnosticAnnotation *entry = owner_->annotationAt(line);
        if (entry == nullptr) {
            return {};
        }
        switch (role) {
        case Qt::DisplayRole:
            return glyphFor(entry->severity);
        case Qt::ToolTipRole:
            return owner_->tooltipForLine(line);
        case Qt::ForegroundRole:
            return QBrush(Qt::white);
        case Qt::BackgroundRole:
            return QBrush(colorFor(entry->severity));
        default:
            return {};
        }
    }

private:
    DiagnosticsAnnotations *owner_ = nullptr;
};

DiagnosticsAnnotations::DiagnosticsAnnotations(KTextEditor::View *view)
    : QObject(view)
    , view_(view)
{
    model_ = new Model(this);
}

DiagnosticsAnnotations *DiagnosticsAnnotations::install(KTextEditor::View *view)
{
    if (view == nullptr) {
        return nullptr;
    }
    if (auto *existing = forView(view)) {
        return existing;
    }
    auto *annotations = new DiagnosticsAnnotations(view);
    annotations->model_ = new Model(annotations);
    view->setAnnotationModel(annotations->model_);
    view->setAnnotationBorderVisible(true);
    return annotations;
}

DiagnosticsAnnotations *DiagnosticsAnnotations::forView(const KTextEditor::View *view)
{
    return view != nullptr ? view->findChild<DiagnosticsAnnotations *>() : nullptr;
}

void DiagnosticsAnnotations::setForView(KTextEditor::View *view,
                                        const QList<DiagnosticAnnotation> &entries)
{
    if (auto *annotations = forView(view)) {
        annotations->setEntries(entries);
    }
}

void DiagnosticsAnnotations::setEntries(const QList<DiagnosticAnnotation> &entries)
{
    entries_ = entries;
    if (model_ != nullptr) {
        // Simplest correct refresh: the model is small, and reset() makes
        // KatePart recompute the border width and repaint every line.
        emit model_->reset();
    }
}

const QList<DiagnosticAnnotation> &DiagnosticsAnnotations::entries() const
{
    return entries_;
}

const DiagnosticAnnotation *DiagnosticsAnnotations::annotationAt(int line) const
{
    for (const DiagnosticAnnotation &entry : entries_) {
        if (entry.line == line) {
            return &entry;
        }
    }
    return nullptr;
}

int DiagnosticsAnnotations::annotatedLineCount() const
{
    QHash<int, int> lines;
    for (const DiagnosticAnnotation &entry : entries_) {
        lines.insert(entry.line, entry.severity);
    }
    return lines.size();
}

QString DiagnosticsAnnotations::tooltipForLine(int line) const
{
    QStringList parts;
    for (const DiagnosticAnnotation &entry : entries_) {
        if (entry.line != line) {
            continue;
        }
        QString text = entry.message;
        if (!entry.rule.isEmpty()) {
            text = QStringLiteral("%1 [%2]").arg(entry.message, entry.rule);
        }
        parts.append(text);
    }
    return parts.join(QLatin1Char('\n'));
}

} // namespace vedit::editor
