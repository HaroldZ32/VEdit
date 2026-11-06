#pragma once

// Bundled VCS toolchain resolution for the VCS lane (batch 6a, rev3).
//
// The product ships git and subversion inside the AppDir, so it must decide
// WHICH binary it runs. The delivery order is **bundled > PATH > env** — the
// same order `crates/vd-vcs/src/toolchain.rs` implements for the read/write
// runners (that resolver decides what actually gets spawned; this one makes the
// choice observable). The lane prints one `VCS-TOOLCHAIN ...` line at init and
// exposes the values as panel properties, so an acceptance probe can assert the
// chosen toolchain without reading the implementation.
#include <QString>
#include <QStringList>

class QWidget;

namespace vedit::workspace::vcs {

struct ToolchainStatus {
    QString appDir;
    QString gitPath;
    QString gitVersion;
    QString gitSource; // bundled | path | env | missing
    QString svnPath;
    QString svnVersion;
    QString svnSource; // bundled | path | env | missing

    bool complete() const { return !gitPath.isEmpty() && !svnPath.isEmpty(); }
};

/// The one machine-readable line (`VCS-TOOLCHAIN git=... version=... ...`).
QString toolchainStatusLine(const ToolchainStatus &status);

/// Runs the resolution + version probes (read-only; starts two processes).
ToolchainStatus detectToolchain();

/// Detects once, prints the line to stderr once, and returns the cached status.
const ToolchainStatus &reportToolchainOnce();

/// Applies the chosen toolchain to `widget` as the five probe-visible properties
/// (`veditVcsToolchain`, `veditVcsGitPath`, `veditVcsGitVersion`,
/// `veditVcsSvnPath`, `veditVcsSvnVersion`). The integration factory calls this
/// for the tabs and both panels; the lane can call it on any widget and assert
/// the SAME properties, so "the toolchain is observable" is tested without an
/// ApplicationShell.
void applyToolchainProperties(QWidget *widget, const ToolchainStatus &status);

/// Testable pieces of the order (identical to the Rust resolver).
QString bundledTool(const QString &appDir, const QString &tool);
QString toolOnPath(const QString &tool);
QString toolFromEnv(const QString &tool);
QString toolVersion(const QString &path, const QStringList &arguments);

} // namespace vedit::workspace::vcs
