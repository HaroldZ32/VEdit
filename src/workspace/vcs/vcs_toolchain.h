#pragma once

// Host executable and version diagnostics for the VCS panels.
#include <QString>
#include <QStringList>

class QWidget;

namespace vedit::workspace::vcs {

struct ToolchainStatus {
    QString appDir;
    QString gitPath;
    QString gitVersion;
    QString gitSource; // path | env | missing
    QString svnPath;
    QString svnVersion;
    QString svnSource; // path | env | missing

    bool complete() const { return !gitPath.isEmpty() && !svnPath.isEmpty(); }
};

/// The one machine-readable line (`VCS-TOOLCHAIN git=... version=... ...`).
QString toolchainStatusLine(const ToolchainStatus &status);

/// Runs the resolution + version probes (read-only; starts two processes).
ToolchainStatus detectToolchain();

/// Starts the startup diagnostics after applying the persisted environment.
void warmToolchainProbe();

/// Detects once, prints the line to stderr once, and returns the cached status.
const ToolchainStatus &reportToolchainOnce();

/// Applies the chosen toolchain to `widget` as the five probe-visible properties
/// (`veditVcsToolchain`, `veditVcsGitPath`, `veditVcsGitVersion`,
/// `veditVcsSvnPath`, `veditVcsSvnVersion`). The integration factory calls this
/// for the tabs and both panels; the lane can call it on any widget and assert
/// the SAME properties, so "the toolchain is observable" is tested without an
/// ApplicationShell.
void applyToolchainProperties(QWidget *widget, const ToolchainStatus &status);

} // namespace vedit::workspace::vcs
