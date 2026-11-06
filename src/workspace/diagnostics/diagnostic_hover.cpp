#include "diagnostic_hover.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace vedit::workspace::diagnostics {

const DiagnosticEntry *diagnosticAt(const QVector<DiagnosticEntry> &rows, int line, int column)
{
    const DiagnosticEntry *wholeLine = nullptr;
    for (const DiagnosticEntry &entry : rows) {
        const bool hasRange = entry.endLine > entry.line
            || (entry.endLine == entry.line && entry.endColumn > entry.column);
        if (!hasRange) {
            // No end position from the producer: the row owns its whole line.
            if (entry.line == line && wholeLine == nullptr) {
                wholeLine = &entry;
            }
            continue;
        }
        if (entry.line == line && entry.endLine == line) {
            if (column >= entry.column && column < entry.endColumn) {
                return &entry;
            }
            continue;
        }
        if (entry.endLine > entry.line) {
            // A multi-line range: the first line starts at `column`, the last
            // ends before `endColumn`, every line in between is fully covered.
            if (line == entry.line && column >= entry.column && line <= entry.endLine) {
                return &entry;
            }
            if (line > entry.line && line < entry.endLine) {
                return &entry;
            }
            if (line == entry.endLine && column < entry.endColumn) {
                return &entry;
            }
        }
    }
    return wholeLine;
}

QString severityLabel(int severity)
{
    switch (severity) {
    case 1:
        return QCoreApplication::translate("VEdit", "Error");
    case 2:
        return QCoreApplication::translate("VEdit", "Warning");
    case 3:
        return QCoreApplication::translate("VEdit", "Information");
    case 4:
        return QCoreApplication::translate("VEdit", "Hint");
    default:
        return QCoreApplication::translate("VEdit", "Diagnostic");
    }
}

HoverContent describeDiagnostic(const DiagnosticEntry &entry, bool fixSupported,
                                const QString &reason)
{
    HoverContent content;
    content.found = true;
    content.entry = entry;
    const QString rule = entry.code.isEmpty() ? entry.source : entry.code;
    content.title = rule.isEmpty() ? severityLabel(entry.severity)
                                   : QStringLiteral("%1 · %2").arg(severityLabel(entry.severity), rule);
    content.body = entry.message;
    content.fixable = fixSupported;
    if (!fixSupported) {
        // The ruling's second branch: no button AND the reason is stated.
        content.fixUnavailableReason = reason.isEmpty()
            ? QCoreApplication::translate("VEdit", "this backend offers no autofix for this diagnostic")
            : reason;
    }
    QString tooltip = content.title;
    if (!content.body.isEmpty()) {
        tooltip += QLatin1Char('\n') + content.body;
    }
    if (!fixSupported) {
        tooltip += QLatin1Char('\n')
                   + (content.fixPending
                          ? QCoreApplication::translate("VEdit", "Checking for an automatic fix…")
                          : QCoreApplication::translate("VEdit", "No automatic fix: %1")
                                .arg(content.fixUnavailableReason));
    }
    content.tooltip = tooltip;
    return content;
}

DiagnosticHoverPopup::DiagnosticHoverPopup(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("veditHoverPopup"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    label_ = new QLabel(this);
    label_->setObjectName(QStringLiteral("veditHoverPopupText"));
    label_->setTextFormat(Qt::PlainText);
    label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label_);
    fixButton_ = new QPushButton(QCoreApplication::translate("VEdit", "Fix"), this);
    fixButton_->setObjectName(QStringLiteral("veditHoverFix"));
    fixButton_->setVisible(false);
    auto *row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(fixButton_);
    layout->addLayout(row);
    connect(fixButton_, &QPushButton::clicked, this, [this]() { emit fixRequested(); });
}

void DiagnosticHoverPopup::setContent(const HoverContent &content)
{
    content_ = content;
    if (label_ != nullptr) {
        QString text = content.title;
        if (!content.body.isEmpty()) {
            text += QLatin1Char('\n') + content.body;
        }
        if (!content.fixable && !content.fixUnavailableReason.isEmpty()) {
            text += QLatin1Char('\n')
                    + QCoreApplication::translate("VEdit", "No automatic fix: %1")
                          .arg(content.fixUnavailableReason);
        }
        label_->setText(text);
        label_->setToolTip(content.tooltip);
    }
    if (fixButton_ != nullptr) {
        fixButton_->setVisible(content.fixable);
        fixButton_->setToolTip(content.fixable ? content.tooltip : content.fixUnavailableReason);
    }
    adjustSize();
}

bool DiagnosticHoverPopup::hasFixButton() const
{
    return fixButton_ != nullptr && content_.fixable;
}

} // namespace vedit::workspace::diagnostics
