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
class QTimer;
class QLabel;
class QMouseEvent;
class QToolButton;
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
    void setOnCloseRequested(std::function<void(EditorPane *)> callback);
    // T130: the pane's own close button must not discard an unsaved document.
    bool isModified() const;

    // Replaces the pane's editor widget with one created by `provider`, keeping
    // the pane and the split layout. Needed because the shell builds its first
    // pane before main() installs the real editor provider (edit-core).
    void setProvider(EditorProvider *provider);
    EditorProvider *provider() const;
    bool openFile(const QString &absolutePath, QString *error = nullptr);
    QString currentFilePath() const;
    bool saveFile(QString *error = nullptr);

    // T94: the breadcrumb bar's current text (empty while hidden).
    QString breadcrumbText() const;

    // T90/F-2: re-apply the single-authority invariant for Ctrl+S on this pane's
    // editor widget (an editor may re-inject its own standard save shortcut at
    // runtime). Returns the number of duplicate bindings removed.
    int enforceSingleCtrlSAuthority();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateHeader();
    void updateBreadcrumb();
    void replaceEditorWidget(QWidget *editor);

    QString paneId_;
    EditorProvider *provider_ = nullptr;
    QLabel *header_ = nullptr;
    QLabel *breadcrumb_ = nullptr;
    QWidget *editor_ = nullptr;
    QVBoxLayout *layout_ = nullptr;
    std::function<void(EditorPane *)> onFocusRequested_;
    std::function<void(EditorPane *)> onCloseRequested_;
    QToolButton *closeButton_ = nullptr;
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
    // T130: close a specific pane (the per-pane X button); returns false when
    // the close was blocked (unsaved modifications).
    bool closePane(EditorPane *pane);
    void focusNextPane();
    void resetLayout();

    QJsonObject saveLayout() const;
    // Replaces the current tree. Returns false (with *error) only for a
    // malformed/unsupported document; provider mismatches are diagnostics.
    bool restoreLayout(const QJsonObject &layout, QString *error);
    QStringList diagnostics() const;

    void setOnLayoutChanged(std::function<void()> callback);
    void setOnActivePaneChanged(std::function<void(EditorPane *)> callback);
    // T130: returning false blocks the close (unsaved changes). The shell
    // installs this; no callback means "allow" for direct/unit use.
    void setOnClosePaneRequested(std::function<bool(EditorPane *)> callback);

    // T90/F-2: runtime guard for the T80 invariant. Editor/standard-action
    // re-application (KStandardShortcut re-apply, KDE system settings, a
    // provider that repopulates its action collection) can put a second Ctrl+S
    // binding back after the pane was created; this re-runs the enforcement on
    // every pane. Returns the number of duplicate bindings removed.
    int enforceSingleCtrlSAuthority();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    std::function<bool(EditorPane *)> onClosePaneRequested_;
    QStringList diagnostics_;
    bool rebuilding_ = false;
    QTimer *shortcutGuard_ = nullptr;
};

} // namespace vedit::app
