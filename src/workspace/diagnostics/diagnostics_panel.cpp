#include "diagnostics_panel.h"
#include "../../common/item_context_menu.h"

#include "diagnostics_controller.h"
#include "diagnostics_hint.h"
#include "diagnostics_outline_model.h"

#include "jump/jump_registry.h"

#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
#include "diagnostics_annotations.h"
#endif

#include <QBrush>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QModelIndex>
#include <QStringLiteral>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <QCoreApplication>

namespace vedit::workspace::diagnostics {

namespace {

/// One palette for the gutter glyph, the inline squiggle and these groups
/// (AC-6.2). When the editor lane is part of the build the values come from the
/// editor's own header instead of being duplicated here.
QColor severityColor(int severity)
{
#if defined(VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS)
    return vedit::editor::diagnosticsSeverityColor(severity);
#else
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
#endif
}

// Data roles of a diagnostic row (a group header has no file role).
constexpr int kIssueFileRole = Qt::UserRole;
constexpr int kIssueLineRole = Qt::UserRole + 1;
constexpr int kIssueColumnRole = Qt::UserRole + 2;

QString issueRowText(const DiagnosticEntry &entry)
{
    QString text = QStringLiteral("%1:%2:%3 — %4")
                       .arg(QFileInfo(entry.file).fileName())
                       .arg(entry.line + 1)
                       .arg(entry.column + 1)
                       .arg(entry.message);
    if (!entry.code.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.code);
    } else if (!entry.source.isEmpty()) {
        text += QStringLiteral(" [%1]").arg(entry.source);
    }
    return text;
}

} // namespace

DiagnosticsPanel::DiagnosticsPanel(DiagnosticsController *controller, QWidget *parent)
    : QWidget(parent)
    , controller_(controller)
{
    // T131: the dock's title is what the user searches for. The panel is
    // diagnostics (with an outline section), not "Outline".
    setWindowTitle(QCoreApplication::translate("VEdit", "Diagnostics"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("diagnosticsStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    model_ = new OutlineModel(this);
    view_ = new QTreeView(this);
    view_->setObjectName(QStringLiteral("diagnosticsOutline"));
    view_->setModel(model_);
    view_->setRootIsDecorated(false);
    view_->setAlternatingRowColors(true);
    layout->addWidget(view_, 1);

    // AC-6.1/6.2: per-severity counts, colored exactly like the editor's gutter
    // glyph and inline squiggle.
    counts_ = new QLabel(this);
    counts_->setObjectName(QStringLiteral("diagnosticsCounts"));
    counts_->setTextFormat(Qt::RichText);
    counts_->setContentsMargins(6, 2, 6, 0);
    layout->addWidget(counts_);

    // AC-6.1: the grouped list (errors / warnings / information), sorted by
    // file:line and double-clickable (AC-6.3, AC-7.2).
    issues_ = new QTreeWidget(this);
    issues_->setObjectName(QStringLiteral("diagnosticsIssues"));
    issues_->setHeaderHidden(true);
    issues_->setRootIsDecorated(true);
    issues_->setExpandsOnDoubleClick(false);
    issues_->setUniformRowHeights(true);
    issues_->setMaximumHeight(220);
    issues_->setVisible(false);
    layout->addWidget(issues_);

    // AC-3 (batch-20261117): ONE persistent hint under the issues list. It is
    // deliberately not a diagnostic row: Verible's preprocessing error keeps its
    // position and the counts stay untouched; the hint only names the cause and
    // the fix. Empty text and hidden until the rows say otherwise.
    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("diagnosticsPreprocessHint"));
    hint_->setWordWrap(true);
    hint_->setContentsMargins(6, 0, 6, 0);
    hint_->setVisible(false);
    layout->addWidget(hint_);

    // AC-6.4 / AC-10.3.3: an explicit empty state that names the next step.
    diagnosticsEmpty_ = new QLabel(this);
    diagnosticsEmpty_->setObjectName(QStringLiteral("diagnosticsIssuesEmpty"));
    diagnosticsEmpty_->setWordWrap(true);
    diagnosticsEmpty_->setContentsMargins(6, 0, 6, 0);
    layout->addWidget(diagnosticsEmpty_);

    // One-line empty state. A missing toolchain is an environment state, not a
    // list of per-file errors: the outline stays empty and says why once.
    emptyState_ = new QLabel(this);
    emptyState_->setObjectName(QStringLiteral("diagnosticsEmptyState"));
    emptyState_->setWordWrap(true);
    layout->addWidget(emptyState_);

    // AC-7.1/7.2: both surfaces jump through the one process-wide registry; the
    // panel implements no file opening of its own.
    connect(view_, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &index) { jumpToOutline(index); });
    new vedit::common::ItemContextMenu(view_, [this](QMenu &menu, const QPersistentModelIndex &hit) {
        QAction *open = menu.addAction(tr("Go to Symbol"), this, [this, hit]() { jumpToOutline(hit); });
        open->setEnabled(hit.isValid() && model_ != nullptr && model_->symbolAt(hit.row()) != nullptr &&
                         controller_ != nullptr && !controller_->analyzeFileName().isEmpty());
        vedit::common::addCopyAction(menu, tr("Copy Symbol"), hit.sibling(hit.row(), 0).data().toString());
    });
    connect(issues_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { jumpToIssue(item); });

    refreshFromController();

    // The analysis is asynchronous (LSP + slang jobs): poll the controller, and
    // repaint the widgets whenever anything observable changed. Before T118 the
    // panel refreshed only in its constructor, so a finished analysis was never
    // shown - that is why the outline stayed empty on a real desktop.
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(300);
    connect(pollTimer_, &QTimer::timeout, this, [this]() { poll(); });
    pollTimer_->start();
}

void DiagnosticsPanel::poll()
{
    if (controller_ != nullptr) {
        // The controller poll advances the LSP/CLI analysis; the panel is its
        // only driver, so hiding the dock must NEVER stall the backends (T9):
        // only the WIDGET rebuild below is skipped.
        controller_->refresh();
        // T9 fix (4): a dock the user cannot see does no list work.
        if (!isVisible() && shownOnce_) {
            staleWhileHidden_ = true;
            return;
        }
        // T9 fix (3): one rebuild per CHANGE, not per 300 ms tick. The token
        // moves for structural AND per-file changes, so nothing is missed.
        const quint64 token = controller_->changeSerial();
        if (!staleWhileHidden_ && token == lastRefreshedChangeSerial_) {
            return;
        }
        lastRefreshedChangeSerial_ = token;
    }
    staleWhileHidden_ = false;
    refreshFromController();
}

void DiagnosticsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    shownOnce_ = true;
    // The dock may have been hidden while the view moved: show the CURRENT
    // state immediately instead of waiting for the next 300 ms tick.
    staleWhileHidden_ = false;
    refreshFromController();
    if (controller_ != nullptr) {
        lastRefreshedChangeSerial_ = controller_->changeSerial();
    }
}

