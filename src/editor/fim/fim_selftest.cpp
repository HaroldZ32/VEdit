// Headless self-test of the editor-side FIM lane (task T27 / AC-S3.2).
//
//   vedit-fim-selftest [--default-off-only]
//
// Two hard properties are measured here, and the evidence file explains what
// each measurement can and cannot prove:
//
//   1. DEFAULT OFF: with no configuration the client reports the Rust side's
//      "not enabled" state and a loopback endpoint records ZERO connections.
//      (The stronger syscall-level proof — no socket()/connect() at all — is the
//      LD_PRELOAD run in evidence/slice3-fim/.)
//   2. CONFIGURED VIA RUST: with VEDIT_FIM_* pointing at a local fake endpoint,
//      the request is performed by the Rust service layer (`vd_fim_complete` ->
//      vd-fim -> ureq); this Qt process only LISTENS. The fake endpoint is a
//      local socket that answers one fixed JSON body — it is NOT a real model
//      endpoint, and the evidence says so.
//
// `--default-off-only` skips the configured scenario so the run can be executed
// under an LD_PRELOAD syscall counter to prove there is no outbound connect.
#include "fim_backend.h"
#include "fim_inline_completion.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QList>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

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
                detail.isEmpty() ? "" : "  ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
    std::fflush(stdout);
}


/// A minimal loopback HTTP/1.1 endpoint, independent of the Qt event loop so it
/// can answer while the Rust call blocks the calling thread.
class LoopbackEndpoint {
public:
    explicit LoopbackEndpoint(QByteArray responseBody)
        : response_(std::move(responseBody))
    {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            return;
        }
        int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
            ::close(listenFd_);
            listenFd_ = -1;
            return;
        }
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
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this]() { run(); });
    }

    ~LoopbackEndpoint()
    {
        stop_.store(true);
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }

    bool isListening() const { return listenFd_ >= 0; }
    QString endpoint() const { return QStringLiteral("http://127.0.0.1:%1").arg(port_); }
    int requestCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(requests_.size());
    }
    QStringList requests() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        QStringList copy;
        for (const QByteArray &raw : requests_) {
            copy.append(QString::fromUtf8(raw));
        }
        return copy;
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
                const ssize_t read = ::recv(client, chunk, sizeof(chunk), 0);
                if (read <= 0) {
                    break;
                }
                raw.append(chunk, static_cast<int>(read));
                if (requestComplete(raw)) {
                    break;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                requests_.append(raw);
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
    int listenFd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    QList<QByteArray> requests_;
};

class FakeBackend final : public vedit::editor::fim::Backend {
public:
    explicit FakeBackend(vedit::editor::fim::Reply reply)
        : reply_(std::move(reply))
    {
    }

    vedit::editor::fim::Reply complete(const QString &, const QString &) override
    {
        ++calls;
        return reply_;
    }

    int calls = 0;

private:
    vedit::editor::fim::Reply reply_;
};

#if defined(VEDIT_FIM_SELFTEST_HAVE_FFI)
void clearFimEnvironment()
{
    for (const char *name : {"VEDIT_FIM_ENABLED", "VEDIT_FIM_PROTOCOL", "VEDIT_FIM_ENDPOINT",
                             "VEDIT_FIM_MODEL", "VEDIT_FIM_TIMEOUT_MS", "VEDIT_FIM_API_KEY"}) {
        qunsetenv(name);
    }
}

void configureFimEnvironment(const QString &endpoint, const QString &model)
{
    qputenv("VEDIT_FIM_ENABLED", QByteArrayLiteral("1"));
    qputenv("VEDIT_FIM_ENDPOINT", endpoint.toUtf8());
    qputenv("VEDIT_FIM_MODEL", model.toUtf8());
    qputenv("VEDIT_FIM_TIMEOUT_MS", QByteArrayLiteral("3000"));
}

