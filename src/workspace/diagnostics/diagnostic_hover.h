#pragma once

// item 9: hover over a DIAGNOSTIC range.
//
// The existing LSP hover (a server answer shown next to the caret) stays for
// ordinary symbols. This is the other half: hovering a squiggle shows the
// diagnostic's severity, rule id and message, and a 「修复」 button iff the
// engine can really autofix that rule (measured, see autofix.h). When it
// cannot, there is NO button and the reason is in the tooltip.
//
// The decision function is separate from the widget so the headless self-test
// asserts both branches without a mouse.

#include "diagnostics_types.h"

#include <QFrame>
#include <QString>
#include <QVector>

class QLabel;
class QPushButton;

namespace vedit::workspace::diagnostics {

/// The row whose range covers (line, column). Rows without a usable end
/// position cover their whole line: the CLI producers do not always report an
/// end column, and a diagnostic that cannot be hovered is a dead end.
const DiagnosticEntry *diagnosticAt(const QVector<DiagnosticEntry> &rows, int line, int column);

/// "Error" / "Warning" / "Information" / "Hint" (LSP severity scale).
QString severityLabel(int severity);

struct HoverContent {
    bool found = false;
    DiagnosticEntry entry;
    bool fixable = false;
    /// The engine's answer is still on its way (the hover never blocks on it).
    bool fixPending = false;
    /// Why no 「修复」 button is offered (empty when `fixable`).
    QString fixUnavailableReason;
    QString title;   ///< "Error · no-trailing-spaces"
    QString body;    ///< the message
    QString tooltip; ///< everything, including the no-fix reason
};

/// Build the popup content. `fixSupported` is the engine's measured answer;
/// when it is false, `reason` is shown and no button is offered.
HoverContent describeDiagnostic(const DiagnosticEntry &entry, bool fixSupported,
                                const QString &reason = QString());

/// The popup: severity + rule id + message, plus the 「修复」 button only when
/// that diagnostic is autofixable.
class DiagnosticHoverPopup final : public QFrame {
    Q_OBJECT
public:
    explicit DiagnosticHoverPopup(QWidget *parent = nullptr);

    void setContent(const HoverContent &content);
    HoverContent content() const { return content_; }
    QLabel *textLabel() const { return label_; }
    QPushButton *fixButton() const { return fixButton_; }
    /// True only while a fix button is part of the popup.
    bool hasFixButton() const;

signals:
    void fixRequested();

private:
    QLabel *label_ = nullptr;
    QPushButton *fixButton_ = nullptr;
    HoverContent content_;
};

} // namespace vedit::workspace::diagnostics