QString DiagnosticsPanel::statusText() const
{
    if (status_ == nullptr) {
        return QString();
    }
    return status_->text();
}

int DiagnosticsPanel::outlineRowCount() const
{
    return model_ != nullptr ? model_->rowCount() : 0;
}

int DiagnosticsPanel::issueRowCount() const
{
    return issueRowCount_;
}

int DiagnosticsPanel::errorCount() const
{
    return errorCount_;
}

int DiagnosticsPanel::warningCount() const
{
    return warningCount_;
}

int DiagnosticsPanel::informationCount() const
{
    return informationCount_;
}

QString DiagnosticsPanel::countsText() const
{
    return countsText_;
}

QColor DiagnosticsPanel::errorColor() const
{
    return severityColor(1);
}

QColor DiagnosticsPanel::warningColor() const
{
    return severityColor(2);
}

int DiagnosticsPanel::groupForSeverity(int severity)
{
    if (severity == 1) {
        return 1; // errors
    }
    if (severity == 2) {
        return 2; // warnings
    }
    // Information/hint/unknown. Never merged into the two groups above
    // (AC-6.2: an error is never listed as a warning and vice versa).
    return 3;
}

QString DiagnosticsPanel::emptyStateText() const
{
    return emptyState_ != nullptr ? emptyState_->text() : QString();
}

bool DiagnosticsPanel::emptyStateVisible() const
{
    return emptyState_ != nullptr && emptyState_->isVisibleTo(this);
}

