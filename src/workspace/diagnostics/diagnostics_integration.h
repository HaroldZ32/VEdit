#pragma once

// Entry point of the diagnostics lane, called by src/workspace/integration.cpp
// (the generic workspace-lane seam; see src/workspace/integration.h):
//
//   * CMake target: `vedit_workspace_diagnostics`
//   * entry point:  `void installDiagnostics(ApplicationShell &, const IntegrationOptions &)`
//
// It registers the "outline" panel factory, a "project.reparse" command, and
// starts the project-model job that feeds the controller with the effective file
// set (A3c). Navigation entry points are exposed by DiagnosticsController for
// the editor lane; the controller is reachable from the shell as the property
// `vedit.diagnosticsController` (a `QObject *` holder).
//
// T11 (AC-1.2/1.3/1.4/1.6): the backend/lint-engine selectors and the capability
// gating live here as a TESTABLE core (`BackendSwitchState`) that depends on no
// widget: the shell integration drives it, the evidence harness drives it, and
// both read the same decisions.
#include "../integration.h"

#include "lsp_backend.h"
#include "slang_backend.h"
#include "toolchain.h"

#include <QString>
#include <QStringList>

namespace vedit::app {
class ApplicationShell;
}

namespace vedit::workspace {

namespace diagnostics {
class DiagnosticsController;
}

/// The user's backend choices (the `tools.*` settings keys; the key spellings
/// are owned by the settings lane, see `settings_keys.h`).
struct DiagnosticsBackendSettings {
    QString lspBackend = QStringLiteral("verible"); ///< `verible` / `slang`
    QString lintEngine = QStringLiteral("slang");    ///< `slang` / `verible`
    QString veribleLs;     ///< `tools.verible_ls`
    QString slang;         ///< `tools.slang`
    QString slangServer;   ///< `tools.slang_server`
    QString veribleFormat; ///< `tools.verible_format`
    QString veribleLint;   ///< `tools.verible_lint` (when the page exposes it)
    QString veribleRules;  ///< `tools.verible_rules`
    QStringList includeDirs; ///< `tools.include_dirs`
    QStringList defines;     ///< `tools.defines`
};

/// One editor action's gating decision (AC-1.3).
struct CapabilityDecision {
    QString action;
    bool enabled = false;
    /// False when the backend table says `Unknown`: "connect to know" instead of
    /// pretending either way.
    bool known = true;
    /// `backend` / `always-verible-format` / `editor-synthesised`.
    QString source;
    QString reason;
};

/// T11 gating rules, in ONE place, from the ABI table (never a hardcoded copy):
///   * formatting is ALWAYS available via `verible-verilog-format` (it is not a
///     backend capability; the row is ignored, the formatter binary decides);
///   * signatureHelp is editor-synthesised (T16): not gated by the table, and
///     absent (disabled, with that reason) until the synthesiser exists;
///   * any other action follows the measured/live row: 1 enabled, 0 disabled with
///     the row's reason, -1 unknown with "connect to know".
CapabilityDecision decideActionGate(const diagnostics::BackendCapabilities &capabilities,
                                    const QString &action,
                                    bool formatterFound,
                                    bool editorSignatureHelpAvailable = false);

/// The switchable backend pair plus the selected toolchain. No widgets, no
/// application shell: both the product integration and the evidence harness own
/// an instance. `apply()` selects; the caller restarts the analysis (the
/// controller's reparse()/analyzeFile()) so a switch never recreates a panel.
class BackendSwitchState final {
public:
    BackendSwitchState(diagnostics::LspSessionBackend *lsp, diagnostics::SlangBackend *lint);

    /// Apply `settings` on top of `toolchain`. Returns false + `error` when the
    /// selected server/engine binary cannot be resolved (the previous selection
    /// is kept: a switch never leaves a half-configured pair).
    bool apply(const DiagnosticsBackendSettings &settings,
               const diagnostics::ToolchainReport &toolchain,
               QString *error);