void isolateUserConfiguration(const QTemporaryDir &home)
{
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QStringLiteral("/cache")).toUtf8());
}
#endif // VEDIT_FIM_SELFTEST_HAVE_FFI

void testContextSplit()
{
    const QString text = QStringLiteral("module m;\n  assign x = 1;\nendmodule\n");
    const auto atBody = vedit::editor::fim::contextAt(text, 1, 6);
    check("fim.context.split-prefix",
          atBody.prefix == QStringLiteral("module m;\n  assi"),
          atBody.prefix);
    check("fim.context.split-suffix",
          atBody.suffix == QStringLiteral("gn x = 1;\nendmodule\n"),
          atBody.suffix);
    const auto atEnd = vedit::editor::fim::contextAt(text, 99, 99);
    check("fim.context.clamps-past-the-end", atEnd.prefix == text && atEnd.suffix.isEmpty());
    const auto atStart = vedit::editor::fim::contextAt(text, 0, 0);
    check("fim.context.starts-at-zero", atStart.prefix.isEmpty() && atStart.suffix == text);
}

void testStateMachine()
{
    // No backend: the client reports unavailability without calling anything.
    {
        vedit::editor::fim::InlineCompletion client(nullptr);
        check("fim.state.no-backend.request", !client.request(QStringLiteral("a"), 0, 0));
        check("fim.state.no-backend.status",
              client.statusText().contains(QStringLiteral("未链接 Rust 服务")),
              client.statusText());
        check("fim.state.no-backend.no-suggestion", !client.hasSuggestion());
    }
    // Disabled backend: exactly one backend call (the seam was exercised), no
    // suggestion, status carries the Rust reason.
    {
        FakeBackend backend(vedit::editor::fim::Reply{
            vedit::editor::fim::Reply::State::Disabled, QStringLiteral("FIM 未启用")});
        vedit::editor::fim::InlineCompletion client(&backend);
        check("fim.state.disabled.request", !client.request(QStringLiteral("module m;"), 0, 8));
        check("fim.state.disabled.backend-called", backend.calls == 1);
        check("fim.state.disabled.status",
              client.statusText().contains(QStringLiteral("FIM 未启用")), client.statusText());
        check("fim.state.disabled.no-suggestion", !client.hasSuggestion());
    }
    // Text backend: suggestion arrives, typing invalidates, accept takes it once.
    {
        FakeBackend backend(vedit::editor::fim::Reply{
            vedit::editor::fim::Reply::State::Text, QStringLiteral(" assign x = 1;\n")});
        vedit::editor::fim::InlineCompletion client(&backend);
        check("fim.state.text.request",
              client.request(QStringLiteral("module m;\n  "), 1, 2));
        check("fim.state.text.suggestion",
              client.hasSuggestion()
                  && client.suggestion() == QStringLiteral(" assign x = 1;\n"));
        check("fim.state.text.status",
              client.statusText().contains(QStringLiteral("建议就绪")), client.statusText());
        client.onTextEdited();
        check("fim.state.text.invalidated", !client.hasSuggestion());
        check("fim.state.text.accept-after-invalidate", client.accept().isEmpty());

        check("fim.state.text.request-again",
              client.request(QStringLiteral("module m;\n  "), 1, 2));
        check("fim.state.text.accept",
              client.accept() == QStringLiteral(" assign x = 1;\n"));
        check("fim.state.text.accept-clears", !client.hasSuggestion());
        check("fim.state.text.accept-once", client.accept().isEmpty());

        check("fim.state.text.request-for-cancel",
              client.request(QStringLiteral("module m;\n  "), 1, 2));
        client.cancel();
        check("fim.state.text.cancel", !client.hasSuggestion());
        check("fim.state.text.request-count", client.requestCount() == 3,
              QString::number(client.requestCount()));
    }
    // Error backend: the status is the short error sentence and no suggestion.
    {
        FakeBackend backend(vedit::editor::fim::Reply{
            vedit::editor::fim::Reply::State::Error, QStringLiteral("FIM 超时（3000ms）")});
        vedit::editor::fim::InlineCompletion client(&backend);
        check("fim.state.error.request", !client.request(QStringLiteral("a"), 0, 0));
        check("fim.state.error.status",
              client.statusText().contains(QStringLiteral("FIM 超时")), client.statusText());
    }
}

