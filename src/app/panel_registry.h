#pragma once

// Panel seam of the application shell.
//
// The shell owns the QMainWindow and the docks; the six panels the product
// needs (file tree, search, terminal, VCS, settings, outline) are registered
// here as *specs* and inserted as docks only when asked for. Panel *content*
// belongs to the owning lane: each spec carries a factory, and until a lane
// registers one the shell inserts a clearly-labelled placeholder (empty state,
// never a crash). src/workspace, src/tools, src/vcs and src/settings replace
// the factories through ApplicationShell/PanelRegistry::setFactory().
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <Qt>

#include <functional>

class QDockWidget;
class QMainWindow;
class QWidget;

namespace vedit::app {

struct PanelSpec {
    QString id;                       // stable id, e.g. "file-tree"
    QString title;                    // dock title
    QString owner;                    // lane that owns the content
    Qt::DockWidgetArea area = Qt::LeftDockWidgetArea;
    std::function<QWidget *()> factory; // empty => shell placeholder
};

class PanelRegistry {
public:
    void addPanel(PanelSpec spec);
    bool contains(const QString &id) const;
    QStringList ids() const;                 // registration order
    QStringList insertedIds() const;         // docks that currently exist
    void setFactory(const QString &id, std::function<QWidget *()> factory);
    void setPlaceholderHint(const QString &id, const QString &hint);

    // Creates and inserts the dock on first request; later calls return the
    // existing dock. `window` is remembered for show/toggle operations.
    QDockWidget *insert(const QString &id, QMainWindow *window);
    QDockWidget *dock(const QString &id) const;
    bool isInserted(const QString &id) const;
    bool isVisible(const QString &id) const;
    void showPanel(const QString &id, QMainWindow *window);
    void hidePanel(const QString &id);
    // Inserts if needed, otherwise flips visibility. Returns the new visibility.
    bool toggle(const QString &id, QMainWindow *window);
    QList<QDockWidget *> docks() const;
    Qt::DockWidgetArea area(const QString &id) const;

private:
    QWidget *createContent(const PanelSpec &spec) const;

    QHash<QString, PanelSpec> specs_;
    QHash<QString, QString> hints_;
    QHash<QString, QDockWidget *> docks_;
    QStringList order_;
    QMainWindow *window_ = nullptr;
};

} // namespace vedit::app
