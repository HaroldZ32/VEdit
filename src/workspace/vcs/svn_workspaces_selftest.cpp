#include "../../common/runtime_init.h"
#include "svn_workspaces.h"
#include "vcs_panel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <utility>

namespace {
using namespace vedit::workspace;
using namespace vedit::workspace::vcs;
int failures = 0;

bool check(const char *name, bool passed)
{
    std::printf("CHECK %s %s\n", name, passed ? "PASS" : "FAIL");
    failures += !passed;
    return passed;
}

bool until(const auto &condition, int timeout = 5000)
{
    QElapsedTimer time;
    time.start();
    while (!condition() && time.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(4);
    }
    return condition();
}

void pump(int milliseconds)
{
    until([] { return false; }, milliseconds);
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool run(const QString &executable, const QStringList &arguments)
{
    QProcess process;
    process.start(executable, arguments);
    if (process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }
    qWarning("fixture command failed: %s", process.readAllStandardError().constData());
    return false;
}

class ObservedSource final : public VcsSource {
public:
    Info info(const std::string &root) override { return real_.info(root); }
    Status status(const std::string &root) override { return real_.status(root); }
    Diff diff(const std::string &root, const std::string &path) override { return real_.diff(root, path); }
    void requestInfo(const std::string &root, InfoCallback done) override { real_.requestInfo(root, std::move(done)); }
    void requestStatus(const std::string &root, StatusCallback done) override
    {
        const QString name = QString::fromStdString(root);
        if (!name.isEmpty()) { visits.append(name); }
        real_.requestStatus(root, [this, name, done = std::move(done)](Status value) mutable {
            if (!holdRoot.isEmpty() && holdRoot == name && !held) {
                held = [done = std::move(done), value = std::move(value)]() mutable { done(std::move(value)); };
            } else {
                done(std::move(value));
            }
        });
    }
    void release()
    {
        holdRoot.clear();
        if (auto reply = std::exchange(held, {})) { reply(); }
    }

    QStringList visits;
    QString holdRoot;
    std::function<void()> held;

private:
    FfiVcsSource real_;
};

struct Fixture {
    QString project;
    QString external;
    QString nested;
    QString deep;
    QString other;
};

bool prepare(const QString &root, const QString &svn, const QString &admin, Fixture &fixture)
{
    const QString seed = root + QStringLiteral("/seed");
    const QString repository = root + QStringLiteral("/repo");
    fixture = {root + QStringLiteral("/project"), root + QStringLiteral("/project/ext vendor"),
               root + QStringLiteral("/project/nested"), root + QStringLiteral("/project/a/b/c/d/deep"),
               root + QStringLiteral("/other")};
    const auto url = QUrl::fromLocalFile(repository).toString(QUrl::FullyEncoded);
    const auto invoke = [&](QStringList args) {
        args << QStringLiteral("--non-interactive") << QStringLiteral("--no-auth-cache") << QStringLiteral("--config-dir")
             << root + QStringLiteral("/svn-config");
        return run(svn, args);
    };
    const auto checkout = [&](const QString &part, const QString &destination) {
        return invoke({QStringLiteral("checkout"), QStringLiteral("-q"), url + part, destination});
    };
    return QDir().mkpath(seed + QStringLiteral("/main")) && QDir().mkpath(seed + QStringLiteral("/vendor")) &&
           put(seed + QStringLiteral("/main/main.txt"), "base main\n") &&
           put(seed + QStringLiteral("/vendor/vendor.txt"), "base vendor\n") &&
           run(admin, {QStringLiteral("create"), repository}) &&
           invoke({QStringLiteral("import"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("fixture"), seed, url}) &&
           checkout(QStringLiteral("/main"), fixture.project) &&
           invoke({QStringLiteral("propset"), QStringLiteral("svn:externals"), QStringLiteral("^/vendor \"ext vendor\""), fixture.project}) &&
           invoke({QStringLiteral("update"), QStringLiteral("-q"), fixture.project}) &&
           QFileInfo::exists(fixture.external + QStringLiteral("/.svn/wc.db")) &&
           checkout(QStringLiteral("/vendor"), fixture.nested) && QDir().mkpath(QFileInfo(fixture.deep).absolutePath()) &&
           checkout(QStringLiteral("/vendor"), fixture.deep) && checkout(QStringLiteral("/vendor"), fixture.other) &&
           QFile::link(fixture.other, fixture.project + QStringLiteral("/outside-link")) &&
           put(fixture.project + QStringLiteral("/main.txt"), "changed main\n") &&
           put(fixture.external + QStringLiteral("/vendor.txt"), "changed external\n") &&
           put(fixture.nested + QStringLiteral("/vendor.txt"), "changed nested\n");
}

void exercise(const Fixture &fixture, const QString &settings)
{
    VcsPanel panel;
    panel.resize(1250, 760);
    int remoteCalls = 0;
    auto active = std::make_unique<ObservedSource>();
    auto *activeProbe = active.get();
    auto monitor = std::make_unique<ObservedSource>();
    auto *monitorProbe = monitor.get();
    const auto select = [&](const QString &root) {
        panel.setRoot(root.toStdString());
        panel.refresh();
    };
    auto *workspaces = new SvnWorkspaces(panel, settings, select, [&] { ++remoteCalls; }, std::move(monitor), 200);
    panel.show();
    workspaces->setProject(fixture.project, fixture.project);
    panel.setSource(std::move(active));
    auto *selector = panel.findChild<QComboBox *>(QStringLiteral("svn-working-copy-selector"));
    auto *close = panel.findChild<QPushButton *>(QStringLiteral("svn-working-copy-close-reopen"));
    auto *wider = panel.findChild<QAction *>(QStringLiteral("svn-working-copy-search-wider"));
    auto *remote = panel.findChild<QAction *>(QStringLiteral("svn-working-copy-periodic-remote"));
    auto *remoteTimer = panel.findChild<QTimer *>(QStringLiteral("svn-working-copy-remote-timer"));
    if (!check("workspaces.controls", selector && close && wider && remote && remoteTimer)) { return; }
    const auto hasRow = [&](const QString &path) {
        for (int index = 0; index < panel.changeCount(); ++index) {
            if (panel.fileText(index).startsWith(path + QStringLiteral("  "))) { return true; }
        }
        return false;
    };
    const auto selectCopy = [&](const QString &root) {
        const int index = selector->findData(root);
        if (index < 0) { return false; }
        selector->setCurrentIndex(index);
        return QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, index));
    };
    check("workspaces.primary-before-monitor", until([&] { return panel.fileCount() > 0; }) && monitorProbe->visits.isEmpty());
    check("workspaces.external-and-nested", until([&] {
        return selector->findData(fixture.external) >= 0 && selector->findData(fixture.nested) >= 0;
    }));
    check("workspaces.bounded-and-no-symlink", selector->findData(fixture.deep) < 0 &&
          selector->findData(fixture.other) < 0 && selector->findData(fixture.project + QStringLiteral("/outside-link")) < 0);
    check("workspaces.monitor-real-status", until([&] {
        const int external = selector->findData(fixture.external);
        const int nested = selector->findData(fixture.nested);
        return selector->itemText(external).contains(QStringLiteral("1 changed")) &&
               selector->itemText(nested).contains(QStringLiteral("1 changed"));
    }));
    wider->setChecked(true);
    check("workspaces.explicit-wide-discovery", until([&] { return selector->findData(fixture.deep) >= 0; }));
    wider->setChecked(false);
    check("workspaces.opt-out-restores-bound", until([&] { return selector->findData(fixture.deep) < 0; }));
    check("workspaces.remote-disabled-by-default", !remoteTimer->isActive() && !remote->isChecked() && remoteCalls == 0);

    monitorProbe->holdRoot = fixture.external;
    check("workspaces.delayed-monitor-fixture", until([&] { return bool(monitorProbe->held); }));
    check("workspaces.switch-external", selectCopy(fixture.external) && until([&] {
        return panel.root() == fixture.external.toStdString() && panel.fileCount() == 1 &&
               panel.fileText(0).contains(QStringLiteral("vendor.txt"));
    }));
    remote->setChecked(true);
    remoteTimer->setInterval(25);
    check("workspaces.periodic-remote-opt-in", until([&] { return remoteCalls > 0; }));
    close->click();
    check("workspaces.close-deactivates-root", panel.root().empty() && !remoteTimer->isActive() && !remote->isChecked());
    const auto remoteBefore = remoteCalls;
    const auto inactiveBefore = monitorProbe->visits.count(fixture.external);
    const auto activeBefore = activeProbe->visits.count(fixture.external);
    monitorProbe->release();
    check("workspaces.switch-after-close", selectCopy(fixture.project) && until([&] {
        return hasRow(QStringLiteral("main.txt"));
    }));
    check("workspaces.closed-fixture-write", put(fixture.external + QStringLiteral("/vendor.txt"), "external closed edit\n"));
    pump(620);
    check("workspaces.closed-discards-late-and-stops-reads", remoteBefore == remoteCalls &&
          monitorProbe->visits.count(fixture.external) == inactiveBefore && activeProbe->visits.count(fixture.external) == activeBefore &&
          selector->itemText(selector->findData(fixture.external)).contains(QStringLiteral("closed")));
    workspaces->setProject(fixture.other, fixture.other);
    check("workspaces.project-switch-refresh", until([&] { return panel.root() == fixture.other.toStdString() && panel.fileCount() == 0; }));
    workspaces->setProject(fixture.project, fixture.project);
    check("workspaces.closed-state-restored", until([&] {
        const int index = selector->findData(fixture.external);
        return index >= 0 && selector->itemText(index).contains(QStringLiteral("closed"));
    }));
    check("workspaces.select-closed", selectCopy(fixture.external) && panel.root().empty());
    close->click();
    check("workspaces.reopen-refreshes-root", until([&] {
        return panel.root() == fixture.external.toStdString() && panel.fileCount() == 1 && !remoteTimer->isActive();
    }));
    activeProbe->holdRoot = fixture.nested;
    check("workspaces.delayed-active-fixture", selectCopy(fixture.nested) && until([&] { return bool(activeProbe->held); }));
    check("workspaces.replace-active-request", selectCopy(fixture.project));
    activeProbe->release();
    check("workspaces.stale-active-status-hidden", until([&] {
        return panel.root() == fixture.project.toStdString() && hasRow(QStringLiteral("main.txt"));
    }) && !panel.diffText().contains(QStringLiteral("vendor")));
    const QString capture = qEnvironmentVariable("VEDIT_SVN_WORKSPACES_CAPTURE");
    if (!capture.isEmpty()) { check("workspaces.capture", panel.grab().save(capture)); }
}
} // namespace

int main(int argc, char **argv)
{
    vedit_runtime_init();
    QApplication app(argc, argv);
    const QString svn = qEnvironmentVariable("VEDIT_SVN_BIN");
    const QString admin = qEnvironmentVariable("VEDIT_SVNADMIN");
    if (svn.isEmpty() || admin.isEmpty()) {
        std::puts("SKIP real SVN workspaces: set VEDIT_SVN_BIN and VEDIT_SVNADMIN");
        return 77;
    }
    QTemporaryDir temporary;
    Fixture fixture;
    if (!check("workspaces.real-external-fixture", temporary.isValid() && prepare(temporary.path(), svn, admin, fixture))) { return 1; }
    exercise(fixture, temporary.filePath(QStringLiteral("working-copies.ini")));
    std::printf("SVN-WORKSPACES failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
