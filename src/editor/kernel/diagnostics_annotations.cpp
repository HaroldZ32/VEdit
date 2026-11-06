#include "diagnostics_annotations.h"

#include <KTextEditor/AnnotationInterface>
#include <KTextEditor/Attribute>
#include <KTextEditor/Document>
#include <KTextEditor/MovingRange>
#include <KTextEditor/View>

#include <QBrush>
#include <QColor>
#include <QHash>
#include <QTextCharFormat>
#include <QVariant>

#include <algorithm>

namespace vedit::editor {

namespace {

/// AC-5.4: one line never carries more attribute ranges than this; entries
/// beyond the cap are merged into the line's last range.
constexpr int kMaxRangesPerLine = 1024;

/// AC-5.1/5.2: the squiggle sits below selections and search highlights.
constexpr qreal kUnderlineZDepth = -90000.0;

/// Live MovingRange objects of every annotations instance. The self-check reads
/// it to prove a closed file leaves no ranges behind (AC-5.3).
int g_activeRanges = 0;

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

/// The span a diagnostic covers, resolved against the live document. A
/// line-level annotation (no end information) underlines the whole line; an
/// empty line has no text to underline and is skipped.
struct ResolvedSpan {
    KTextEditor::Range range;
    int severity = 0;
    bool valid = false;
};

ResolvedSpan resolveSpan(KTextEditor::Document *document, const DiagnosticAnnotation &entry)
{
    ResolvedSpan span;
    if (document == nullptr || document->lines() <= 0) {
        return span;
    }
    const int lineCount = document->lines();
    const int line = std::clamp(entry.line, 0, lineCount - 1);
    const int startColumn = std::clamp(entry.column, 0, document->lineLength(line));
    int endLine = entry.endLine < line ? line : std::clamp(entry.endLine, line, lineCount - 1);
    int endColumn = entry.endColumn;
    if (endColumn < 0) {
        endColumn = document->lineLength(endLine);
    }
    endColumn = std::clamp(endColumn, 0, document->lineLength(endLine));

    if (endLine == line) {
        if (endColumn <= startColumn) {
            // Line-level entry (or a zero-width diagnostic): underline the text
            // of the line; nothing to draw on an empty line.
            endColumn = document->lineLength(line);
        }
        if (endColumn <= startColumn) {
            return span;
        }
        span.range = KTextEditor::Range(KTextEditor::Cursor(line, startColumn),
                                        KTextEditor::Cursor(line, endColumn));
    } else {
        span.range = KTextEditor::Range(KTextEditor::Cursor(line, startColumn),
                                        KTextEditor::Cursor(endLine, endColumn));
    }
    span.severity = entry.severity;
    span.valid = true;
    return span;
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
            return QBrush(diagnosticsSeverityColor(entry->severity));
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

DiagnosticsAnnotations::~DiagnosticsAnnotations()
{
    // Closed file: delete the ranges while the document is still alive, or just
    // forget them when the document's moving interface already died.
    if (document_ != nullptr) {
        clearUnderlines();
    } else {
        forgetUnderlines();
    }
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
    rebuildUnderlines();
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

// --- inline squiggle ---------------------------------------------------------

void DiagnosticsAnnotations::attachDocument(KTextEditor::Document *document)
{
    if (document == document_) {
        return;
    }
    QObject::disconnect(invalidateConnection_);
    QObject::disconnect(destroyedConnection_);
    document_ = document;
    if (document_ == nullptr) {
        return;
    }
    // The moving interface is invalidated on clear/reload AND when the document
    // is destroyed; while that signal is emitted the ranges are still valid, so
    // this is where they are deleted. A destroyed() fallback only forgets the
    // pointers, so a document that dies without the signal can never be
    // dereferenced through a stale MovingRange.
    invalidateConnection_ = connect(
        document_, &KTextEditor::Document::aboutToInvalidateMovingInterfaceContent, this,
        [this](KTextEditor::Document *) { clearUnderlines(); });
    destroyedConnection_ = connect(document_, &QObject::destroyed, this, [this]() {
        forgetUnderlines();
        document_ = nullptr;
    });
}

void DiagnosticsAnnotations::rebuildUnderlines()
{
    clearUnderlines();

    KTextEditor::Document *document = view_ != nullptr ? view_->document() : nullptr;
    attachDocument(document);
    if (document == nullptr) {
        return;
    }

    // Stable order by position: it makes the per-line cap deterministic and the
    // evidence reads the ranges in document order.
    QList<DiagnosticAnnotation> ordered = entries_;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DiagnosticAnnotation &left, const DiagnosticAnnotation &right) {
                         if (left.line != right.line) {
                             return left.line < right.line;
                         }
                         return left.column < right.column;
                     });

    QHash<int, int> rangesCreatedPerLine;
    QHash<int, int> lastRangeIndexPerLine;
    for (const DiagnosticAnnotation &entry : ordered) {
        const ResolvedSpan span = resolveSpan(document, entry);
        if (!span.valid) {
            continue;
        }
        const int startLine = span.range.start().line();
        if (rangesCreatedPerLine.value(startLine) >= kMaxRangesPerLine) {
            // AC-5.4: merge instead of growing the attribute set without bound.
            const int index = lastRangeIndexPerLine.value(startLine, -1);
            if (index >= 0 && index < ranges_.size()) {
                KTextEditor::MovingRange *last = ranges_.at(index);
                const KTextEditor::Cursor end =
                    last->end() < span.range.end() ? span.range.end() : last->end();
                last->setRange(KTextEditor::Range(last->start(), end), last->attribute(),
                               last->zDepth());
                ++mergedRangeCount_;
            }
            continue;
        }

        auto attribute = KTextEditor::Attribute::Ptr(new KTextEditor::Attribute());
        attribute->setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        attribute->setUnderlineColor(diagnosticsSeverityColor(span.severity));
        auto *range = document->newMovingRange(span.range);
        range->setRange(span.range, attribute, kUnderlineZDepth);
        ranges_.append(range);
        ++g_activeRanges;
        rangesCreatedPerLine[startLine] = rangesCreatedPerLine.value(startLine) + 1;
        lastRangeIndexPerLine[startLine] = static_cast<int>(ranges_.size()) - 1;
    }
}

void DiagnosticsAnnotations::clearUnderlines()
{
    for (KTextEditor::MovingRange *range : ranges_) {
        delete range;
    }
    g_activeRanges -= static_cast<int>(ranges_.size());
    ranges_.clear();
    mergedRangeCount_ = 0;
}

void DiagnosticsAnnotations::forgetUnderlines()
{
    g_activeRanges -= static_cast<int>(ranges_.size());
    ranges_.clear();
    mergedRangeCount_ = 0;
}

KTextEditor::MovingRange *DiagnosticsAnnotations::firstRangeOnLine(int line) const
{
    for (KTextEditor::MovingRange *range : ranges_) {
        if (range->start().line() == line) {
            return range;
        }
    }
    return nullptr;
}

int DiagnosticsAnnotations::underlineRangeCount() const
{
    return static_cast<int>(ranges_.size());
}

int DiagnosticsAnnotations::underlineRangeCountForLine(int line) const
{
    int count = 0;
    for (KTextEditor::MovingRange *range : ranges_) {
        if (range->start().line() == line) {
            ++count;
        }
    }
    return count;
}

QColor DiagnosticsAnnotations::underlineColorForLine(int line) const
{
    KTextEditor::MovingRange *range = firstRangeOnLine(line);
    if (range == nullptr || range->attribute() == nullptr) {
        return QColor();
    }
    return range->attribute()->underlineColor();
}

int DiagnosticsAnnotations::underlineStyleForLine(int line) const
{
    KTextEditor::MovingRange *range = firstRangeOnLine(line);
    if (range == nullptr || range->attribute() == nullptr) {
        return -1;
    }
    return static_cast<int>(range->attribute()->underlineStyle());
}

qreal DiagnosticsAnnotations::underlineZDepthForLine(int line) const
{
    KTextEditor::MovingRange *range = firstRangeOnLine(line);
    return range != nullptr ? range->zDepth() : 0.0;
}

bool DiagnosticsAnnotations::underlineStartsAt(int line, int column) const
{
    KTextEditor::MovingRange *range = firstRangeOnLine(line);
    return range != nullptr && range->start().column() == column;
}

QString DiagnosticsAnnotations::underlineTextForLine(int line) const
{
    KTextEditor::MovingRange *range = firstRangeOnLine(line);
    if (range == nullptr || document_ == nullptr) {
        return QString();
    }
    return document_->text(KTextEditor::Range(range->start(), range->end()));
}

int DiagnosticsAnnotations::mergedRangeCount() const
{
    return mergedRangeCount_;
}

int DiagnosticsAnnotations::maxRangesPerLine()
{
    return kMaxRangesPerLine;
}

int DiagnosticsAnnotations::activeRangeCount()
{
    return g_activeRanges;
}

} // namespace vedit::editor
