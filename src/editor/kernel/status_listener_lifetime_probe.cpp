// batch-20261113 T3 — setStatusListener lifetime probe.
//
// The four frozen defects (see .mpd/plans/batch-20261113-fim-detect.md §8 T3):
//   1. `setStatusListener(editor, nullptr)` returned right after removing the
//      registry entry, so the FoldBreadcrumbBinding kept forwarding into a pane
//      that had already unsubscribed;
//   2. the binding stored `[this, view]` / `[this, editor]` — a BARE provider
//      pointer in a child of the view;
//   3. the status-listener registry had no view-destroyed cleanup;
//   4. the shell destroys its provider BEFORE the editor area's views
//      (app_shell.h declares provider_ before editorArea_).
//
// One case per invocation, so ctest reports the three assertions separately:
//
//   status-listener-lifetime-probe after-unsubscribe-the-binding-has-no-listener
//   status-listener-lifetime-probe destroyed-view-leaves-no-registry-entry
//   status-listener-lifetime-probe relay-outlived-by-a-captured-lambda-is-a-no-op
//
// The first case is the RED one: on the pre-fix code the binding still holds a
// listener after `setStatusListener(editor, nullptr)`.
#include "fold_breadcrumb.h"
#include "ktexteditor_provider.h"
#include "status_relay.h"

#include <KTextEditor/Editor>

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

namespace {

int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.toUtf8().constData());
    std::fflush(stdout);
    if (!ok) {
        ++g_failures;
    }
}

QString countDetail(const char *what, int value)
{
    return QStringLiteral("%1=%2").arg(QString::fromUtf8(what)).arg(value);
}

// A real KTextEditor view from the provider, or nullptr when this build cannot
// create one; the probe then SKIPs (77) exactly like the other kernel probes.
QWidget *createProviderView(vedit::editor::EditCoreProvider &provider)
{
    if (KTextEditor::Editor::instance() == nullptr) {
        return nullptr;
    }
    QWidget *editor = provider.createEditor(nullptr);
    if (editor == nullptr || vedit::editor::viewFor(editor) == nullptr) {
        delete editor;
        return nullptr;
    }
    return editor;
}

int finish()
{
    if (g_failures == 0) {
        std::printf("STATUS-LISTENER-LIFETIME result ok=1 failures=0\n");
        return 0;
    }
    std::printf("STATUS-LISTENER-LIFETIME result ok=0 failures=%d\n", g_failures);
    return 1;
}

int skip(const char *reason)
{
    std::printf("SKIP %s\n", reason);
    std::fflush(stdout);
    return 77;
}

// Case 1 (RED): an unsubscribe must clear BOTH the registry entry and the
// forwarding lambda installed on the view's FoldBreadcrumbBinding.
int caseAfterUnsubscribe()
{
    vedit::editor::EditCoreProvider provider;
    QWidget *editor = createProviderView(provider);
    if (editor == nullptr) {
        return skip("no KTextEditor view available");
    }
    auto *binding = vedit::editor::FoldBreadcrumbBinding::instanceFor(editor);
    check("after-unsubscribe.the-view-has-a-binding", binding != nullptr);
    provider.setStatusListener(editor, []() {});
    if (binding != nullptr) {
        check("after-unsubscribe.the-binding-is-wired", binding->hasStatusListener());
    }
    provider.setStatusListener(editor, nullptr);
    if (binding != nullptr) {
        check("after-unsubscribe-the-binding-has-no-listener", !binding->hasStatusListener());
    }
    check("after-unsubscribe.the-registry-is-empty", provider.statusListenerCount() == 0,
          countDetail("registry", provider.statusListenerCount()));
    delete editor;
    return finish();
}

// Case 2: a view Qt destroys WITHOUT an unsubscribe leaves no registry entry.
int caseDestroyedView()
{
    vedit::editor::EditCoreProvider provider;
    QWidget *editor = createProviderView(provider);
    if (editor == nullptr) {
        return skip("no KTextEditor view available");
    }
    provider.setStatusListener(editor, []() {});
    check("destroyed-view.the-registry-has-the-entry", provider.statusListenerCount() == 1,
          countDetail("registry", provider.statusListenerCount()));
    delete editor;
    check("destroyed-view-leaves-no-registry-entry", provider.statusListenerCount() == 0,
          countDetail("registry", provider.statusListenerCount()));
    return finish();
}

// Case 3: the stored lambda outlives the relay — exactly the teardown order of
// app_shell.h (provider first, views later). It must be a silent no-op: no crash
// and no callback into the pane that was already torn down.
int caseRelayOutlived()
{
    int calls = 0;
    QWidget key; // stands in for the view pointer the stored lambda forwards
    auto relay = std::make_shared<vedit::editor::StatusRelay>();
    const std::weak_ptr<vedit::editor::StatusRelay> weakRelay = relay;
    relay->set(&key, [&calls]() { ++calls; });
    // The exact lambda shape EditCoreProvider stores in a view's binding: a weak
    // relay plus the view pointer, never the provider's `this`.
    const std::function<void()> stored = [weakRelay, view = &key]() {
        if (const std::shared_ptr<vedit::editor::StatusRelay> alive = weakRelay.lock()) {
            alive->notify(view);
        }
    };
    check("relay-outlived.the-registry-has-the-entry", relay->size() == 1,
          countDetail("registry", relay->size()));
    stored();
    check("relay-outlived.the-live-lambda-forwards", calls == 1, countDetail("calls", calls));
    relay.reset(); // what ~EditCoreProvider now does before the views die
    check("relay-outlived.the-weak-pointer-expired", weakRelay.expired());
    stored(); // must neither crash nor reach the destroyed listener
    check("relay-outlived-by-a-captured-lambda-is-a-no-op", calls == 1, countDetail("calls", calls));

    // The same order through the product seam: provider destroyed first, then
    // the view it configured. Pre-fix the binding's lambda held the provider.
    {
        auto provider = std::make_unique<vedit::editor::EditCoreProvider>();
        QWidget *editor = createProviderView(*provider);
        if (editor == nullptr) {
            return skip("no KTextEditor view available");
        }
        provider->setStatusListener(editor, []() {});
        provider.reset();
        delete editor;
    }
    return finish();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <case>\n", argc > 0 ? argv[0] : "probe");
        return 2;
    }
    QApplication app(argc, argv);
    const char *caseName = argv[1];
    if (std::strcmp(caseName, "after-unsubscribe-the-binding-has-no-listener") == 0) {
        return caseAfterUnsubscribe();
    }
    if (std::strcmp(caseName, "destroyed-view-leaves-no-registry-entry") == 0) {
        return caseDestroyedView();
    }
    if (std::strcmp(caseName, "relay-outlived-by-a-captured-lambda-is-a-no-op") == 0) {
        return caseRelayOutlived();
    }
    std::fprintf(stderr, "status-listener-lifetime-probe: unknown case '%s'\n", caseName);
    return 2;
}
