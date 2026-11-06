#pragma once

// item 12 + R3: how a finished project pass is PRESENTED.
//
// The ruling is one sentence: the modal dialog is shown ONLY for an explicit
// `project.lint` (menu/toolbar); an automatic (save-triggered) pass gets a
// CLICKABLE status-bar notice instead. Both surfaces carry the same rows, and
// every row jumps to the offending location, so "elaboration failed, now what"
// always has a next step.
//
// The policy lives in this widget-free-of-the-controller class precisely so the
// headless self-test can drive both branches and count the dialogs.

#include "diagnostics_types.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <QDialog>
#include <QLabel>

class QStatusBar;
class QWidget;

namespace vedit::workspace::diagnostics {

class ProjectLintNotice final : public QObject {
    Q_OBJECT
public:
    enum class Mode {
        Automatic, ///< save-triggered: notice only, NEVER a modal dialog (R3)
        Explicit,  ///< the user ran Lint Project: the modal jumpable list
    };

    ProjectLintNotice(QWidget *window, QStatusBar *statusBar, QObject *parent = nullptr);
    ~ProjectLintNotice() override;

    /// Present the error rows of a finished pass. `Automatic` never creates a
    /// dialog; `Explicit` always does (and also updates the notice).
    void present(const QVector<DiagnosticEntry> &errors, Mode mode);
    void clear();

    int dialogsShown() const { return dialogsShown_; }
    int noticesShown() const { return noticesShown_; }
    /// Clicks on the automatic notice that produced a jump (item 12's
    /// "clickable notice": the click IS the jump).
    int noticeJumps() const { return noticeJumps_; }
    bool noticeVisible() const;
    QString noticeText() const;
    /// The open dialog (nullptr when none). The self-test reads its rows and
    /// closes it; the product never calls this.
    QDialog *dialog() const { return dialog_; }
    /// The rows the last presentation listed.
    QVector<DiagnosticEntry> rows() const { return rows_; }

signals:
    /// One row was activated (dialog double click, or a click on the notice).
    void jumpRequested(const QString &file, int line, int column);

private:
    void showDialog();
    void activateNotice();
    void jumpTo(int index);

    QWidget *window_ = nullptr;
    QStatusBar *statusBar_ = nullptr;
    // Both widgets are CHILDREN of the shell's status bar / window, so the shell
    // may destroy them before this object: the pointers are guarded, and the
    // destructor never deletes a widget it does not own.
    QPointer<QLabel> notice_;
    QPointer<QDialog> dialog_;
    QVector<DiagnosticEntry> rows_;
    int dialogsShown_ = 0;
    int noticesShown_ = 0;
    int noticeJumps_ = 0;
    int nextNoticeRow_ = 0;
};

} // namespace vedit::workspace::diagnostics
