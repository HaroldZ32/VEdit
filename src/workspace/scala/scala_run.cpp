#include "scala_run.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <cstddef>

#if defined(VEDIT_SCALA_HAVE_FFI)
#include "vd_ffi.h"
#endif

namespace vedit::workspace::scala {

namespace {

/// How deep the scala-cli search walks from the project root.
constexpr int kWalkDepth = 3;

/// How many directory entries the search may visit before giving up. A root that
/// is not a Scala project must cost almost nothing.
constexpr int kWalkBudget = 2000;

/// Directories that never hold the user's Scala sources (and are huge).
bool isSkippedDirectory(const QString &name)
{
    static const QStringList skipped{
        QStringLiteral(".git"),         QStringLiteral("target"),
        QStringLiteral(".scala-build"), QStringLiteral("node_modules"),
        QStringLiteral(".bsp"),         QStringLiteral(".metals"),
    };
    return skipped.contains(name);
}

/// True when a `*.scala` file is reachable from `directory` within the budget.
bool findScalaFile(const QDir &root, const QDir &directory, int depth, int *budget,
                   QStringList *evidence)
{
    if (depth > kWalkDepth || *budget <= 0) {
        return false;
    }
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    *budget -= entries.size();
    for (const QFileInfo &entry : entries) {
        if (entry.isFile() && entry.suffix() == QStringLiteral("scala")) {
            evidence->append(root.relativeFilePath(entry.absoluteFilePath()));
            return true;
        }
    }
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir() || isSkippedDirectory(entry.fileName())) {
            continue;
        }
        if (findScalaFile(root, QDir(entry.absoluteFilePath()), depth + 1, budget, evidence)) {
            return true;
        }
    }
    return false;
}

#if defined(VEDIT_SCALA_HAVE_FFI)
/// Map the ABI's `state` code onto the lane's enum.
RunState stateFromCode(int code)
{
    switch (code) {
    case 0:
        return RunState::Running;
    case 1:
        return RunState::Exited;
    case 2:
        return RunState::Failed;
    case 3:
        return RunState::Cancelled;
    case 4:
        return RunState::TimedOut;
    default:
        return RunState::Failed;
    }
}

/// One borrowed `VdStringList`: the byte arrays and the pointer array stay alive
/// as long as this object does, which is exactly the ABI's "borrowed for the
/// duration of the call" contract.
class BorrowedList final {
public:
    explicit BorrowedList(const QStringList &values)
    {
        storage_.reserve(values.size());
        for (const QString &value : values) {
            storage_.append(value.toUtf8());
        }
        pointers_.reserve(storage_.size());
        for (QByteArray &value : storage_) {
            pointers_.append(value.data());
        }
        list_.count = static_cast<std::size_t>(pointers_.size());
        list_.items = pointers_.isEmpty() ? nullptr : pointers_.data();
    }

    const VdStringList &list() const { return list_; }

private:
    QList<QByteArray> storage_;
    QList<char *> pointers_;
    VdStringList list_{};
};
#endif // VEDIT_SCALA_HAVE_FFI

} // namespace

ShapeReport detectProjectShape(const QString &root)
{
    ShapeReport report;
    const QDir directory(root);
    if (root.isEmpty() || !directory.exists()) {
        report.detail = QCoreApplication::translate(
            "VEdit", "no project is open: open a folder before running a Scala project.");
        return report;
    }
    if (QFileInfo::exists(directory.filePath(QStringLiteral("build.sbt")))) {
        report.shape = ProjectShape::Sbt;
        report.evidence.append(QStringLiteral("build.sbt"));
        report.detail = QCoreApplication::translate(
            "VEdit", "sbt project detected (build.sbt): running `sbt run` offline.");
        return report;
    }
    for (const QString &directives :
         {QStringLiteral("project.scala"), QStringLiteral("scala-cli.conf")}) {
        if (QFileInfo::exists(directory.filePath(directives))) {
            report.shape = ProjectShape::ScalaCli;
            report.evidence.append(directives);
            report.detail = QCoreApplication::translate(
                "VEdit", "scala-cli project detected (%1): running `scala-cli run` offline.")
                              .arg(directives);
            return report;
        }
    }
    int budget = kWalkBudget;
    if (findScalaFile(directory, directory, 0, &budget, &report.evidence)) {
        report.shape = ProjectShape::ScalaCli;
        report.detail = QCoreApplication::translate(
            "VEdit", "scala-cli project detected (%1): running `scala-cli run` offline.")
                          .arg(report.evidence.join(QStringLiteral(", ")));
        return report;
    }
    report.shape = ProjectShape::Unknown;
    report.detail = QCoreApplication::translate(
        "VEdit", "no Scala project here: add build.sbt (sbt) or a Scala source file / "
                 "project.scala (scala-cli).");
    return report;
}

RunPlan buildRunPlan(const QString &root, const ShapeReport &shape, const ScalaTools &tools)
{
    RunPlan plan;
    plan.root = root;
    plan.shape = shape;
    const auto reject = [&plan](const QString &message) {
        plan.ok = false;
        plan.error = message;
        return plan;
    };
    switch (shape.shape) {
    case ProjectShape::Sbt:
        if (tools.sbt.isEmpty()) {
            return reject(QCoreApplication::translate(
                "VEdit", "sbt was not found: the Scala toolchain is missing. Point VEDIT_SBT "
                         "at sbt (or put it on PATH)."));
        }
        plan.program = tools.sbt;
        plan.argv = {tools.sbt, QStringLiteral("-Dsbt.offline=true"), QStringLiteral("run")};
        break;
    case ProjectShape::ScalaCli:
        if (tools.scalaCli.isEmpty()) {
            return reject(QCoreApplication::translate(
                "VEdit", "scala-cli was not found: the Scala toolchain is missing. Point "
                         "VEDIT_SCALA_CLI at scala-cli (or put it on PATH)."));
        }
        plan.program = tools.scalaCli;
        plan.argv = {tools.scalaCli, QStringLiteral("run"),      QStringLiteral("--offline"),
                     QStringLiteral("--server=false"), QStringLiteral("--jvm"),
                     QStringLiteral("system"), root};
        break;
    case ProjectShape::Unknown:
        return reject(shape.detail);
    }
    plan.env.append(QStringLiteral("COURSIER_MODE=offline"));
    plan.ok = true;
    return plan;
}

