#pragma once

// Toolchain discovery + observability for the diagnostics lane.
//
// Discovery order (the delivery contract): **bundled > PATH > env**.
//   * bundled — a binary next to the product binary (`bin/` next to `vd-app`,
//     or the legacy `lib/vedit/bin/`);
//   * PATH    — the user's shell PATH;
//   * env     — `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` (explicit override, last).
//
// The order and the chosen source are REPORTED, never implicit: the status line
// names each tool's source (`bundled|PATH|env:<name>|missing`) and a `missing`
// list, so a user who has no toolchain sees why diagnostics are unavailable, not
// a silently empty panel.

#include <QHash>
#include <QString>
#include <QStringList>

namespace vedit::workspace::diagnostics {

/// Which command-line tool is being resolved.
enum class ToolKind {
    Verible, ///< `verible-verilog-ls` (diagnostics / navigation / outline)
    Slang,   ///< `slang` (deep lint)
};

/// Where the resolved binary came from.
enum class ToolSource {
    Bundled,
    Path,
    Env,
    Missing,
};

/// The stable label used in status output: `bundled` / `PATH` / `env:<name>` / `missing`.
QString toolSourceLabel(ToolSource source, const QString &envName = QString());

/// Outcome of resolving one tool.
struct ToolResolution {
    ToolKind kind = ToolKind::Verible;
    QString name;                            ///< file name (`verible-verilog-ls`, `slang`)
    QString path;                            ///< absolute path when found
    ToolSource source = ToolSource::Missing; ///< chosen source
    QString envName;                         ///< the env variable used (source == Env)
    QStringList searched;                    ///< every candidate probed, in order

    /// True when a usable executable was found.
    bool found() const { return source != ToolSource::Missing && !path.isEmpty(); }
    /// `bundled` / `PATH` / `env:NAME` / `missing`.
    QString sourceLabel() const { return toolSourceLabel(source, envName); }
    /// One-line description for logs/UI.
    QString describe() const;
};

/// The joint report for both tools (the visible observability surface).
struct ToolchainReport {
    ToolResolution verible;
    ToolResolution slang;

    /// Tool families with no executable, e.g. `{"verible", "slang"}`.
    QStringList missing() const;
    /// `toolchain source=verible:bundled slang:missing missing=[slang]` style line.
    QString statusLine() const;
    /// What the missing tools disable (empty when nothing is missing).
    QString impact() const;
    /// How the user can supply the missing tools (empty when nothing is missing).
    QString fixHint() const;
    /// A human sentence for the status bar / diagnostics panel.
    QString userMessage() const;
    /// The per-tool explanation, e.g. `verible-verilog-ls not found (...)`.
    QString missingMessage(ToolKind kind) const;
};

/// Inputs of one resolution (injectable so the self-test is deterministic).
struct ToolchainSearchContext {
    QStringList bundledDirs;            ///< directories probed as "bundled"
    QStringList pathDirs;               ///< PATH entries
    QHash<QString, QString> env;        ///< override variables
    QString veribleName = QStringLiteral("verible-verilog-ls");
    QString slangName = QStringLiteral("slang");
};

/// Resolve both tools against `context` (pure; no process spawning).
ToolchainReport resolveToolchain(const ToolchainSearchContext &context);

/// The real application context: bundled dirs next to the product binary, the
/// process PATH and the override environment variables.
ToolchainSearchContext applicationToolchainContext();

/// `TOOLCHAIN ...` machine line for `--selfcheck` and evidence logs.
QString toolchainStatusLine();

} // namespace vedit::workspace::diagnostics