#if defined(VEDIT_FIM_SELFTEST_HAVE_FFI)
/// Redact the test key before printing any request text into the evidence.
QString redact(const QString &raw)
{
    QString copy = raw;
    copy.replace(QStringLiteral("SECRET-SELFTEST-KEY"), QStringLiteral("<redacted>"));
    return copy;
}

void testRustDefaultOff(const QTemporaryDir &home, bool withEndpoint)
{
    clearFimEnvironment();
    isolateUserConfiguration(home);
    std::unique_ptr<LoopbackEndpoint> endpointGuard;
    if (withEndpoint) {
        endpointGuard = std::make_unique<LoopbackEndpoint>(
            QByteArrayLiteral("{\"choices\":[{\"text\":\"SHOULD-NOT-APPEAR\"}]}"));
    }
    LoopbackEndpoint *endpoint = endpointGuard.get();
    if (endpoint != nullptr) {
        check("fim.rust.default-off.endpoint-listening", endpoint->isListening());
    } else {
        std::printf("SKIP fim.rust.default-off.endpoint-listening (--default-off-only: no listener "
                    "so the syscall counter sees a socket-free process)\n");
    }
    auto backend = vedit::editor::fim::makeRustBackend();
    check("fim.rust.backend.available", backend != nullptr,
          vedit::editor::fim::backendDescription());
    if (backend == nullptr) {
        return;
    }
    vedit::editor::fim::InlineCompletion client(backend.get());
    const bool suggestion = client.request(QStringLiteral("module m;"), 0, 9);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::printf("FIM-DEFAULT-OFF status=\"%s\" endpoint_requests=%d suggestion=%d\n",
                qPrintable(client.statusText()),
                endpoint != nullptr ? endpoint->requestCount() : 0, suggestion ? 1 : 0);
    check("fim.rust.default-off.no-suggestion", !suggestion);
    check("fim.rust.default-off.disabled-status",
          client.statusText().contains(QStringLiteral("未启用")) || client.hasSuggestion() == false,
          client.statusText());
    if (endpoint != nullptr) {
        check("fim.rust.default-off.zero-endpoint-connections", endpoint->requestCount() == 0,
              QStringLiteral("connections=%1").arg(endpoint->requestCount()));
    }
}

