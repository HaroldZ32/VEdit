#pragma once

// Git history view (batch 4b, AC-4.3): the commit list (paged + search), one
// commit's detail (files + patch), a single file's history, blame and a
// two-commit comparison.
//
// Every read goes through the asynchronous VcsSource seam, so no `git log` /
// `git show` / `git blame` ever runs on the GUI thread. The panel reports the
// same four states the working-tree panel does: loading, empty (no commits yet),
// error and ready.
#include "vcs_source.h"

#include <QWidget>

#include <functional>
#include <memory>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace vedit::workspace::vcs {

class VcsHistoryPanel final : public QWidget {
    Q_OBJECT

public:
    explicit VcsHistoryPanel(QWidget *parent = nullptr);
    ~VcsHistoryPanel() override;

    void setSource(std::unique_ptr<VcsSource> source);
    void setRoot(const std::string &root);
    std::string root() const { return root_; }

    static constexpr int kDefaultPageSize = 50;

    // --- user actions (probe-callable) -------------------------------------
    void refresh();   // the first page
    void loadPage(int offset);
    void search(const QString &query);
    void nextPage();
    void previousPage();
    void selectCommit(const QString &hash); // loads the detail
    void openFileHistory(const QString &path);
    void showBlame(const QString &path);
    void compareSelected(); // the two selected commits, in list order

    // --- probe-readable surface -------------------------------------------
    int commitCount() const;
    QString commitText(int row, int column) const;
    QStringList commitHashes() const;
    int commitRow(const QString &hash) const;
    QString pageLabel() const;
    QString searchText() const;
    QStringList tabTitles() const;
    QString detailText() const;
    QString fileHistoryText() const;
    QString blameText() const;
    QString compareText() const;
    QString messageText() const;
    QString emptyText() const;
    QString loadingText() const;
    QString errorText() const;
    bool emptyStateVisible() const;
    bool loadingStateVisible() const;
    bool errorStateVisible() const;

    int pageSize() const { return page_size_; }
    void setPageSize(int size);
    int offset() const { return offset_; }
    bool hasMore() const { return has_more_; }
    int requestCount() const { return request_count_; }
    // The last request the panel issued, so a probe can assert its exact shape
    // (kind, offset, query, hash, path, from/to) instead of guessing.
    const HistoryRequest &lastRequest() const { return last_request_; }

    // Replaces the modal-free default: the panel never shows dialogs; the path
    // for file history / blame comes from the line edit.

private:
    void buildUi();
    void issue(const HistoryRequest &request);
    void applyCommits(HistoryData data);
    void applyCommit(HistoryData data);
    void applyFileHistory(HistoryData data);
    void applyBlame(HistoryData data);
    void applyCompare(HistoryData data);
    void showState(const QString &message, bool error);
    void updateActions();
    void onSelectionChanged();

    std::unique_ptr<VcsSource> source_;
    std::string root_;
    int page_size_ = kDefaultPageSize;
    int offset_ = 0;
    bool has_more_ = false;
    int request_count_ = 0;
    int in_flight_ = 0;
    bool ready_ = false;
    bool empty_state_ = false;
    bool failed_ = false;
    HistoryRequest last_request_;
    QString current_query_;
    QString selected_hash_;

    quint64 generation_ = 0;
    QLabel *header_ = nullptr;
    QLabel *loading_ = nullptr;
    QLabel *page_ = nullptr;
    QLabel *message_ = nullptr;
    QLabel *empty_ = nullptr;
    QLabel *error_ = nullptr;
    QLineEdit *search_ = nullptr;
    QLineEdit *path_ = nullptr;
    QPushButton *search_button_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QPushButton *previous_button_ = nullptr;
    QPushButton *next_button_ = nullptr;
    QPushButton *file_button_ = nullptr;
    QPushButton *blame_button_ = nullptr;
    QPushButton *compare_button_ = nullptr;
    QSplitter *splitter_ = nullptr;
    QTreeWidget *commits_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QPlainTextEdit *detail_ = nullptr;
    QPlainTextEdit *file_history_ = nullptr;
    QPlainTextEdit *blame_ = nullptr;
    QPlainTextEdit *compare_ = nullptr;
};

} // namespace vedit::workspace::vcs
