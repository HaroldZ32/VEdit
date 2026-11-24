#pragma once

// batch-20261113 T3 — the status-subscription registry shared between the
// edit-core provider and the per-view breadcrumb bindings.
//
// Deliberately NOT a QObject. The provider owns it through a std::shared_ptr and
// every lambda stored in a view's child object captures only a std::weak_ptr to
// it plus the view pointer. That is what makes the real teardown order safe:
// `ApplicationShell` declares `provider_` BEFORE `editorArea_`
// (src/app/app_shell.h), so on window close the provider is destroyed first
// while the views (and their bindings) still exist. A lambda that runs after
// that point locks an expired weak_ptr and is a silent no-op instead of calling
// through a dangling provider pointer.
//
// The relay itself is not thread-safe; it is only touched from the GUI thread
// (subscription, notification, and Qt's destroyed signal).
#include <QHash>

#include <functional>

class QWidget;

namespace vedit::editor {

class StatusRelay
{
public:
    // Installs the pane's listener for this editor widget. An empty listener
    // removes the entry, so set(editor, {}) and remove(editor) agree.
    void set(QWidget *editor, std::function<void()> listener)
    {
        if (editor == nullptr) {
            return;
        }
        if (!listener) {
            remove(editor);
            return;
        }
        listeners_.insert(editor, std::move(listener));
    }

    void remove(QWidget *editor)
    {
        if (editor == nullptr) {
            return;
        }
        listeners_.remove(editor);
    }

    // Calls the listener registered for this widget, if any. The key is only
    // compared, never dereferenced.
    void notify(QWidget *editor) const
    {
        const auto it = listeners_.constFind(editor);
        if (it != listeners_.constEnd() && it.value()) {
            it.value()();
        }
    }

    // TEST SEAM (batch-20261113 T3): the number of live subscriptions. The
    // lifetime probe uses it to prove an unsubscribe and a destroyed view each
    // leave no dangling key; product code never reads it.
    int size() const { return static_cast<int>(listeners_.size()); }

private:
    QHash<QWidget *, std::function<void()>> listeners_;
};

} // namespace vedit::editor
