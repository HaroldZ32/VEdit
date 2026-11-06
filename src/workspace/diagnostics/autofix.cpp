#include "autofix.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace vedit::workspace::diagnostics {

namespace {

QString missingBinaryReason()
{
    return QCoreApplication::translate(
        "VEdit", "verible-verilog-lint was not found: no autofix is possible");
}

QStringList autofixArguments(const QString &rule, const QString &rulesConfigPath)
{
    QStringList arguments;
    // Isolate the rule: without `--ruleset=none` the default ruleset also runs
    // and its fixes would be attributed to this diagnostic.
    arguments << QStringLiteral("--ruleset=none")
              << QStringLiteral("--rules=%1").arg(rule.trimmed())
              << QStringLiteral("--autofix=inplace");
    if (!rulesConfigPath.isEmpty()) {
        arguments << QStringLiteral("--rules_config=%1").arg(rulesConfigPath);
    }
    return arguments;
}

/// The verdict for one finished job, from the temp copy the engine edited.
AutofixOutcome outcomeFromCopy(const QString &copyPath, const QString &original,
                               const QString &rule, bool attempted,
                               bool normalExit, const QString &stderrText)
{
    AutofixOutcome outcome;
    outcome.attempted = attempted;
    QFile repaired(copyPath);
    if (!repaired.open(QIODevice::ReadOnly)) {
        outcome.error = QCoreApplication::translate("VEdit", "the fixed copy could not be read back");
        return outcome;
    }
    outcome.fixedText = QString::fromUtf8(repaired.readAll());
    repaired.close();
    if (!normalExit) {
        outcome.error = QCoreApplication::translate("VEdit", "the lint tool crashed during autofix");
        return outcome;
    }
    if (outcome.fixedText == original) {
        // The engine ran and changed NOTHING: this rule has no autofix for this
        // text (verible reports the diagnostic but offers no replacement).
        outcome.supported = false;
        outcome.error = QStringLiteral("%1 has no autofix here").arg(rule.trimmed());
        if (!stderrText.isEmpty()) {
            outcome.error += QStringLiteral(" (%1)").arg(stderrText.left(160));
        }
        return outcome;
    }
    outcome.supported = true;
    return outcome;
}

} // namespace

AutofixOutcome autofixRuleInTempCopy(const QString &lintBinary,
                                     const QString &rule,
                                     const QString &filePathHint,
                                     const QString &text,
                                     const QString &rulesConfigPath)
{
    AutofixOutcome outcome;
    if (lintBinary.isEmpty() || !QFileInfo::exists(lintBinary)) {
        outcome.error = missingBinaryReason();
        return outcome;
    }
    if (rule.trimmed().isEmpty()) {
        outcome.error = QCoreApplication::translate(
            "VEdit", "this diagnostic carries no rule id, so no autofix can be selected");
        return outcome;
    }
    QTemporaryDir temp;
    if (!temp.isValid()) {
        outcome.error = QCoreApplication::translate("VEdit", "no temporary directory is available");
        return outcome;
    }
    // The copy keeps the original base name: `module-filename` (and any rule
    // that looks at the path) must see the same name the user's file has.
    QString baseName = QFileInfo(filePathHint).fileName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("autofix.sv");
    }
    const QString copyPath = temp.filePath(baseName);
    QFile copy(copyPath);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outcome.error = QCoreApplication::translate("VEdit", "the temporary copy could not be written");
        return outcome;
    }
    copy.write(text.toUtf8());
    copy.close();

    QStringList arguments = autofixArguments(rule, rulesConfigPath);
    arguments << copyPath;

    QProcess process;
    process.setProgram(lintBinary);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    outcome.attempted = true;
    process.start();
    if (!process.waitForStarted(5000)) {
        outcome.error = QCoreApplication::translate("VEdit", "the lint tool could not be started: %1")
                            .arg(process.errorString());
        return outcome;
    }
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(2000);
        outcome.error = QCoreApplication::translate("VEdit", "the autofix run timed out");
        return outcome;
    }
    QFile repaired(copyPath);
    if (!repaired.open(QIODevice::ReadOnly)) {
        outcome.error = QCoreApplication::translate("VEdit", "the fixed copy could not be read back");
        return outcome;
    }
    outcome.fixedText = QString::fromUtf8(repaired.readAll());
    repaired.close();
    if (process.exitStatus() != QProcess::NormalExit) {
        outcome.error = QCoreApplication::translate("VEdit", "the lint tool crashed during autofix");
        return outcome;
    }
    if (outcome.fixedText == text) {
        // The engine ran and changed NOTHING: this rule has no autofix for this
        // text (verible reports the diagnostic but offers no replacement).
        outcome.supported = false;
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        outcome.error = QStringLiteral("%1 has no autofix here").arg(rule.trimmed());
        if (!stderrText.isEmpty()) {
            outcome.error += QStringLiteral(" (%1)").arg(stderrText.left(160));
        }
        return outcome;
    }
    outcome.supported = true;
    return outcome;
}

