#pragma once

// Ctrl+P command palette with fuzzy file jump (task T22, A2b).
//
// The palette is an overlay popup, not a dock: Lapce's file picker opens on
// Ctrl+P, filters as the user types and opens the highlighted file on Enter.
// It owns no file discovery of its own - it filters the shared
// WorkspaceFileIndex handed to it by the caller (see file_index.h), so opening
// the palette over a 10k-file workspace never walks the disk.
//
// Accessibility / test surface: object names are stable ("palette-query",
// "palette-results", "palette-empty", "palette-status"), and the result list,
// empty state and activation are readable without synthesising mouse input.
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include "fuzzy_matcher.h"

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;

namespace vedit::workspace {

class WorkspaceFileIndex;

class CommandPalette final : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget *parent = nullptr);

    void setFileIndex(const WorkspaceFileIndex *index);
    const WorkspaceFileIndex *fileIndex() const;

    // Called with the absolute path of the activated file, after the palette
    // closes. The palette never opens files itself - the editor seam decides.
    void setOpenHandler(std::function<void(const QString &)> handler);

    void setMaxResults(int maxResults);
    int maxResults() const;

    void openPalette(const QString &initialQuery = QString());
    void closePalette();

    QString queryText() const;
    void setQueryText(const QString &text);
    int resultCount() const;
    QStringList visibleResults() const;
    int currentRow() const;
    void moveSelection(int delta);
    void activateCurrent();

    // Test/automation surface: the focused query field (key events are sent to
    // it, exactly like a user typing).
    QLineEdit *queryEdit() const;

    bool emptyStateVisible() const;
    QString emptyStateText() const;

    // Wall time of the last filter + list rebuild, in microseconds. The 10k
    // benchmark asserts this stays far below the 200 ms acceptance budget.
    qint64 lastFilterMicros() const;

    // "No matching files" — the explicit empty state required by the contract.
    static QString noResultsText();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshResults();
    void updateEmptyState();
    void updateStatus();
    void showOverParent();

    const WorkspaceFileIndex *index_ = nullptr;
    std::function<void(const QString &)> openHandler_;
    int maxResults_ = 100;
    QLineEdit *query_ = nullptr;
    QListWidget *results_ = nullptr;
    QLabel *empty_ = nullptr;
    QLabel *status_ = nullptr;
    QVector<FuzzyMatcher::Result> matches_;
    qint64 lastFilterMicros_ = 0;
};

} // namespace vedit::workspace
