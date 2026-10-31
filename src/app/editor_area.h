#pragma once

// Split editor area of the application shell.
//
// The area owns a binary tree of editor *panes* (the product editor widget is
// supplied by the EditorProvider seam). It implements the shell's layout
// requirements: horizontal and vertical splits, focus switching, closing and
// re-layout, and a JSON layout model that the session store persists.
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <Qt>

#include <functional>
#include <memory>

class QEvent;
class QLabel;
class QMouseEvent;
class QVBoxLayout;

namespace vedit::app {

class EditorProvider;

// One editor pane: a header line (pane id + active marker) and one editor
// widget created by the provider. The pane asks for activation on click and on
// focus-in of the editor widget.
class EditorPane final : public QWidget {
public:
    EditorPane(QString paneId, EditorProvider *provider, QWidget *parent = nullptr);

    QString paneId() const;
    QWidget *editorWidget() const;
    void setActive(bool active);
    bool isActive() const;
    void setOnFocusRequested(std::function<void(EditorPane *)> callback);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateHeader();

    QString paneId_;
    EditorProvider *provider_ = nullptr;
    QLabel *header_ = nullptr;
    QWidget *editor_ = nullptr;
    std::function<void(EditorPane *)> onFocusRequested_;
    bool active_ = false;
};

// The split container. Invariants:
//   * at least one pane exists while the area is visible (ensurePane());
//   * every structural change rebuilds the QSplitter tree from the layout model
//     so the model is the single source of truth (and the session JSON).
class EditorArea final : public QWidget {
public:
    explicit EditorArea(QWidget *parent = nullptr);
    ~EditorArea() override;

    void setProvider(EditorProvider *provider);
    EditorProvider *provider() const;

    EditorPane *ensurePane();
    QList<EditorPane *> panes() const;
    int paneCount() const;
    EditorPane *activePane() const;
    void setActivePane(EditorPane *pane);

    // Splits the active pane. Qt::Horizontal puts the new pane to the right,
    // Qt::Vertical puts it below; the new pane becomes active (returned).
    EditorPane *splitActive(Qt::Orientation orientation);
    bool closeActivePane();
    void focusNextPane();
    void resetLayout();

    QJsonObject saveLayout() const;
    // Replaces the current tree. Returns false (with *error) only for a
    // malformed/unsupported document; provider mismatches are diagnostics.
    bool restoreLayout(const QJsonObject &layout, QString *error);
    QStringList diagnostics() const;

    void setOnLayoutChanged(std::function<void()> callback);
    void setOnActivePaneChanged(std::function<void(EditorPane *)> callback);

private:
    struct Node;

    EditorPane *createPane(const QString &paneId = QString());
    Node *newLeaf(EditorPane *pane);
    Node *findLeaf(Node *node, const EditorPane *pane) const;
    Node *findParent(Node *node, const Node *child) const;
    std::unique_ptr<Node> *ownerSlot(Node *child);
    void collectPanes(const Node *node, QList<EditorPane *> *out) const;
    QJsonObject nodeToJson(const Node *node) const;
    Node *nodeFromJson(const QJsonObject &json, QList<EditorPane *> *created, QString *error);
    QWidget *buildWidget(Node *node);
    void rebuild();
    void clearTree();
    void deletePane(EditorPane *pane);

    QVBoxLayout *layout_ = nullptr;
    QWidget *content_ = nullptr;
    std::unique_ptr<Node> root_;
    EditorProvider *provider_ = nullptr;
    EditorPane *active_ = nullptr;
    int nextPaneNumber_ = 1;
    std::function<void()> onLayoutChanged_;
    std::function<void(EditorPane *)> onActivePaneChanged_;
    QStringList diagnostics_;
    bool rebuilding_ = false;
};

} // namespace vedit::app
