#pragma once

#include <QProcess>

#include <functional>

class QWidget;

namespace vedit::app {

class StartupWelcome
{
public:
    ~StartupWelcome();
    void start(const QString &settingsPath);
    void finish();

private:
    QProcess process_;
};

bool isStartupWelcome(int argc, char **argv);
int runStartupWelcome(const QString &settingsPath);
void afterFirstWorkbenchPaint(QWidget *window, std::function<void()> callback);

} // namespace vedit::app
