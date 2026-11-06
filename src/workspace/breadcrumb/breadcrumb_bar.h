#pragma once

// Clickable breadcrumb bar (acceptance AC-7.6: "面包屑各段可点击 → 跳到对应符号").
//
// The bar renders the semantic path of breadcrumb_model.h (outermost ->
// innermost) as one flat button per container; a click on a segment asks the
// application-wide jump contract (src/workspace/jump/jump_registry.h) to open
// the container's own file at its start line. It is a VIEW: it never parses,
// never opens a file itself, and a build without a registered jump target makes
// a click an explicit no-op (the registry reports NoTarget) instead of crashing.
//
// The bar is a drop-in replacement for the pane's breadcrumb label: same
// margins, hidden while the path is empty. The editor pane feeds it with
// `EditorPane::currentFilePath()` + the semantic path (the same entries the
// pane's text breadcrumb is built from).
#include "breadcrumb_model.h"

#include "jump/jump_registry.h"

#include <QString>
#include <QWidget>

#include <vector>

class QHBoxLayout;

namespace vedit::breadcrumb {

class Bar final : public QWidget {
    Q_OBJECT

public:
    explicit Bar(QWidget *parent = nullptr);
    ~Bar() override;

    Bar(const Bar &) = delete;
    Bar &operator=(const Bar &) = delete;

    // Semantic update for `filePath` (the file the cursor is in, absolute) and
    // the container path at the cursor (Entry::line is 0-based, as the syntax
    // source reports it). An empty path hides the bar.
    void setPath(const QString &filePath, const std::vector<Entry> &path);
    void clear();

    QString filePath() const;
    int segmentCount() const;
    // The clickable widget of a segment (for tests/automation); nullptr when the
    // index is out of range.
    QWidget *segmentWidget(int index) const;
    QString segmentText(int index) const;
    // The position a click on that segment asks for, 1-based (the contract's
    // convention); 0 when the index is out of range.
    int segmentLine(int index) const;

    // The same effect as clicking the segment: jump through the shared registry.
    // Returns true only when the target performed the jump.
    bool activateSegment(int index);

    // What the last activation did (evidence / a future status line).
    vedit::workspace::jump::JumpStatus lastStatus() const;
    int lastSegment() const;

private:
    void rebuild();
    void clearSegments();
    QString segmentLabel(const Entry &entry) const;

    QHBoxLayout *layout_ = nullptr;
    QString filePath_;
    std::vector<Entry> path_;
    std::vector<QWidget *> segments_;
    vedit::workspace::jump::JumpStatus lastStatus_ =
        vedit::workspace::jump::JumpStatus::NoTarget;
    int lastSegment_ = -1;
};

} // namespace vedit::breadcrumb
