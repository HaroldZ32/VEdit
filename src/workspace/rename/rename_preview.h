#pragma once

// Rename UI (batch 20261110c, AC-T13.2–13.5): the per-item preview, the ONE
// confirmation that starts the atomic apply job, and the cancel paths.
//
// The dialog is a VIEW: it never touches the rename C ABI. The lane owns the
// `VdService` and the jobs; the dialog hands it plan/apply/cancel REQUESTS and
// receives typed result values. That split keeps the rendered surface assertable
// while the probe still drives the REAL product path (no fake seam).
//
// AC-N6/R2-1 honesty: the measured classes the engine refuses WHOLE are listed
// by renameUnsupportedClasses() and rendered at all times, and a refusal that
// reaches the dialog is shown with its engine code and full message (an
// ambiguous symbol lists EVERY declaration) — never swallowed, never turned
// into a text replacement.
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace vedit::workspace {

// One planned replacement site (rendered from the engine's `VdRenameHit`).
struct RenamePreviewHit {
    int line = 0;   // 1-based, as shown to the user
    int column = 0; // 1-based, as shown to the user
    QString before; // the source line before the edit (engine-trimmed)
    QString after;  // the same line after the edit
};

// The preview of one file (rendered from `VdRenameFilePlan`).
struct RenamePreviewFile {
    QString path;  // the engine's absolute path (what the apply job opens)
    QString label; // the path shown to the user (project-relative when inside it)
    bool truncated = false;
    QVector<RenamePreviewHit> hits;
};

// The whole preview (rendered from `VdRenamePlan`). `files` is the number of
// files the index PARSED (the engine's own field); the plan's own file count is
// entries.size().
struct RenamePreviewPlan {
    QString root;
    QString from;
    QString to;
    quint64 files = 0;
    quint64 occurrences = 0;
    QVector<RenamePreviewFile> entries;
    QStringList warnings;
};

// The classes this engine refuses WHOLE (AC-N6/R2-1, measured against the
// pinned slang AST dump). One line per class, each carrying the reason. The
// list is static because it is a property of the ENGINE, not of one symbol.
QStringList renameUnsupportedClasses();

// The ONE line of one preview entry: "<file>:<line>  <before>  ->  <after>".
QString renamePreviewRowText(const RenamePreviewFile &file, const RenamePreviewHit &hit);

// The dialog's phases. Every job result ends in exactly one terminal state.
enum class RenamePreviewState {
    Editing,   // the name field is editable; only Preview starts work
    Planning,  // a plan job runs; Cancel cancels it
    Ready,     // the per-item preview is rendered; Apply is the ONE confirmation
    Applying,  // the atomic apply job runs; Cancel cancels it
    Applied,   // terminal success
    Failed,    // terminal failure (engine refusal, or an apply that rolled back)
    Cancelled, // terminal cancel (plan or apply): zero writes
};

class RenamePreviewDialog final : public QDialog {
    Q_OBJECT

public:
    explicit RenamePreviewDialog(QWidget *parent = nullptr);

    // Requests the lane fulfils (the dialog never creates a job itself).
    using PlanRequest = std::function<void(const QString &from, const QString &to)>;
    using JobRequest = std::function<void()>;
    void setRequests(PlanRequest plan, JobRequest apply, JobRequest cancel);

    // The symbol under the cursor, handed in by the command's trigger.
    void setSymbol(const QString &from);
    QString symbol() const;

    // --- job lifecycle, called by the lane -------------------------------
    void beginPlan(); // clears the previous preview, enters Planning
    void beginApply(); // enters Applying (the ONE confirmation disabled here)
    void showPlan(const RenamePreviewPlan &plan);
    void showPlanRefused(const QString &code, const QString &message);
    void showPlanCancelled();
    void showApplyApplied(quint64 files, quint64 occurrences, bool verified,
                          bool cancelArrivedLate = false);
    void showApplyFailed(const QString &failedFile, bool rolledBack, const QString &message);
    void showApplyRefused(const QString &code, const QString &message);
    void showApplyCancelled(bool rolledBack, quint64 filesWritten);
    void showCancelRequested();

    // --- observable surface (the probe and the evidence read THESE) -------
    RenamePreviewState state() const;
    QString statusText() const;
    QString totalText() const;
    QString unsupportedText() const;
    QString hintText() const;
    QStringList renderedFiles() const; // the per-file group lines, in order
    QStringList renderedRows() const;  // the per-entry lines, in order
    bool cancelVisible() const;
    bool progressVisible() const;
    bool applyEnabled() const;
    bool previewEnabled() const;

    // Stable widget accessors (the probe drives the SAME widgets a user does).
    QLineEdit *nameEdit() const;
    QPushButton *previewButton() const;
    QPushButton *applyButton() const;
    QPushButton *cancelButton() const;
    QPushButton *closeButton() const;
    QTreeWidget *tree() const;
    QLabel *totalLabel() const;
    QLabel *statusLabel() const;
    QLabel *unsupportedLabel() const;
    QLabel *warningsLabel() const;
    QProgressBar *progressBar() const;

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    void setState(RenamePreviewState state);
    void refreshButtons();
    void clearPreview();
    void setStatus(const QString &text);
    void previewClicked();
    void applyClicked();
    void cancelClicked();

    QString symbol_;
    RenamePreviewState state_ = RenamePreviewState::Editing;
    PlanRequest planRequest_;
    JobRequest applyRequest_;
    JobRequest cancelRequest_;

    QLineEdit *nameEdit_ = nullptr;
    QLabel *header_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QLabel *total_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *unsupported_ = nullptr;
    QLabel *warnings_ = nullptr;
    QLabel *hint_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *preview_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *close_ = nullptr;
};

} // namespace vedit::workspace