AutofixEngine::AutofixEngine(QString lintBinary)
    : lintBinary_(std::move(lintBinary))
{
}

struct AutofixEngine::ProbeJob {
    QProcess *process = nullptr;
    QTimer *timeout = nullptr;
    QTemporaryDir *temp = nullptr;
    QString copyPath;
    QString key;
    QString rule;
    QString text;
    std::function<void(const AutofixOutcome &)> done;
};

AutofixEngine::~AutofixEngine()
{
    for (ProbeJob *job : jobs_) {
        if (job->process != nullptr) {
            job->process->kill();
            job->process->waitForFinished(1000);
            delete job->process;
        }
        delete job->timeout;
        delete job->temp;
        delete job;
    }
    jobs_.clear();
}

void AutofixEngine::setLintBinary(const QString &path)
{
    if (lintBinary_ != path) {
        lintBinary_ = path;
        clearCache();
    }
}

void AutofixEngine::setRulesConfig(const QString &path)
{
    if (rulesConfig_ != path) {
        rulesConfig_ = path;
        // A different rules file can change the answer for the SAME rule/text.
        clearCache();
    }
}

void AutofixEngine::clearCache()
{
    cache_.clear();
    cacheBytes_ = 0;
}

QString AutofixEngine::cacheKey(const QString &rule, const QString &filePathHint,
                                 const QString &text) const
{
    // The base name is part of the answer (`module-filename` compares the module
    // name with the file name), and so is the active rules config. The old key
    // was (rule, text) only, so two files with the same text shared one answer.
    const QString baseName = QFileInfo(filePathHint).fileName();
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(rule, baseName, rulesConfig_, QString::number(text.size()),
             QString::number(qHash(text)));
}

void AutofixEngine::storeOutcome(const QString &key, const AutofixOutcome &outcome)
{
    // Bounded by count AND bytes: the cache holds whole fixed documents, so a
    // count-only bound could pin hundreds of megabytes on a large file.
    constexpr qint64 kMaxCacheBytes = 8 * 1024 * 1024;
    if (cache_.size() >= 64 || cacheBytes_ + outcome.fixedText.size() > kMaxCacheBytes) {
        clearCache();
    }
    if (cache_.contains(key)) {
        cacheBytes_ -= cache_.value(key).fixedText.size();
    }
    cache_.insert(key, outcome);
    cacheBytes_ += outcome.fixedText.size();
}

AutofixOutcome AutofixEngine::probe(const QString &rule, const QString &filePathHint,
                                    const QString &text)
{
    const QString key = cacheKey(rule, filePathHint, text);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        return found.value();
    }
    ++probeCount_;
    const AutofixOutcome outcome =
        autofixRuleInTempCopy(lintBinary_, rule, filePathHint, text, rulesConfig_);
    storeOutcome(key, outcome);
    return outcome;
}

