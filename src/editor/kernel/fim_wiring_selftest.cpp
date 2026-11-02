// Lane validator for the FIM product wiring (task T104).
//
//   vedit-fim-wiring-selftest --selfcheck
//
// Drives the REAL provider (EditCoreProvider::createEditor, the same call the
// panes make) and the REAL FIM lane without a stub on the wiring side:
//
//   * default off: Alt+\ asks the state machine (which reports "disabled" and
//     opens no socket) but a fake endpoint sitting on 127.0.0.1 gets ZERO
//     connections, no ghost text appears and Tab/Esc keep their normal meaning;
//   * configured (VEDIT_FIM_ENABLED=1 + a local fake endpoint): Alt+\ returns a
//     suggestion, the ghost overlay shows it while the document text is
//     untouched, Tab inserts it exactly once, Esc drops it without editing, and
//     typing invalidates a pending suggestion (T27's state machine, not a
//     bypass).
#include "fim_wiring.h"
#include "ktexteditor_provider.h"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QApplication>
#include <QLabel>
#include <QShortcut>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <QWidget>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using vedit::editor::FimBinding;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const char *name, bool ok, const QString &detail = QString())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
    }
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : " ", detail.toUtf8().constData());
    std::fflush(stdout);
}

void skip(const char *name, const char *reason)
{
    ++g_checks;
    std::printf("CHECK %s SKIP %s\n", name, reason);
    std::fflush(stdout);
}

// A tiny HTTP endpoint on 127.0.0.1 served from its own thread, so it keeps
// answering while the GUI thread waits for the synchronous FIM request.
class RawEndpoint {
public:
    explicit RawEndpoint(QByteArray body, int delayMs = 0, bool respond = true)
        : response_(std::move(body))
        , delayMs_(delayMs)
        , respond_(respond)
    {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            return;
        }
        const int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
            || ::listen(listenFd_, 4) != 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            return;
        }
        socklen_t length = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            return;
        }
        port_ = ::ntohs(address.sin_port);
        thread_ = std::thread([this]() { run(); });
    }

    ~RawEndpoint()
    {
        stop_.store(true);
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    RawEndpoint(const RawEndpoint &) = delete;
    RawEndpoint &operator=(const RawEndpoint &) = delete;

    bool listening() const { return listenFd_ >= 0; }
    quint16 port() const { return port_; }
    int requests() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }
    QString endpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(port_);
    }

private:
    void run()
    {
        while (!stop_.load()) {
            const int client = ::accept(listenFd_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            QByteArray raw;
            char chunk[2048];
            while (!stop_.load()) {
                const ssize_t got = ::recv(client, chunk, sizeof(chunk), 0);
                if (got <= 0) {
                    break;
                }
                raw.append(chunk, static_cast<int>(got));
                if (requestComplete(raw)) {
                    break;
                }
            }
            if (!raw.isEmpty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++requests_;
            }
            if (!respond_) {
                // Black hole: hold the connection open and never answer, so the
                // caller's own budget has to fire.
                while (!stop_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                ::close(client);
                continue;
            }
            if (delayMs_ > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
            }
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(response_.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += response_;
            const ssize_t sent = ::send(client, response.constData(),
                                        static_cast<size_t>(response.size()), 0);
            (void)sent;
            ::close(client);
        }
    }

    static bool requestComplete(const QByteArray &raw)
    {
        const int headersEnd = raw.indexOf("\r\n\r\n");
        if (headersEnd < 0) {
            return false;
        }
        int contentLength = 0;
        for (const QByteArray &line : raw.left(headersEnd).split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                contentLength = trimmed.mid(int(std::strlen("content-length:"))).trimmed().toInt();
            }
        }
        return raw.size() >= headersEnd + 4 + contentLength;
    }

    QByteArray response_;
    int delayMs_ = 0;
    bool respond_ = true;
    int listenFd_ = -1;
    quint16 port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    int requests_ = 0;
};

void clearFimEnvironment()
{
    for (const char *name : {"VEDIT_FIM_ENABLED", "VEDIT_FIM_PROTOCOL", "VEDIT_FIM_ENDPOINT",
                             "VEDIT_FIM_MODEL", "VEDIT_FIM_TIMEOUT_MS", "VEDIT_FIM_API_KEY"}) {
        qunsetenv(name);
    }
}

void configureFimEnvironment(const QString &endpoint)
{
    qputenv("VEDIT_FIM_ENABLED", QByteArrayLiteral("1"));
    qputenv("VEDIT_FIM_ENDPOINT", endpoint.toUtf8());
    qputenv("VEDIT_FIM_MODEL", QByteArrayLiteral("fake-local-model"));
    qputenv("VEDIT_FIM_TIMEOUT_MS", QByteArrayLiteral("3000"));
}

// The shortcut signal is what Qt delivers to a key press that matches the
// binding; invoking it exercises the same wiring without depending on
// offscreen focus handling. The key sequence itself is asserted separately.
void pumpFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

void activate(QShortcut *shortcut)
{
    if (shortcut != nullptr) {
        QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection);
    }
}