QString DiagnosticsPanel::diagnosticsEmptyStateText() const
{
    if (controller_ == nullptr) {
        return QString();
    }
    // batch-20261116 L1.2: when there IS no analysis path, the panel NAMES it -
    // one explicit line (no analyzer for a `.f`/`.md`, above the byte cap, top
    // scope excluded, buffer pending). The old text said "no problems reported
    // for <file>", which reads as a clean verdict for a file nothing analysed.
    // batch-20261118 AC-2: the server produced NEITHER diagnostics NOR symbols
    // for this file. "No diagnostics — no problems reported" would be a clean
    // verdict nothing earned; the state names what actually happened.
    if (controller_->serverSilent()) {
        return QCoreApplication::translate(
            "VEdit",
            "Not analysed — the language server produced no symbols and no diagnostics "
            "for this file (no build context / no response / the session failed).");
    }
    const QString reason = controller_->realtimeReason();
    if (!reason.isEmpty()) {
        return QCoreApplication::translate("VEdit", "No diagnostics — %1.").arg(reason);
    }
    const QStringList missing = controller_->toolchainReport().missing();
    QString nextStep;
    if (!controller_->analyzeFileName().isEmpty()) {
        nextStep = QCoreApplication::translate("VEdit", "no problems reported for %1 — edit or save it to run the "
                                  "analysis again")
                       .arg(QFileInfo(controller_->analyzeFileName()).fileName());
    } else {
        nextStep = QCoreApplication::translate("VEdit", "open a file to start the analysis, or open a project folder "
                                  "and run Lint project");
    }
    if (!missing.isEmpty()) {
        return QCoreApplication::translate("VEdit", "No diagnostics — the diagnostics backend is missing (%1); %2.")
            .arg(missing.join(QStringLiteral(", ")), nextStep);
    }
    return QCoreApplication::translate("VEdit", "No diagnostics — %1.").arg(nextStep);
}

bool DiagnosticsPanel::diagnosticsEmptyStateVisible() const
{
    return diagnosticsEmpty_ != nullptr && diagnosticsEmpty_->isVisibleTo(this);
}

bool DiagnosticsPanel::hintVisible() const
{
    return hint_ != nullptr && hint_->isVisibleTo(this);
}

QString DiagnosticsPanel::hintText() const
{
    return hint_ != nullptr ? hint_->text() : QString();
}

QWidget *DiagnosticsPanel::outlineWidget() const
{
    return view_;
}

QWidget *DiagnosticsPanel::issuesWidget() const
{
    return issues_;
}

QWidget *DiagnosticsPanel::countsWidget() const
{
    return counts_;
}

DiagnosticsController *DiagnosticsPanel::controller() const
{
    return controller_;
}

void DiagnosticsPanel::refreshFromController()
{
    if (controller_ == nullptr) {
        if (status_ != nullptr) {
            status_->setText(QCoreApplication::translate("VEdit", "no diagnostics controller"));
        }
        return;
    }
    if (model_ != nullptr) {
        model_->setSymbols(controller_->outline());
    }
    if (status_ != nullptr) {
        const QString toolchain = controller_->toolchainReport().missing().isEmpty()
            ? QString()
            : controller_->toolchainReport().userMessage();
        const QString file = QFileInfo(controller_->analyzeFileName()).fileName();
        const SessionSnapshot &session = controller_->session();
        if (!file.isEmpty() && controller_->documentAnalysisAvailable()
            && session.state == BackendState::Failed) {
            status_->setText(
                QCoreApplication::translate("VEdit", "%1 — session FAILED (restarts %2): %3")
                    .arg(file).arg(session.restarts)
                    .arg(session.message.isEmpty()
                        ? QCoreApplication::translate("VEdit", "no reason was reported")
                        : session.message));
        } else if (!toolchain.isEmpty()) {
            status_->setText(toolchain);
        } else if (!file.isEmpty()) {
            const QString reason = controller_->realtimeReason();
            if (!reason.isEmpty()) {
                status_->setText(QCoreApplication::translate("VEdit", "%1 — %2").arg(file, reason));
            } else {
                const QDateTime at = controller_->lastAnalysisAt();
                status_->setText(
                    QCoreApplication::translate("VEdit", "%1 — analyzed %2 (%3): %4 diagnostic(s)")
                        .arg(file,
                             at.isValid() ? at.toString(QStringLiteral("HH:mm:ss"))
                                          : QCoreApplication::translate("VEdit", "pending"),
                             controller_->analyzeWasDirty()
                                 ? QCoreApplication::translate("VEdit", "editor buffer, not saved yet")
                                 : QCoreApplication::translate("VEdit", "saved file"),
                             QString::number(controller_->mergedDiagnosticCount())));
            }
        } else {
            const QString message = controller_->session().message;
            const int merged = controller_->mergedDiagnosticCount();
            // composeSessionMessage() already ends with "diagnostics N".
            // Appending the same merged count again printed it twice
            // ("diagnostics 2 — 2 diagnostic(s)"); only append when the message
            // carries no count of its own.
            status_->setText(
                message.contains(QStringLiteral("diagnostics "))
                    ? message
                    : QCoreApplication::translate("VEdit", "%1 — %2 diagnostic(s)")
                          .arg(message)
                          .arg(merged));
        }
    }
    if (emptyState_ != nullptr) {
        const QStringList missing = controller_->toolchainReport().missing();
        if (model_ != nullptr && model_->rowCount() > 0) {
            emptyState_->clear();
            emptyState_->setVisible(false);
        } else if (!missing.isEmpty()) {
            emptyState_->setText(QCoreApplication::translate("VEdit", "No symbols — the outline backend is missing (%1). "
                                                "See the status line above for the one-line fix.")
                                     .arg(missing.join(QStringLiteral(", "))));
            emptyState_->setVisible(true);
        } else if (!controller_->realtimeReason().isEmpty()) {
            // L1.2: the same one-line reason reaches the outline surface, so a
            // `.f`/capped/excluded document does not look like "analysis is
            // starting" forever.
            emptyState_->setText(
                QCoreApplication::translate("VEdit", "No symbols — %1.")
                    .arg(controller_->realtimeReason()));
            emptyState_->setVisible(true);
        } else if (!controller_->analyzeFileName().isEmpty()) {
            emptyState_->setText(QCoreApplication::translate("VEdit", "No symbols yet for %1 — analysis is starting, "
                                                "or this file has no structure.")
                                     .arg(QFileInfo(controller_->analyzeFileName()).fileName()));
            emptyState_->setVisible(true);
        } else {
            emptyState_->setText(QCoreApplication::translate("VEdit", "No symbols yet — open a file; analysis starts "
                                                "automatically."));
            emptyState_->setVisible(true);
        }
    }
    rebuildDiagnostics();
}