void testRustConfigured(const QTemporaryDir &home)
{
    isolateUserConfiguration(home);
    LoopbackEndpoint endpoint(
        QByteArrayLiteral("{\"choices\":[{\"text\":\" assign x = 1;\"}]}"));
    check("fim.rust.configured.endpoint-listening", endpoint.isListening());
    configureFimEnvironment(endpoint.endpoint(), QStringLiteral("local-fake-model"));
    qputenv("VEDIT_FIM_API_KEY", QByteArrayLiteral("SECRET-SELFTEST-KEY"));

    auto backend = vedit::editor::fim::makeRustBackend();
    if (backend == nullptr) {
        check("fim.rust.configured.backend-available", false);
        return;
    }
    vedit::editor::fim::InlineCompletion client(backend.get());
    // The Qt side only LISTENS on the socket above; the request it triggers must
    // come from the Rust transport (vd-fim -> ureq).
    const bool suggestion = client.request(QStringLiteral("module m;\n  "), 1, 2);
    check("fim.rust.configured.suggestion", suggestion, client.statusText());
    check("fim.rust.configured.suggestion-text",
          client.suggestion().contains(QStringLiteral("assign x = 1;")),
          client.suggestion());
    check("fim.rust.configured.request-count", endpoint.requestCount() == 1,
          QStringLiteral("requests=%1").arg(endpoint.requestCount()));

    const QStringList requests = endpoint.requests();
    if (!requests.isEmpty()) {
        const QString raw = redact(requests.first());
        const QString firstLine = raw.section(QLatin1Char('\n'), 0, 0).trimmed();
        QString userAgent;
        for (const QString &line : raw.split(QLatin1Char('\n'))) {
            if (line.trimmed().toLower().startsWith(QStringLiteral("user-agent:"))) {
                userAgent = line.trimmed();
            }
        }
        const bool hasPrompt = raw.contains(QStringLiteral("\"prompt\""));
        const bool hasSuffix = raw.contains(QStringLiteral("\"suffix\""));
        const bool hasBearer = raw.contains(QStringLiteral("Authorization: Bearer <redacted>"))
            || raw.toLower().contains(QStringLiteral("authorization: bearer <redacted>"));
        std::printf("FIM-RUST-REQUEST first_line=\"%s\" %s body_has_prompt=%d body_has_suffix=%d "
                    "authorization_bearer_redacted=%d\n",
                    qPrintable(firstLine), qPrintable(userAgent), hasPrompt ? 1 : 0,
                    hasSuffix ? 1 : 0, hasBearer ? 1 : 0);
        check("fim.rust.configured.request-is-rust-http",
              firstLine.startsWith(QStringLiteral("POST /v1/completions ")), firstLine);
        check("fim.rust.configured.body-has-prompt-and-suffix", hasPrompt && hasSuffix);
        check("fim.rust.configured.key-as-bearer-only", hasBearer);
        check("fim.rust.configured.no-key-in-status",
              !client.statusText().contains(QStringLiteral("SECRET-SELFTEST-KEY")));
    }

    // Half-configured (enabled + endpoint, no model): the Rust side reports the
    // missing model and must not open a second connection.
    const int requestsBefore = endpoint.requestCount();
    clearFimEnvironment();
    qputenv("VEDIT_FIM_ENABLED", QByteArrayLiteral("1"));
    qputenv("VEDIT_FIM_ENDPOINT", endpoint.endpoint().toUtf8());
    qputenv("VEDIT_FIM_PROTOCOL", QByteArrayLiteral("open_ai"));
    vedit::editor::fim::InlineCompletion half(backend.get());
    const bool halfSuggestion = half.request(QStringLiteral("module m;"), 0, 9);
    std::printf("FIM-HALF-CONFIGURED status=\"%s\" endpoint_requests=%d\n",
                qPrintable(half.statusText()), endpoint.requestCount());
    check("fim.rust.half-configured.no-suggestion", !halfSuggestion);
    check("fim.rust.half-configured.no-new-connection", endpoint.requestCount() == requestsBefore,
          QStringLiteral("requests=%1").arg(endpoint.requestCount()));
    clearFimEnvironment();
}
#endif // VEDIT_FIM_SELFTEST_HAVE_FFI

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vedit-fim-selftest"));
    const bool defaultOffOnly = app.arguments().contains(QStringLiteral("--default-off-only"));

    std::printf("FIM-BACKEND %s\n", qPrintable(vedit::editor::fim::backendDescription()));
    testContextSplit();
    testStateMachine();

#if defined(VEDIT_FIM_SELFTEST_HAVE_FFI)
    QTemporaryDir home;
    if (!home.isValid()) {
        check("fim.selftest.temp-home", false);
    } else {
        testRustDefaultOff(home, !defaultOffOnly);
        if (!defaultOffOnly) {
            testRustConfigured(home);
        } else {
            std::printf("SKIP fim.rust.configured (--default-off-only)\n");
        }
    }
#else
    (void)defaultOffOnly;
    std::printf("SKIP fim.rust.* (this build has no Rust service layer; the lane CMake says why)\n");
#endif

    std::printf("FIM-SELFTEST ok=%d checks=%d failures=%d\n",
                g_failures == 0 ? 1 : 0, g_checks, g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