ScalaRun::ScalaRun(QObject *parent) : QObject(parent)
{
    timer_ = new QTimer(this);
    timer_->setInterval(50);
    connect(timer_, &QTimer::timeout, this, &ScalaRun::poll);
}

ScalaRun::~ScalaRun()
{
    // Dropping this object must never leave a Scala child running: `cancel` sets
    // the ABI's flag and `release` drops the slot, so the engine still kills the
    // tree after this object is gone.
    cancel();
    releaseJob();
#if defined(VEDIT_SCALA_HAVE_FFI)
    if (service_ != nullptr) {
        vd_service_destroy(static_cast<VdService *>(service_));
        service_ = nullptr;
    }
#endif
}

bool ScalaRun::start(const RunPlan &plan)
{
    error_.clear();
    if (isRunning()) {
        error_ = QCoreApplication::translate("VEdit", "a Scala run is already active.");
        return false;
    }
    if (!plan.ok) {
        error_ = plan.error;
        return false;
    }
    if (plan.argv.isEmpty()) {
        error_ = QCoreApplication::translate("VEdit", "the run plan has no program.");
        return false;
    }
#if !defined(VEDIT_SCALA_HAVE_FFI)
    error_ = QCoreApplication::translate(
        "VEdit", "this build has no Rust service layer (vd-ffi): the Scala run cannot start.");
    return false;
#else
    if (service_ == nullptr) {
        service_ = vd_service_create();
        if (service_ == nullptr) {
            error_ = QCoreApplication::translate("VEdit",
                                                 "cannot create the Rust service handle.");
            return false;
        }
    }
    const BorrowedList argv(plan.argv);
    const BorrowedList env(plan.env);
    const QByteArray root = plan.root.toUtf8();

    VdScalaRunOptions options{};
    options.root = plan.root.isEmpty() ? nullptr : root.constData();
    options.argv = argv.list();
    options.env = env.list();
    options.timeout_ms = 0;   // run until it exits; the user cancels explicitly
    options.merge_stderr = 1; // ONE stream in the terminal, as the acceptance asks

    const quint64 job = vd_scala_run_start(static_cast<VdService *>(service_), &options);
    if (job == 0) {
        VdError failure{};
        if (vd_service_error(static_cast<VdService *>(service_), &failure) != 0) {
            error_ = QString::fromUtf8(failure.message);
        } else {
            error_ = QCoreApplication::translate(
                "VEdit", "the Rust engine rejected the run request.");
        }
        return false;
    }
    job_ = job;
    program_ = plan.program;
    state_ = RunState::Running;
    exitCode_ = -1;
    timer_->start();
    return true;
#endif
}

void ScalaRun::cancel()
{
#if defined(VEDIT_SCALA_HAVE_FFI)
    if (job_ != 0 && service_ != nullptr) {
        vd_scala_run_cancel(static_cast<VdService *>(service_), job_);
    }
#endif
}

void ScalaRun::releaseJob()
{
#if defined(VEDIT_SCALA_HAVE_FFI)
    if (job_ != 0 && service_ != nullptr) {
        vd_scala_run_release(static_cast<VdService *>(service_), job_);
    }
#endif
    job_ = 0;
    if (timer_ != nullptr) {
        timer_->stop();
    }
}

void ScalaRun::poll()
{
#if defined(VEDIT_SCALA_HAVE_FFI)
    if (job_ == 0 || service_ == nullptr) {
        return;
    }
    VdScalaRunStatus status{};
    const int32_t polled = vd_scala_run_poll(static_cast<VdService *>(service_), job_, &status);
    if (status.output != nullptr && status.output_len > 0) {
        if (sink_) {
            sink_(QByteArray(status.output, static_cast<int>(status.output_len)));
        }
    }
    if (status.truncated != 0) {
        emit outputTruncated();
    }
    if (polled < 0) {
        // POLL_MISUSE (-1, no exported constant): the slot is gone. There is no
        // engine state left to report, so the lane says exactly that.
        vd_scala_run_status_free(&status);
        error_ = QCoreApplication::translate("VEdit", "the run job disappeared before it "
                                                      "reached a state.");
        finish(RunState::Failed, -1);
        return;
    }
    if (polled == VD_STATUS_PENDING) {
        vd_scala_run_status_free(&status);
        return;
    }
    const int stateCode = status.state;
    const int exitCode = status.exit_code;
    if (stateCode == static_cast<int>(RunState::Failed)) {
        VdError failure{};
        if (vd_service_error(static_cast<VdService *>(service_), &failure) != 0) {
            error_ = QString::fromUtf8(failure.message);
        }
    }
    vd_scala_run_status_free(&status);
    finish(stateFromCode(stateCode), exitCode);
#endif
}

void ScalaRun::finish(RunState state, int exitCode)
{
    state_ = state;
    exitCode_ = exitCode;
    const QString program = program_;
    releaseJob();
    program_.clear();
    emit finished(static_cast<int>(state), exitCode, program);
}

} // namespace vedit::workspace::scala