void DiagnosticsPanel::rebuildDiagnostics()
{
    if (issues_ == nullptr) {
        return;
    }
    ++rebuildCount_;
    issues_->clear();
    issueRowCount_ = 0;
    errorCount_ = 0;
    warningCount_ = 0;
    informationCount_ = 0;

    QVector<DiagnosticEntry> errors;
    QVector<DiagnosticEntry> warnings;
    QVector<DiagnosticEntry> others;
    // The rows this rebuild renders: the hint below reads the SAME set, so the
    // sentence and the list can never disagree (AC-3).
    QVector<DiagnosticEntry> rows;
    if (controller_ != nullptr) {
        // R2: the panel renders the ONE merged view (project pass = upper,
        // per-file/LSP = lower), never both layers of the same file.
        //
        // batch-20261115 AC-3.2 + AC-4.3: this is the REALTIME surface. It lists
        // the ACTIVE document's LOWER (per-file/LSP) rows only:
        //   * the session covers the open documents' level-1 closures (AC-3.1), so
        //     a submodule's rows exist in the store but must not appear - or be
        //     jumped to - while another file is being edited;
        //   * the whole-project (upper) rows live in the dedicated Project Lint
        //     dock, so the two panels never show the same row (AC-4.3).
        // No analysed document (headless pass, no editor) keeps the historical
        // whole merged view.
        // F2 (Review B, High): NO whole-view fallback. The Outline is the
        // realtime surface and never shows an upper (whole-project) row: with no
        // analysed document it is EMPTY (its empty state names the reason), not a
        // window onto the project result - that result has its own dock.
        //
        // batch-20261116 L1.2: when the document in front of the user has NO
        // analysis path (`.f`/`.md`, above the byte cap, an empty buffer), the
        // panel reads the WATCHER's document - not the previous HDL file's - so
        // a stale row of the file the user just switched away from can never be
        // painted under the new document.
        const QString displayFile = controller_->documentAnalysisAvailable()
            ? controller_->realtimeDisplayFile()
            : controller_->documentAnalysisPath();
        rows = displayFile.isEmpty() ? QVector<DiagnosticEntry>{}
                                     : controller_->lowerRowsForFile(displayFile);
        for (const DiagnosticEntry &entry : rows) {
            switch (groupForSeverity(entry.severity)) {
            case 1:
                errors.append(entry);
                break;
            case 2:
                warnings.append(entry);
                break;
            default:
                others.append(entry);
                break;
            }
        }
    }
    const auto byLocation = [](const DiagnosticEntry &left, const DiagnosticEntry &right) {
        if (left.file != right.file) {
            return left.file < right.file;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.column != right.column) {
            return left.column < right.column;
        }
        return left.severity < right.severity;
    };
    std::sort(errors.begin(), errors.end(), byLocation);
    std::sort(warnings.begin(), warnings.end(), byLocation);
    std::sort(others.begin(), others.end(), byLocation);

    errorCount_ = static_cast<int>(errors.size());
    warningCount_ = static_cast<int>(warnings.size());
    informationCount_ = static_cast<int>(others.size());
    countsText_ = QCoreApplication::translate("VEdit", "Errors: %1 · Warnings: %2 · Information: %3")
                      .arg(errorCount_)
                      .arg(warningCount_)
                      .arg(informationCount_);
    if (counts_ != nullptr) {
        counts_->setText(
            QCoreApplication::translate("VEdit", "<span style='color:%1'><b>Errors: %2</b></span>"
                           "&nbsp;&nbsp;<span style='color:%3'><b>Warnings: %4</b></span>"
                           "&nbsp;&nbsp;<span style='color:%5'>Information: %6</span>")
                .arg(errorColor().name())
                .arg(errorCount_)
                .arg(warningColor().name())
                .arg(warningCount_)
                .arg(severityColor(3).name())
                .arg(informationCount_));
    }

    const auto addGroup = [this](const QString &title, const QVector<DiagnosticEntry> &entries,
                                 const QColor &color) {
        if (entries.isEmpty()) {
            return;
        }
        auto *group = new QTreeWidgetItem(issues_);
        group->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(entries.size()));
        QFont font = group->font(0);
        font.setBold(true);
        group->setFont(0, font);
        group->setForeground(0, QBrush(color));
        group->setData(0, IssueSeverityRole, -1); // group header, not a diagnostic
        for (const DiagnosticEntry &entry : entries) {
            auto *row = new QTreeWidgetItem(group);
            row->setText(0, issueRowText(entry));
            row->setForeground(0, QBrush(color));
            row->setData(0, kIssueFileRole, entry.file);
            row->setData(0, kIssueLineRole, entry.line);
            row->setData(0, kIssueColumnRole, entry.column);
            row->setData(0, IssueSeverityRole, entry.severity);
            row->setToolTip(0, QCoreApplication::translate("VEdit", "%1:%2:%3 — %4")
                                   .arg(entry.file)
                                   .arg(entry.line + 1)
                                   .arg(entry.column + 1)
                                   .arg(entry.message));
            ++issueRowCount_;
        }
        group->setExpanded(true);
    };
    addGroup(QStringLiteral("Errors"), errors, errorColor());
    addGroup(QStringLiteral("Warnings"), warnings, warningColor());
    addGroup(QStringLiteral("Information"), others, severityColor(3));

    issues_->setVisible(issueRowCount_ > 0);
    if (diagnosticsEmpty_ != nullptr) {
        if (issueRowCount_ > 0) {
            diagnosticsEmpty_->clear();
            diagnosticsEmpty_->setVisible(false);
        } else {
            diagnosticsEmpty_->setText(diagnosticsEmptyStateText());
            diagnosticsEmpty_->setVisible(true);
        }
    }
    if (hint_ != nullptr) {
        // AC-3: refreshed from the same rows this rebuild rendered, and keyed on
        // the backend that produced them. An empty answer clears the label, so a
        // stale sentence never outlives its rows (edit, undo, backend switch).
        const QString hint = controller_ != nullptr
            ? veriblePreprocessHint(controller_->session().backendName, rows)
            : QString();
        hint_->setText(hint);
        hint_->setVisible(!hint.isEmpty());
    }
}

