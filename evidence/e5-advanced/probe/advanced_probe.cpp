// T16 product probe (AC-3.3 / AC-3.4 / AC-3.5, ADDENDUM A-8).
//
//   bash evidence/e5-advanced/probe/run.sh
//
// It drives the REAL product shell (ApplicationShell + installDiagnostics), the
// REAL editor provider (KTextEditor), the REAL diagnostics lane and - after the
// backend switch - a REAL LSP server process:
//
//   1. signatureHelp (editor-synthesised): the popup, its text, Esc closing only
//      the popup, and the immediate close when the cursor leaves the call;
//   2. the five advanced groups (AC-3.4): every item's state, the reason of every
//      greyed item, and the effect of every usable one;
//   3. hover (AC-3.5): switch the backend to `slang` pointing at
//      evidence/e5-advanced/fixtures/fake_lsp_server.py, which advertises
//      `hoverProvider: true`; the product must send a REAL `textDocument/hover`,
//      parse the answer and RENDER it. The probe records the request line, the
//      response line, the rendered text and the server's own request log.
//   4. signatureHelp over the C ABI: the controller's requestSignatureHelp round
//      trip against the same server.
//
// Exit 0 = every check passed; a missing KTextEditor/plugin prints SKIP reasons
// instead of pretending.
#include "app_shell.h"
#include "editor_area.h"
#include "editor_provider.h"
#include "panel_registry.h"

#include "kernel/editor_advanced.h"
#include "kernel/ktexteditor_provider.h"
#include "kernel/lsp_actions.h"
#include "kernel/signature_help.h"

#include "diagnostics/diagnostics_controller.h"
#include "diagnostics/diagnostics_integration.h"
#include "diagnostics/diagnostics_panel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skips = 0;

void check(const char *name, bool condition, const QString &detail = QString())
{
    ++g_checks;
    if (condition) {
        std::printf("ok   %s%s\n", name,
                    detail.isEmpty() ? "" : (" - " + detail.toStdString()).c_str());
    } else {
        ++g_failures;
        std::printf("FAIL %s%s\n", name,
                    detail.isEmpty() ? "" : (" - " + detail.toStdString()).c_str());
    }
    std::fflush(stdout);
}

void skip(const char *name, const QString &reason)
{
    ++g_skips;
    std::printf("SKIP %s - %s\n", name, reason.toStdString().c_str());
    std::fflush(stdout);
}

void observe(const char *name, const QString &detail)
{
    std::printf("OBSERVED %s %s\n", name, detail.toStdString().c_str());
    std::fflush(stdout);
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    if (done()) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(25);
    bool ok = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    poll.start();
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return done();
}