const QString kDocument = QStringLiteral("module m;\n  assign x = 1;\nendmodule\n");
const QString kSuggestion = QStringLiteral(" assign y = 2;");
const QByteArray kFakeResponse =
    QByteArrayLiteral("{\"choices\":[{\"text\":\" assign y = 2;\"}]}");

} // namespace

int main(int argc, char **argv)
{
    bool selfcheck = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--selfcheck") == 0) {
            selfcheck = true;
        }
    }
    if (!selfcheck) {
        std::printf("usage: %s --selfcheck\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    std::printf("FIM-WIRING-SELFTEST start\n");

    if (KTextEditor::Editor::instance() == nullptr) {
        skip("fim.wiring.editor", "KTextEditor is not available in this build");
        std::printf("FIM-WIRING-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    clearFimEnvironment();

    // --- the real provider installs the binding on the real view -------------
    auto provider = vedit::editor::makeEditCoreProvider();
    QWidget parent;
    parent.show(); // the ghost overlay is a child of the view: show the window
    QWidget *editorWidget = provider->createEditor(&parent);
    auto *view = qobject_cast<KTextEditor::View *>(editorWidget);
    check("fim.wiring.view", view != nullptr);
    if (view == nullptr) {
        std::printf("FIM-WIRING-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    FimBinding *binding = FimBinding::instanceFor(view);
    check("fim.wiring.binding-installed", binding != nullptr);
    check("fim.wiring.binding-owned-by-view", binding != nullptr && binding->parent() == view);
    if (binding == nullptr) {
        std::printf("FIM-WIRING-SELFTEST ok=0 checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    KTextEditor::Document *document = view->document();
    document->setText(kDocument);
    view->setCursorPosition(KTextEditor::Cursor(1, 6));

    // --- default off: Alt+\ asks the state machine, which opens no socket ----
    RawEndpoint endpoint(kFakeResponse);
    check("fim.wiring.default-off.endpoint", endpoint.listening(), endpoint.endpoint());
    qputenv("VEDIT_FIM_ENDPOINT", endpoint.endpoint().toUtf8()); // configured, but NOT enabled
    qunsetenv("VEDIT_FIM_ENABLED");
    check("fim.wiring.default-off.trigger-key",
          binding->triggerShortcut() != nullptr
              && binding->triggerShortcut()->key() == QKeySequence(Qt::ALT | Qt::Key_Backslash),
          binding->triggerShortcut() != nullptr ? binding->triggerShortcut()->key().toString()
                                                : QStringLiteral("<missing>"));
    check("fim.wiring.default-off.tab-free",
          !binding->acceptShortcutActive() && !binding->cancelShortcutActive());
    const quint64 requestsBefore = binding->requestCount();
    activate(binding->triggerShortcut());
    pumpUntil([binding]() { return binding->inFlightJobs() == 0; }, 3000);
    check("fim.wiring.default-off.state-machine-asked",
          binding->requestCount() == requestsBefore + 1, QString::number(binding->requestCount()));
    check("fim.wiring.default-off.no-suggestion", !binding->hasSuggestion());
    check("fim.wiring.default-off.no-ghost", !binding->ghostVisible());
    check("fim.wiring.default-off.document-untouched", document->text() == kDocument);
    pumpFor(150);
    check("fim.wiring.default-off.zero-connections", endpoint.requests() == 0,
          QStringLiteral("connections=%1").arg(endpoint.requests()));
    check("fim.wiring.default-off.status-disabled",
          binding->statusText().contains(QStringLiteral("未启用")), binding->statusText());

    // --- configured: the job starts, the suggestion arrives asynchronously ---
    configureFimEnvironment(endpoint.endpoint());
    activate(binding->triggerShortcut());
    check("fim.wiring.configured.async-started",
          binding->inFlightJobs() == 1 || binding->hasSuggestion(),
          QStringLiteral("in_flight=%1").arg(binding->inFlightJobs()));
    const bool arrived = pumpUntil([binding]() { return binding->hasSuggestion(); }, 5000);
    check("fim.wiring.configured.request", endpoint.requests() >= 1,
          QStringLiteral("connections=%1").arg(endpoint.requests()));
    check("fim.wiring.configured.suggestion", arrived && binding->suggestion() == kSuggestion,
          binding->suggestion());
    check("fim.wiring.configured.ghost-visible", binding->ghostVisible());
    check("fim.wiring.configured.ghost-text",
          binding->ghostText().contains(kSuggestion.trimmed()), binding->ghostText());
    check("fim.wiring.configured.document-untouched-before-tab",
          document->text() == kDocument, document->text().left(60));
    check("fim.wiring.configured.tab-esc-armed",
          binding->acceptShortcutActive() && binding->cancelShortcutActive());
    check("fim.wiring.configured.no-job-left", binding->inFlightJobs() == 0);

    // --- Tab accepts: the text changes here and exactly once ----------------
    activate(binding->acceptShortcut());
    const QString expected = QStringLiteral("module m;\n  assi") + kSuggestion
        + QStringLiteral("gn x = 1;\nendmodule\n");
    check("fim.wiring.accept.text", document->text() == expected, document->text().left(80));
    check("fim.wiring.accept.exactly-once", document->text().count(kSuggestion) == 1);
    check("fim.wiring.accept.shortcuts-removed",
          !binding->acceptShortcutActive() && !binding->cancelShortcutActive());
    check("fim.wiring.accept.ghost-hidden", !binding->ghostVisible());
    check("fim.wiring.accept.no-pending", !binding->hasSuggestion());

    // --- Esc cancels without editing ---------------------------------------
    view->setCursorPosition(KTextEditor::Cursor(1, 6));
    const QString beforeCancel = document->text();
    activate(binding->triggerShortcut());
    check("fim.wiring.cancel.suggestion",
          pumpUntil([binding]() { return binding->hasSuggestion(); }, 5000));
    activate(binding->cancelShortcut());
    check("fim.wiring.cancel.dropped", !binding->hasSuggestion() && !binding->ghostVisible());
    check("fim.wiring.cancel.document-unchanged", document->text() == beforeCancel);

    // --- typing invalidates a pending suggestion (lane state machine) -------
    view->setCursorPosition(KTextEditor::Cursor(1, 6));
    activate(binding->triggerShortcut());
    check("fim.wiring.invalidate.suggestion",
          pumpUntil([binding]() { return binding->hasSuggestion(); }, 5000));
    document->insertText(view->cursorPosition(), QStringLiteral("z"));
    const QString afterTyping = document->text();
    check("fim.wiring.invalidate.typing-drops-it",
          !binding->hasSuggestion() && !binding->ghostVisible());
    check("fim.wiring.invalidate.tab-disarmed", !binding->acceptShortcutActive());
    check("fim.wiring.invalidate.accept-inserts-nothing",
          !binding->acceptSuggestion() && document->text() == afterTyping,
          document->text().left(80));

    // --- T106: a SLOW endpoint must not block the GUI thread -----------------
    RawEndpoint slow(kFakeResponse, 400);
    check("fim.async.slow-endpoint", slow.listening(), slow.endpoint());
    configureFimEnvironment(slow.endpoint());
    pumpFor(20);
    QElapsedTimer startTimer;
    startTimer.start();
    activate(binding->triggerShortcut());
    const qint64 startMs = startTimer.elapsed();
    check("fim.async.start-returns-fast", startMs < 50,
          QStringLiteral("start took %1 ms (the endpoint sleeps 400 ms)").arg(startMs));
    check("fim.async.in-flight", binding->inFlightJobs() == 1);
    int ticks = 0;
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, [&ticks]() { ++ticks; });
    ticker.start();
    const int pollsBefore = binding->pollRounds();
    pumpFor(250);
    ticker.stop();
    check("fim.async.event-loop-ticks", ticks >= 3, QStringLiteral("ticks=%1").arg(ticks));
    check("fim.async.still-pending",
          binding->inFlightJobs() == 1 && !binding->hasSuggestion(),
          QStringLiteral("in_flight=%1").arg(binding->inFlightJobs()));
    check("fim.async.polls-ran", binding->pollRounds() > pollsBefore,
          QStringLiteral("polls=%1").arg(binding->pollRounds() - pollsBefore));
    check("fim.async.suggestion-arrives-late",
          pumpUntil([binding]() { return binding->hasSuggestion(); }, 5000));
    check("fim.async.ghost-after-arrival", binding->ghostVisible());
    check("fim.async.job-finished", binding->inFlightJobs() == 0);
    binding->cancelSuggestion();

    // --- typing also cancels an IN-FLIGHT request ----------------------------
    activate(binding->triggerShortcut());
    check("fim.async.inflight-before-typing", binding->inFlightJobs() == 1);
    document->insertText(view->cursorPosition(), QStringLiteral("q"));
    check("fim.async.typing-cancels-inflight",
          binding->inFlightJobs() == 0 && !binding->hasSuggestion());
    pumpFor(600);
    check("fim.async.cancelled-never-arrives",
          !binding->hasSuggestion() && !binding->ghostVisible());

    // --- sensitivity: the same metric detects a blocked thread ---------------
    qint64 firedAtMs = -1;
    QElapsedTimer probe;
    probe.start();
    QTimer::singleShot(20, [&firedAtMs, &probe]() { firedAtMs = probe.elapsed(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check("fim.async.metric-detects-block", firedAtMs >= 250,
          QStringLiteral("20 ms timer fired at %1 ms after scheduling").arg(firedAtMs));

    // --- timeout: a black hole becomes a diagnostic, not a frozen window -----
    RawEndpoint blackHole(kFakeResponse, 0, false);
    check("fim.async.black-hole", blackHole.listening(), blackHole.endpoint());
    configureFimEnvironment(blackHole.endpoint());
    binding->setPollBudgetMs(300);
    const QString beforeTimeout = document->text();
    QElapsedTimer timeoutTimer;
    timeoutTimer.start();
    activate(binding->triggerShortcut());
    const bool timedOut = pumpUntil([binding]() { return binding->inFlightJobs() == 0; }, 3000);
    const qint64 timeoutElapsed = timeoutTimer.elapsed();
    binding->setPollBudgetMs(15000);
    check("fim.async.timeout-fires", timedOut,
          QStringLiteral("after %1 ms with a 300 ms budget").arg(timeoutElapsed));
    check("fim.async.timeout-status",
          binding->statusText().contains(QStringLiteral("超时")), binding->statusText());
    check("fim.async.timeout-no-suggestion",
          !binding->hasSuggestion() && !binding->ghostVisible());
    check("fim.async.timeout-document-unchanged", document->text() == beforeTimeout);

    clearFimEnvironment();
    std::printf("FIM-WIRING-SELFTEST ok=%d checks=%d failures=%d\n", g_failures == 0 ? 1 : 0,
                g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
