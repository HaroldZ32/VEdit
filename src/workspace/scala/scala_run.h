#pragma once

// One-click Scala run: the project-shape rules and the job driver of the
// `scala` workspace lane (batch 20261110, AC-7.5 / AC-7.10).
//
// The lane is split in two halves on purpose:
//
//   * `detectProjectShape()` and `buildRunPlan()` are PURE functions over a
//     project root, so the three shapes AC-7.10 names can be asserted without a
//     process, a toolchain or a window;
//   * `ScalaRun` drives exactly ONE run through the frozen C ABI
//     (`vd_scala_run_start` → `vd_scala_run_poll` → `vd_scala_run_release`). It
//     never spawns anything itself: the Rust engine owns the child, and this
//     class only polls it from a QTimer, hands every chunk to its sink and
//     reports the terminal state. That is why the GUI thread can never block on
//     `sbt` — the ABI's `start` returns an id immediately.
//
// The output sink is a `std::function`, not a terminal widget: the lane core stays
// free of the shell/panel types, and the integration binds the sink to the
// existing Terminal panel (AC-7.5).
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace vedit::workspace::scala {

/// The project shapes AC-7.10 distinguishes.
enum class ProjectShape {
    Sbt,      ///< `build.sbt` at the root — offline `sbt run`.
    ScalaCli, ///< scala-cli directives or a bare `*.scala` — offline `scala-cli run`.
    Unknown,  ///< neither — an explicit error, never a silent no-op.
};

/// What was found under one project root.
struct ShapeReport {
    ProjectShape shape = ProjectShape::Unknown;
    QString detail;       ///< why this shape, or exactly what is missing
    QStringList evidence; ///< the deciding paths, relative to the root
};

/// Detect the shape of `root`. Pure: no process, no shell, no toolchain.
///
/// The order is AC-7.10's: `build.sbt` wins, then `project.scala` (the scala-cli
/// directives file), `scala-cli.conf`, or any `*.scala` below the root. The walk
/// is BOUNDED (depth 3, a fixed entry budget, and `.git` / `target` /
/// `.scala-build` / `node_modules` / `.bsp` / `.metals` skipped), so a huge tree
/// costs a directory read or two and never a full traversal.
ShapeReport detectProjectShape(const QString &root);

/// The absolute tool paths a run needs; an empty field means "not resolved".
struct ScalaTools {
    QString scalaCli; ///< `scala-cli` (the AppDir wrapper, so `--power` is set)
    QString sbt;      ///< `sbt` (the AppDir wrapper adds the offline cache homes)
    QString java;     ///< the JDK launcher (reported only; the wrappers set JAVA_HOME)
};

/// One prepared run: the exact argv/env the C ABI receives.
struct RunPlan {
    bool ok = false;
    QString error;    ///< filled when `ok == false`
    QString program;  ///< argv[0], for the banner and the ABI's `tool`
    QStringList argv; ///< complete argv (`argv[0]` == program)
    QStringList env;  ///< NAME=VALUE entries on top of the sanitized child env
    QString root;
    ShapeReport shape;
};

/// Build the argv/env for `shape` under `root`.
///
///   * Sbt:      `<sbt> -Dsbt.offline=true run`
///   * ScalaCli: `<scala-cli> run --offline --server=false --jvm system <root>`
///   * Unknown:  `ok = false`, with `error` naming both things a user can add.
///
/// A binary the shape needs but the toolchain did not resolve is the SAME kind of
/// explicit error: the lane never pretends a run started. The bundled wrappers
/// (`bin/scala-cli`, `bin/sbt`) carry the offline homes and `--power` themselves;
/// the flags and `COURSIER_MODE=offline` here keep a user's own PATH tool offline
/// too.
RunPlan buildRunPlan(const QString &root, const ShapeReport &shape, const ScalaTools &tools);

/// The states of one run. The values 0..4 ARE the C ABI's `state` codes; `Idle`
/// is lane-local (a slot that never started a run).
enum class RunState {
    Idle = -1,
    Running = 0,
    Exited = 1,
    Failed = 2,
    Cancelled = 3,
    TimedOut = 4,
};

/// One run, driven through the frozen C ABI.
class ScalaRun final : public QObject {
    Q_OBJECT

public:
    /// Receives every output chunk, in order, exactly once.
    using OutputSink = std::function<void(const QByteArray &)>;

    explicit ScalaRun(QObject *parent = nullptr);
    ~ScalaRun() override;

    void setOutputSink(OutputSink sink) { sink_ = std::move(sink); }

    /// Start `plan`. Returns false with `error()` filled when the plan is
    /// unusable, a run is already live, or this build has no Rust service layer
    /// (the explicit "cannot run" refusal, never a fake run).
    bool start(const RunPlan &plan);

    /// Ask the engine to stop. The engine kills the child's whole process tree;
    /// this keeps polling until the run reports `Cancelled`.
    void cancel();

    bool isRunning() const { return job_ != 0; }
    RunState state() const { return state_; }
    int exitCode() const { return exitCode_; }
    QString program() const { return program_; }
    QString error() const { return error_; }

signals:
    /// Emitted once per run when it reaches a terminal state. `stateCode` is the
    /// C ABI's code (1 exited, 2 failed, 3 cancelled, 4 timed out).
    void finished(int stateCode, int exitCode, const QString &program);
    /// The engine dropped older output because the run produced more than it
    /// keeps. The UI must say so instead of silently showing a gap.
    void outputTruncated();

private:
    void poll();
    void finish(RunState state, int exitCode);
    void releaseJob();

    OutputSink sink_;
    QTimer *timer_ = nullptr;
    quint64 job_ = 0;
    void *service_ = nullptr; // struct VdService*, owned here, created lazily
    RunState state_ = RunState::Idle;
    int exitCode_ = -1;
    QString program_;
    QString error_;
};

} // namespace vedit::workspace::scala
