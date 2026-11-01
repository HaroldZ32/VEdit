#pragma once

// VCS panel (T23): read-only view of the working tree.
//
// It shows the backend, the branch/revision, the changed files, the diff of the
// selected file, and the explicit empty state for a directory that is not a
// repository. It has NO write path: VcsSource exposes only info/status/diff.
#include "vcs_source.h"

#include <QString>
#include <QWidget>

#include <memory>

class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace vedit::workspace::vcs {

class VcsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit VcsPanel(QWidget *parent = nullptr);
    ~VcsPanel() override;

    void setSource(std::unique_ptr<VcsSource> source);
    void setRoot(const std::string &root);
    std::string root() const { return root_; }

    // Queries the source and re-renders. Safe with no source (renders the empty
    // state) and after the repository disappeared.
    void refresh();
    // Selects one changed file and renders its diff.
    void selectFile(const std::string &path);

    Backend backend() const { return backend_; }
    int fileCount() const;
    QString fileText(int row) const;
    QString headerText() const;
    QString emptyText() const;
    QString diffText() const;
    bool emptyStateVisible() const;

private:
    void showEmptyState(const Status &status);
    std::unique_ptr<VcsSource> source_;
    std::string root_;
    Backend backend_ = Backend::None;
    QLabel *header_ = nullptr;
    QLabel *empty_ = nullptr;
    QListWidget *files_ = nullptr;
    QPlainTextEdit *diff_ = nullptr;
};

} // namespace vedit::workspace::vcs