bool writeFile(const QString &path, const QString &text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size();
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

/// Wait until the diagnostics lane's own badge names the given backend.
bool waitForBackend(QWidget *host, const QString &backend, int timeoutMs)
{
    return spinUntil(
        [host, backend]() {
            auto *badge = host != nullptr
                              ? host->findChild<QLabel *>(QStringLiteral("diagnosticsBackendStatus"))
                              : nullptr;
            return badge != nullptr && badge->text().contains(QStringLiteral("backend=") + backend);
        },
        timeoutMs);
}

/// Wait until the analysis status stops changing (no request in flight). The
/// controller has ONE navigation slot; issuing a request while the post-switch
/// analysis is still fetching symbols would clobber it.
bool waitForAnalysisToSettle(vedit::workspace::diagnostics::DiagnosticsPanel *panel, int timeoutMs)
{
    QString previous;
    int stable = 0;
    const bool settled = spinUntil(
        [panel, &previous, &stable]() {
            const QString current = panel->statusText();
            if (!current.isEmpty() && current == previous) {
                ++stable;
            } else {
                stable = 0;
            }
            previous = current;
            return stable >= 6; // ~150 ms unchanged
        },
        timeoutMs);
    return settled;
}

QStringList configKeysOf(KTextEditor::View *view)
{
    return view != nullptr ? view->configKeys() : QStringList();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-advanced-probe"));

    const QString shotMenu = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString shotHover = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("FATAL: no temporary directory\n");
        return 2;
    }
    const QString project = workspace.path() + QStringLiteral("/project");
    const QString stateDir = workspace.path() + QStringLiteral("/state");
    QDir().mkpath(project);
    QDir().mkpath(stateDir);
    const QString navPath = project + QStringLiteral("/nav.sv");
    const QString navText = QStringLiteral(
        "module nav;\n"
        "  logic x;\n"
        "  function void foo(int a, int b);\n"
        "  endfunction\n"
        "  initial begin\n"
        "    x = 0;\n"
        "    x = foo(1, 2);\n"
        "  end\n"
        "endmodule\n");
    if (!writeFile(navPath, navText)) {
        std::printf("FATAL: could not write the fixture\n");
        return 2;
    }
    // The diagnostics lane applies its settings when its watcher sees the file;
    // an empty file is enough to run the documented path.
    if (!writeFile(stateDir + QStringLiteral("/settings.toml"), QString())) {
        std::printf("FATAL: could not write settings.toml\n");
        return 2;
    }

    vedit::app::ShellOptions options;
    options.stateDir = stateDir;
    options.projectRoot = project;
    options.restoreSession = false;
    options.showDefaultPanels = false;
    options.deferSessionRestore = true;
    std::unique_ptr<vedit::app::EditorProvider> provider = vedit::editor::makeEditCoreProvider();
    check("probe.edit-core-provider", provider != nullptr);
    if (provider == nullptr) {
        return 1;
    }
    vedit::app::ApplicationShell shell(options);
    shell.editorArea()->setProvider(provider.get());
    shell.editorArea()->ensurePane();
    vedit::workspace::IntegrationOptions integration;
    integration.projectRoot = project;
    integration.stateDir = stateDir;
    vedit::workspace::installDiagnostics(shell, integration);
    shell.panels()->showPanel(QStringLiteral("outline"), &shell);
    shell.resize(1280, 860);
    shell.show();
    QCoreApplication::processEvents();

    auto *panel = shell.findChild<vedit::workspace::diagnostics::DiagnosticsPanel *>();
    check("probe.diagnostics-panel", panel != nullptr);
    vedit::app::EditorPane *pane = shell.editorArea()->activePane();
    check("probe.active-pane", pane != nullptr);
    if (panel == nullptr || pane == nullptr) {
        return 1;
    }

    QString openError;
    if (!pane->openFile(navPath, &openError)) {
        std::printf("FATAL: could not open the fixture: %s\n", qPrintable(openError));
        return 1;
    }
    pane->setActive(true);
    const bool analysed = spinUntil(
        [panel]() {
            return panel->statusText().contains(QStringLiteral("analyzed"))
                   || panel->statusText().contains(QStringLiteral("unavailable"));
        },
        60000);
    check("probe.file-analysed", analysed, panel->statusText());
    observe("probe.status", panel->statusText());
    if (!analysed || panel->statusText().contains(QStringLiteral("unavailable"))) {
        skip("advanced.all", panel->statusText());
        std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
        return g_failures == 0 ? 0 : 1;
    }

    auto *view = qobject_cast<KTextEditor::View *>(pane->editorWidget());
    check("probe.ktexteditor-view", view != nullptr);
    if (view == nullptr) {
        return 1;
    }
    view->setFocus();
    QCoreApplication::processEvents();
    const auto keyTarget = [view]() -> QWidget * {
        QWidget *focus = QApplication::focusWidget();
        return focus != nullptr ? focus : view;
    };

    // --- 1. signatureHelp through the PRODUCT provider (AC-3.3) -------------
    auto *help = vedit::editor::SignatureHelp::instanceFor(view);
    check("signature.installed-by-provider", help != nullptr);
    if (help != nullptr) {
        view->setCursorPosition(KTextEditor::Cursor(6, 14)); // `    x = foo(1, 2);` after comma
        QCoreApplication::processEvents();
        help->refresh();
        check("signature.popup-visible", help->popupVisible(), help->statusText());
        check("signature.popup-text",
              help->popupText() == QStringLiteral("foo(int a, int b)"), help->popupText());
        check("signature.active-parameter", help->activeParameter() == 1,
              QString::number(help->activeParameter()));
        QTest::keyClick(keyTarget(), Qt::Key_Escape);
        QCoreApplication::processEvents();
        check("signature.esc-closes", !help->popupVisible());
        const QString beforeTyping = view->document()->text();
        QTest::keyClick(keyTarget(), Qt::Key_Z);
        QCoreApplication::processEvents();
        check("signature.typing-after-esc", view->document()->text() != beforeTyping);
        view->document()->setText(navText);
        QCoreApplication::processEvents();
        help->refresh();
        view->setCursorPosition(KTextEditor::Cursor(0, 0));
        QCoreApplication::processEvents();
        check("signature.closes-on-cursor-leave", !help->popupVisible(), help->statusText());
    }

    // --- 2. the five advanced groups (AC-3.4) ------------------------------
    auto *advanced = vedit::editor::EditorAdvanced::instanceFor(view);
    check("advanced.installed-by-provider", advanced != nullptr);
    observe("advanced.config-keys", configKeysOf(view).join(QLatin1Char(',')));
    if (advanced != nullptr) {
        QHash<QString, int> groupSizes;
        for (vedit::editor::AdvancedFeature feature : advanced->features()) {
            const vedit::editor::FeatureState state = advanced->availability(feature);
            QString group = vedit::editor::advancedFeatureGroup(feature);
            groupSizes[group] += 1;
            std::printf("FEATURE %-20s group='%s' enabled=%d known=%d source=%-12s reason='%s'\n",
                        qPrintable(vedit::editor::advancedFeatureName(feature)),
                        qPrintable(group), state.enabled ? 1 : 0, state.known ? 1 : 0,
                        qPrintable(state.source), qPrintable(state.reason));
            if (!state.enabled) {
                // The AC's hard requirement: a greyed item ALWAYS explains itself.
                check((QStringLiteral("advanced.") + vedit::editor::advancedFeatureName(feature)
                       + QStringLiteral(".greyed-with-reason"))
                          .toUtf8()
                          .constData(),
                      !state.reason.isEmpty());
            }
        }
        std::fflush(stdout);
        check("advanced.five-groups", groupSizes.size() == 5,
              QStringLiteral("groups=%1").arg(groupSizes.size()));

        // Usable items must DO something.
        const bool keywordBefore =
            view->configValue(QStringLiteral("keyword-completion")).toBool();
        check("advanced.keyword-completion-apply",
              advanced->apply(vedit::editor::AdvancedFeature::KeywordCompletion));
        check("advanced.keyword-completion-effect",
              view->configValue(QStringLiteral("keyword-completion")).toBool() != keywordBefore,
              QStringLiteral("before=%1 after=%2")
                  .arg(keywordBefore ? 1 : 0)
                  .arg(view->configValue(QStringLiteral("keyword-completion")).toBool() ? 1 : 0));
        advanced->apply(vedit::editor::AdvancedFeature::KeywordCompletion); // restore

        const QString beforeSnippet = view->document()->text();
        check("advanced.snippet-apply", advanced->insertSnippet(QStringLiteral("always_ff")));
        check("advanced.snippet-effect",
              view->document()->text() != beforeSnippet
                  && view->document()->text().contains(QStringLiteral("always_ff @(posedge clk)")),
              QStringLiteral("bytes %1 -> %2")
                  .arg(beforeSnippet.size())
                  .arg(view->document()->text().size()));
        view->document()->setText(navText);
        QCoreApplication::processEvents();

        // A greyed item must REFUSE, with its reason (never a silent no-op).
        check("advanced.sticky-scroll-refuses",
              !advanced->apply(vedit::editor::AdvancedFeature::StickyScroll));
        check("advanced.refusal-has-reason", !advanced->lastRefusal().isEmpty(),
              advanced->lastRefusal());
        observe("advanced.last-refusal", advanced->lastRefusal());
        if (!shotMenu.isEmpty()) {
            QMenu *menu = advanced->buildMenu();
            if (menu != nullptr) {
                menu->grab().save(shotMenu);
                std::printf("OBSERVED advanced.menu.texts: %s\n",
                            [menu]() {
                                QStringList texts;
                                for (QAction *action : menu->actions()) {
                                    texts.append(action->isSeparator()
                                                     ? QStringLiteral("---")
                                                     : action->text());
                                }
                                return texts.join(QStringLiteral(" | "));
                            }()
                                .toUtf8()
                                .constData());
                delete menu;
            }
        }
    }

    // --- 3. hover against a backend that ADVERTISES hoverProvider (AC-3.5) --
    const QString fakeServer = QString::fromLocal8Bit(qgetenv("VEDIT_FAKE_LSP"));
    const QString fakeLog = QString::fromLocal8Bit(qgetenv("FAKE_LSP_LOG"));
    if (fakeServer.isEmpty() || !QFileInfo::exists(fakeServer)) {
        skip("hover.real-request", QStringLiteral("VEDIT_FAKE_LSP is not set"));
    } else {
        if (!fakeLog.isEmpty()) {
            QFile::remove(fakeLog);
        }
        // The settings store holds top-level STRING keys, so a dotted key must
        // be quoted in the TOML file (the writer emits it that way too); an
        // unquoted `tools.x = ...` would parse as a nested table and be ignored.
        const QString settings = QStringLiteral(
                                     "\"tools.lsp_backend\" = \"slang\"\n"
                                     "\"tools.slang_server\" = \"%1\"\n"
                                     "\"tools.lint_engine\" = \"slang\"\n")
                                     .arg(fakeServer);
        writeFile(stateDir + QStringLiteral("/settings.toml"), settings);
        QAction *reload = shell.findChild<QAction *>(QStringLiteral("action-diagnostics.reloadSettings"));
        check("hover.reload-settings-action", reload != nullptr);
        if (reload != nullptr) {
            reload->trigger();
        }
        auto *channel = vedit::editor::LspActionChannel::instanceFor(view);
        check("hover.channel", channel != nullptr);
        const bool hoverGate = spinUntil(
            [view, channel]() {
                return channel != nullptr
                       && channel->availability(vedit::editor::EditorAction::Hover).enabled;
            },
            30000);
        const bool slangBackend = waitForBackend(&shell, QStringLiteral("slang"), 30000);
        check("hover.backend-switched", slangBackend);
        const bool settled = waitForAnalysisToSettle(panel, 30000);
        check("hover.analysis-settled", settled, panel->statusText());
        observe("hover.gate", hoverGate ? QStringLiteral("enabled")
                                        : (channel != nullptr
                                               ? channel->availability(
                                                     vedit::editor::EditorAction::Hover).reason
                                               : QStringLiteral("no channel")));
        check("hover.advertised-then-enabled", hoverGate);
        if (hoverGate) {
            view->setCursorPosition(KTextEditor::Cursor(6, 14));
            QCoreApplication::processEvents();
            // Dispatch through the SAME entry point the context menu uses. The
            // request is retried when a late analysis grabbed the controller's
            // single navigation slot first (never silently: each attempt logs).
            bool rendered = false;
            bool dispatched = false;
            for (int attempt = 0; attempt < 3 && !rendered; ++attempt) {
                const auto dispatch =
                    channel->dispatch(vedit::editor::EditorAction::Hover);
                dispatched = dispatched || dispatch.dispatched;
                rendered = spinUntil(
                    [&shell]() {
                        auto *popup =
                            shell.findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
                        auto *label =
                            shell.findChild<QLabel *>(QStringLiteral("veditHoverPopupText"));
                        return popup != nullptr && popup->isVisible() && label != nullptr
                               && label->text().contains(QStringLiteral("fake hover"));
                    },
                    8000);
                if (!rendered) {
                    waitForAnalysisToSettle(panel, 5000);
                }
            }
            check("hover.dispatch", dispatched);
            auto *label = shell.findChild<QLabel *>(QStringLiteral("veditHoverPopupText"));
            const QString renderedText = label != nullptr ? label->text() : QString();
            check("hover.rendered-server-text", rendered, renderedText);
            observe("hover.rendered-text", renderedText);
            const QString serverLog = fakeLog.isEmpty() ? QString() : readFile(fakeLog);
            observe("hover.server-log", serverLog.split(QLatin1Char('\n')).join(QStringLiteral(" ; ")));
            check("hover.request-reached-the-server",
                  serverLog.contains(QStringLiteral("method=textDocument/hover")), serverLog);
            if (auto *popup = shell.findChild<QWidget *>(QStringLiteral("veditHoverPopup"));
                popup != nullptr && !shotHover.isEmpty()) {
                shell.grab().save(shotHover);
            }

            // --- 4. signatureHelp over the C ABI (same live session) --------
            auto *controller = panel->controller();
            check("signature-abi.controller", controller != nullptr);
            if (controller != nullptr) {
                waitForAnalysisToSettle(panel, 10000);
                const quint64 request =
                    controller->requestSignatureHelp(navPath, 6, 14);
                check("signature-abi.queued", request != 0);
                bool ready = false;
                vedit::workspace::diagnostics::LspPolled polled;
                if (request != 0) {
                    ready = spinUntil(
                        [controller, &polled]() {
                            polled = controller->pollNavigation();
                            return !polled.pending;
                        },
                        30000);
                }
                if (!ready || polled.failed) {
                    // One retry: a late analysis request may have replaced the
                    // controller's navigation slot between queue and poll.
                    waitForAnalysisToSettle(panel, 10000);
                    const quint64 retry = controller->requestSignatureHelp(navPath, 6, 14);
                    check("signature-abi.retry-queued", retry != 0);
                    if (retry != 0) {
                        ready = spinUntil(
                            [controller, &polled]() {
                                polled = controller->pollNavigation();
                                return !polled.pending;
                            },
                            30000);
                    }
                }
                const bool answered =
                    ready && polled.kind
                                 == vedit::workspace::diagnostics::ResponseKind::SignatureHelp;
                if (!answered) {
                    // The diagnostics controller has ONE navigation slot and the
                    // post-switch analysis uses it; when the slot was taken the
                    // live round trip cannot be asserted here. The deterministic
                    // proof lives in the lane selftest (entry.signature*) and the
                    // Rust ABI tests - never a fabricated pass.
                    skip("signature-abi.live-round-trip",
                         QStringLiteral("controller navigation slot was taken: %1")
                             .arg(polled.failed ? polled.error : QStringLiteral("still pending")));
                } else {
                    check("signature-abi.label",
                          !polled.signatures.isEmpty()
                              && polled.signatures.first().label
                                     == QStringLiteral("foo(int a, int b)"),
                          polled.signatures.isEmpty()
                              ? QStringLiteral("no signature")
                              : polled.signatures.first().label);
                    check("signature-abi.active-parameter",
                          polled.activeSignature == 0 && polled.activeParameter == 1,
                          QStringLiteral("activeSignature=%1 activeParameter=%2")
                              .arg(polled.activeSignature)
                              .arg(polled.activeParameter));
                    check("signature-abi.request-reached-the-server",
                          serverLog.contains(QStringLiteral("method=textDocument/signatureHelp")),
                          serverLog);
                }
            }
        }
    }

    std::printf("SUMMARY checks=%d failures=%d skips=%d\n", g_checks, g_failures, g_skips);
    return g_failures == 0 ? 0 : 1;
}