void AutofixEngine::probeAsync(const QString &rule, const QString &filePathHint,
                               const QString &text,
                               std::function<void(const AutofixOutcome &)> done)
{
    ++asyncProbeCount_;
    const QString key = cacheKey(rule, filePathHint, text);
    const auto found = cache_.constFind(key);
    if (found != cache_.constEnd()) {
        const AutofixOutcome cached = found.value();
        // Still asynchronous from the caller's point of view: the popup is
        // updated on the next event-loop turn, never inside the mouse event.
        QTimer::singleShot(0, [done = std::move(done), cached]() {
            if (done) {
                done(cached);
            }
        });
        return;
    }

    AutofixOutcome immediate;
    if (lintBinary_.isEmpty() || !QFileInfo::exists(lintBinary_)) {
        immediate.error = missingBinaryReason();
        storeOutcome(key, immediate);
        QTimer::singleShot(0, [done = std::move(done), immediate]() {
            if (done) {
                done(immediate);
            }
        });
        return;
    }
    if (rule.trimmed().isEmpty()) {
        immediate.error = QCoreApplication::translate(
            "VEdit", "this diagnostic carries no rule id, so no autofix can be selected");
        storeOutcome(key, immediate);
        QTimer::singleShot(0, [done = std::move(done), immediate]() {
            if (done) {
                done(immediate);
            }
        });
        return;
    }

    auto *job = new ProbeJob;
    job->temp = new QTemporaryDir;
    if (!job->temp->isValid()) {
        immediate.error = QCoreApplication::translate("VEdit",
                                                      "no temporary directory is available");
        delete job->temp;
        delete job;
        storeOutcome(key, immediate);
        QTimer::singleShot(0, [done = std::move(done), immediate]() {
            if (done) {
                done(immediate);
            }
        });
        return;
    }
    QString baseName = QFileInfo(filePathHint).fileName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("autofix.sv");
    }
    job->copyPath = job->temp->filePath(baseName);
    job->key = key;
    job->rule = rule;
    job->text = text;
    job->done = std::move(done);
    QFile copy(job->copyPath);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        immediate.error = QCoreApplication::translate("VEdit",
                                                      "the temporary copy could not be written");
        auto done = std::move(job->done);
        delete job->temp;
        delete job;
        storeOutcome(key, immediate);
        QTimer::singleShot(0, [done = std::move(done), immediate]() {
            if (done) {
                done(immediate);
            }
        });
        return;
    }
    copy.write(text.toUtf8());
    copy.close();

    job->process = new QProcess;
    job->process->setProgram(lintBinary_);
    QStringList arguments = autofixArguments(rule, rulesConfig_);
    arguments << job->copyPath;
    job->process->setArguments(arguments);
    job->process->setProcessChannelMode(QProcess::SeparateChannels);

    const quint64 id = nextJob_++;
    jobs_.insert(id, job);
    QObject::connect(job->process, &QProcess::finished, job->process,
                     [this, id](int, QProcess::ExitStatus) { finishProbe(id); });
    QObject::connect(job->process, &QProcess::errorOccurred, job->process,
                     [this, id](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart) {
                             finishProbe(id, QCoreApplication::translate(
                                                 "VEdit", "the lint tool could not be started"));
                         }
                     });
    job->timeout = new QTimer;
    job->timeout->setSingleShot(true);
    job->timeout->setInterval(20000);
    QObject::connect(job->timeout, &QTimer::timeout, job->timeout, [this, id]() {
        finishProbe(id, QCoreApplication::translate("VEdit", "the autofix run timed out"));
    });
    job->timeout->start();
    job->process->start();
    ++probeCount_;
}

void AutofixEngine::finishProbe(quint64 id, const QString &error)
{
    ProbeJob *job = jobs_.take(id);
    if (job == nullptr) {
        return;
    }
    AutofixOutcome outcome;
    if (!error.isEmpty()) {
        if (job->process != nullptr) {
            job->process->kill();
        }
        outcome.error = error;
    } else {
        outcome = outcomeFromCopy(job->copyPath, job->text, job->rule, true,
                                  job->process->exitStatus() == QProcess::NormalExit,
                                  QString::fromUtf8(job->process->readAllStandardError()).trimmed());
    }
    storeOutcome(job->key, outcome);
    const auto done = job->done;
    if (job->process != nullptr) {
        job->process->deleteLater();
    }
    delete job->timeout;
    delete job->temp;
    delete job;
    if (done) {
        done(outcome);
    }
}

AutofixOutcome AutofixEngine::fix(const QString &rule, const QString &filePathHint,
                                  const QString &text)
{
    ++fixCount_;
    return autofixRuleInTempCopy(lintBinary_, rule, filePathHint, text, rulesConfig_);
}

} // namespace vedit::workspace::diagnostics
