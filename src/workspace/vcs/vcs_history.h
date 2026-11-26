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

#include <QSet>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
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

    void setSource(std::unique_ptr<VcsSource> source, bool deferRefresh = false);
    void setRoot(const std::string &root);
    std::string root() const { return editorHistoryRoot_.empty() ? root_ : editorHistoryRoot_; }

    static constexpr int kDefaultPageSize = 50;

    // --- user actions (probe-callable) -------------------------------------
    void refresh();   // the first page
    void loadPage(int offset);
    void search(const QString &query);
    void nextPage();
    void previousPage();
    void selectCommit(const QString &hash); // loads the detail
    void openFileHistory(const QString &path, std::function<bool()> stillCurrent = {});
    void openEditorFileHistory(const std::string &root, const QString &path, std::function<bool()> isActive);
    void editorFileChanged();
    bool hasEditorFileHistory() const { return !editorHistoryRoot_.empty(); }
    bool editorFileHistoryCurrent() const;
    void setSavedRepositoriesFile(const QString &path);
    void showBlame(const QString &path);
    void compareSelected();
    quint64 svnSelectionEpoch() const { return svnSelectionEpoch_; }

    Q_SIGNAL void svnSnapshotRequested(const QString &path, quint64 revision, quint64 peg);
    Q_SIGNAL void svnWorkingFileRequested(const QString &path);
    Q_SIGNAL void svnBaseComparisonRequested(const QString &path, quint64 revision, quint64 peg);
    Q_SIGNAL void svnRemoteFileRequested(const QString &url, const QString &path, quint64 revision, bool deleted, bool compare);
    Q_SIGNAL void svnComparisonRequested(const QString &path, const QList<quint64> &revisions,
                                         quint64 peg, bool workingCopy);

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
    void compareSvnSelection(bool workingCopy);
    void promptSvnRevisions();
    void clearEditorFileHistory();

    Backend backend_ = Backend::None;
    quint64 svnSelectionEpoch_ = 0;
    quint64 fileRequestEpoch_ = 0;
    quint64 svnPeg_ = kDefaultSvnPeg;
    QString svnHistoryPath_;
    QString svnDetailRevision_;
    QString svnDetailRepository_;
    QString svnCheckoutPrefix_;
    QStringList svnRevisionFiles_;
    QSet<QString> svnDeletedRevisionFiles_;
    std::function<bool()> historySelectionGuard_;
    std::string editorHistoryRoot_;
    std::function<bool()> editorFileIsActive_;
    QString savedRepositoriesFile_;
    QComboBox *repository_ = nullptr;
    QPushButton *addRepository_ = nullptr;
    QPushButton *removeRepository_ = nullptr;
    void addSavedRepository();
    void removeSavedRepository();
    void storeSavedRepositories();
    QPushButton *svnWorkingButton_ = nullptr;
    QPushButton *svnRevisionsButton_ = nullptr;
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
    quint64 historyPageEpoch_ = 0;
    quint64 detailEpoch_ = 0;
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