void DiagnosticsPanel::jumpToOutline(const QModelIndex &index)
{
    if (model_ == nullptr || controller_ == nullptr || !index.isValid()) {
        return;
    }
    const OutlineSymbol *symbol = model_->symbolAt(index.row());
    if (symbol == nullptr) {
        return;
    }
    jumpTo(controller_->analyzeFileName(), symbol->line, symbol->column);
}

void DiagnosticsPanel::jumpToIssue(QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    const QVariant file = item->data(0, kIssueFileRole);
    if (!file.isValid()) {
        return; // a group header, not a diagnostic row
    }
    jumpTo(file.toString(), item->data(0, kIssueLineRole).toInt(),
           item->data(0, kIssueColumnRole).toInt());
}

void DiagnosticsPanel::jumpTo(const QString &filePath, int zeroBasedLine, int zeroBasedColumn)
{
    if (filePath.isEmpty() || zeroBasedLine < 0) {
        return;
    }
    // The contract is 1-based; with no registered target the registry is an
    // explicit no-op (a panel standing alone never crashes on a double click).
    vedit::workspace::jump::jumpTo(filePath, zeroBasedLine + 1, zeroBasedColumn + 1);
}

QWidget *createOutlinePanel(DiagnosticsController *controller)
{
    return new DiagnosticsPanel(controller);
}

} // namespace vedit::workspace::diagnostics