    /// T29/AC-8.1: prime the state with the toolchain the shell already resolved.
    /// A CLI capability (formatting) is a TOOLCHAIN fact, not a settings fact: on
    /// a first launch with no `settings.toml` nothing calls apply() yet, and the
    /// gate must still answer from the real probe instead of claiming a present
    /// formatter is missing. A later apply() replaces this report (overrides
    /// included), so seeding never masks a settings-provided path.
    void seedToolchain(const diagnostics::ToolchainReport &toolchain);

    /// RM-2: re-pin the CURRENT project's `-I`/`-D` on the selected backends.
    ///
    /// The session environment comes from `selectBackend`/`selectEngine`, and
    /// project switches used to leave the previous project's values in place:
    /// every session started after File > Open Folder ran with the OLD
    /// include dirs and defines (the user-visible "environment does not
    /// refresh"). The wiring calls this with the new root's values when its
    /// file set arrives, and with two empty lists the moment a switch starts
    /// (so a session started mid-switch cannot inherit the old values either).
    ///
    /// A non-empty project list wins over the tools-page value, exactly like
    /// `buildLintRequest()`: the project expansion already APPENDS
    /// `tools.include_dirs` / `tools.defines`, so the project's list is the
    /// complete one. With nothing discovered yet the settings values stay in
    /// force. Returns false only when this build has no LSP backend.
    bool setProjectEnvironment(const QStringList &includeDirs, const QStringList &defines);
    /// What the backends currently hold as the project environment (observable).
    const QStringList &projectIncludeDirs() const { return projectIncludeDirs_; }
    const QStringList &projectDefines() const { return projectDefines_; }

    diagnostics::LspBackendKind lspKind() const { return lspKind_; }
    diagnostics::LintEngineKind lintKind() const { return lintKind_; }
    QString lspBackendName() const;
    QString lintEngineName() const;
    /// `backend=slang lint=verible-verilog-lint lsp=<path> engine=<path>`
    QString statusLine() const;
    /// The selected server/lint binary paths (what setConfiguration()/the pass
    /// request need); empty before the first successful apply().
    const QString &lspServerPath() const { return lspPath_; }
    const QString &lintEnginePath() const { return lintPath_; }
    const QString &rulesPath() const { return rulesPath_; }

    /// The exact LSP argv of the selected backend (AC-2.5's echo).
    QString commandLine() const;
    diagnostics::BackendCapabilities capabilities() const;
    CapabilityDecision gate(const QString &action) const;
    const diagnostics::ToolchainReport &toolchain() const { return toolchain_; }

private:
    /// Project values when the project declared any, else the tools-page ones.
    QStringList effectiveIncludeDirs() const;
    QStringList effectiveDefines() const;

    diagnostics::LspSessionBackend *lsp_ = nullptr;
    diagnostics::SlangBackend *lint_ = nullptr;
    diagnostics::LspBackendKind lspKind_ = diagnostics::LspBackendKind::Verible;
    diagnostics::LintEngineKind lintKind_ = diagnostics::LintEngineKind::Slang;
    QString lspPath_;
    QString lintPath_;
    QString rulesPath_;
    QStringList settingsIncludeDirs_; ///< the last tools-page `-I`
    QStringList settingsDefines_;     ///< the last tools-page `-D`
    QStringList projectIncludeDirs_;  ///< RM-2: the CURRENT root's `-I`
    QStringList projectDefines_;      ///< RM-2: the CURRENT root's `-D`
    diagnostics::ToolchainReport toolchain_;
};

void installDiagnostics(vedit::app::ApplicationShell &shell, const IntegrationOptions &options);

/// The live diagnostics controller of `shell`, or nullptr before
/// installDiagnostics() ran (and in a build without the app-shell seam).
///
/// RM-9: the product-level regressions must be able to install a deterministic
/// lint backend BEFORE the first event-loop turn - the project-open pass starts
/// as soon as the file set is adopted, and a harness that could only reach the
/// controller after pumping events would race it. Same controller instance the
/// panel uses; nothing is created here.
diagnostics::DiagnosticsController *diagnosticsControllerForShell(
    const vedit::app::ApplicationShell &shell);

} // namespace vedit::workspace
