#include "startup_welcome.h"

#include "i18n.h"
#include "product_theme.h"
#include "themed_icons.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QProcessEnvironment>
#include <QScreen>
#include <QSocketNotifier>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <poll.h>
#include <unistd.h>

namespace vedit::app {
namespace {

constexpr auto kWelcomeArgument = "--internal-startup-welcome";
constexpr auto kWelcomeChild = "VEDIT_STARTUP_WELCOME_CHILD";

class FirstWorkbenchPaint final : public QObject
{
public:
    FirstWorkbenchPaint(QWidget *window, std::function<void()> callback)
        : QObject(window), callback_(std::move(callback))
    {
        window->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *window = static_cast<QWidget *>(object);
        if (event->type() == QEvent::Paint ||
            (event->type() == QEvent::WindowStateChange && window->isMinimized())) {
            window->removeEventFilter(this);
            QTimer::singleShot(0, window, std::move(callback_));
            deleteLater();
        }
        return false;
    }

private:
    std::function<void()> callback_;
};

class BreathingBar final : public QWidget
{
public:
    explicit BreathingBar(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(232, 6);
        setAccessibleName(QCoreApplication::translate("VEdit", "Opening the workbench…"));
        setAccessibleDescription(QCoreApplication::translate("VEdit", "Startup in progress"));
        if (!qEnvironmentVariableIsSet("VEDIT_REDUCED_MOTION") &&
            style()->styleHint(QStyle::SH_Widget_Animate, nullptr, this) != 0) {
            time_.start();
            auto *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
            timer->start(32);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal pulse = time_.isValid()
            ? (1.0 - std::cos(time_.elapsed() * 0.0039269908)) / 2.0 : 0.5;
        QColor edge = palette().color(QPalette::Highlight);
        QColor center = edge;
        edge.setAlphaF(0.38 + 0.20 * pulse);
        center.setAlphaF(0.65 + 0.35 * pulse);
        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0, edge);
        gradient.setColorAt(0.5, center);
        gradient.setColorAt(1, edge);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::AlternateBase));
        painter.drawRoundedRect(rect(), 3, 3);
        painter.setBrush(gradient);
        painter.drawRoundedRect(rect(), 3, 3);
        if (qEnvironmentVariableIsSet("VEDIT_STARTUP_TRACE")) {
            std::printf("WELCOME-TRACE frame=%d elapsed_ms=%lld phase=%.3f animated=%d\n",
                        ++frames_, static_cast<long long>(time_.isValid() ? time_.elapsed() : 0),
                        pulse, time_.isValid());
            std::fflush(stdout);
        }
    }

private:
    QElapsedTimer time_;
    int frames_ = 0;
};

} // namespace

void afterFirstWorkbenchPaint(QWidget *window, std::function<void()> callback)
{
    if (window->isMinimized()) {
        QTimer::singleShot(0, window, std::move(callback));
    } else {
        new FirstWorkbenchPaint(window, std::move(callback));
    }
}

bool isStartupWelcome(int argc, char **argv)
{
    return argc > 1 && std::strcmp(argv[1], kWelcomeArgument) == 0 &&
           qEnvironmentVariableIsSet(kWelcomeChild);
}

StartupWelcome::~StartupWelcome()
{
    finish();
    if (process_.state() != QProcess::NotRunning && !process_.waitForFinished(250)) {
        process_.kill();
        process_.waitForFinished();
    }
}

void StartupWelcome::start(const QString &settingsPath)
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal") ||
        qEnvironmentVariableIsSet("VEDIT_NO_STARTUP_WELCOME")) {
        return;
    }
    QString program = QCoreApplication::applicationFilePath();
    QStringList arguments{QString::fromLatin1(kWelcomeArgument), settingsPath,
                          QStringLiteral("-platform"), platform};
    const QString appDir = qEnvironmentVariable("APPDIR");
    const QString loader = appDir + QStringLiteral("/ld-linux-x86-64.so.2");
    const QString entry = QCoreApplication::arguments().value(0);
    if (!appDir.isEmpty() && entry.startsWith(appDir + QStringLiteral("/bin/")) &&
        QFileInfo(loader).isExecutable()) {
        program = loader;
        arguments.prepend(entry);
        arguments.prepend(appDir + QStringLiteral("/lib"));
        arguments.prepend(QStringLiteral("--library-path"));
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kWelcomeChild), QStringLiteral("1"));
    environment.remove(QStringLiteral("DESKTOP_STARTUP_ID"));
    environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    process_.setProcessEnvironment(environment);
    process_.setProcessChannelMode(QProcess::ForwardedChannels);
    process_.start(program, arguments);
}

void StartupWelcome::finish()
{
    process_.closeWriteChannel();
}

int runStartupWelcome(const QString &settingsPath)
{
    auto *app = qApp;
    app->setQuitOnLastWindowClosed(false);
    QSocketNotifier completion(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&completion, &QSocketNotifier::activated, app, &QCoreApplication::quit);

    // A fast launch may finish before the helper has a window. Avoid showing a late splash.
    pollfd parentChannel{STDIN_FILENO, POLLIN, 0};
    if (poll(&parentChannel, 1, 0) != 0) {
        return 0;
    }
    i18n::installTranslations(settingsPath);
    const QString themeId = i18n::readSettingsString(settingsPath, QStringLiteral("uiTheme"));
    const ProductTheme *savedTheme = productThemeById(themeId);
    const ProductTheme &theme = savedTheme == nullptr ? defaultProductTheme() : *savedTheme;

    QWidget welcome(nullptr, Qt::SplashScreen | Qt::WindowStaysOnTopHint
                                 | Qt::WindowDoesNotAcceptFocus);
    welcome.setAttribute(Qt::WA_ShowWithoutActivating);
    welcome.setWindowTitle(QStringLiteral("VEdit"));
    welcome.setPalette(productPalette(theme));
    welcome.setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(&welcome);
    layout->setContentsMargins(48, 28, 48, 29);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignHCenter);
    auto *icon = new QLabel(&welcome);
    icon->setPixmap(themed_icons::appIcon(theme.dark).pixmap(QSize(44, 44), welcome.devicePixelRatioF()));
    layout->addWidget(icon, 0, Qt::AlignHCenter);
    auto *title = new QLabel(QStringLiteral("VEdit"), &welcome);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.55);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title, 0, Qt::AlignHCenter);
    auto *status = new QLabel(QCoreApplication::translate("VEdit", "Opening the workbench…"), &welcome);
    layout->addWidget(status, 0, Qt::AlignHCenter);
    layout->addSpacing(5);
    layout->addWidget(new BreathingBar(&welcome), 0, Qt::AlignHCenter);
    welcome.adjustSize();
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        welcome.move(screen->availableGeometry().center() - welcome.rect().center());
    }
    if (poll(&parentChannel, 1, 0) != 0) {
        return 0;
    }
    welcome.show();
    if (qEnvironmentVariableIsSet("VEDIT_STARTUP_TRACE")) {
        std::printf("WELCOME-TRACE window=%llu topmost=%d accepts_focus=%d\n",
                    static_cast<unsigned long long>(welcome.winId()),
                    welcome.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
                    !welcome.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        std::fflush(stdout);
    }
    return app->exec();
}

} // namespace vedit::app
