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

#include <QtGlobal>
#include <QString>
#include <QStringList>

namespace vedit::app {
class ApplicationShell;
}

class QWidget;

namespace vedit::workspace {

/// BLOCKER fix (batch 20261110d): hard cap on the source the 400 ms active-file
/// watcher will read or copy for ONE file. Above it the watcher copies neither
/// the document nor the file and reports one named "analysis disabled" line;
/// below it the watcher's own allocations are a small multiple of this value,
/// independent of the document size.
///
/// 4 MiB is deliberate: the analysis engines are meant for hand-written HDL
/// (every source in this repository is far below 1 MiB), while a generated or
/// pasted multi-hundred-MB file is exactly the input that used to make the
/// watcher ask for >1.5 GB in a single event-loop callback. 4 MiB keeps a real
/// generated netlist analysable (4× the largest source here) while bounding the
/// watcher path at ~5×4 MiB = 20 MiB (disk bytes + UTF-8 QString + document
/// copy); see `src/workspace/diagnostics/README.md` "Active-file watcher cap".
inline constexpr qint64 kActiveFileWatchByteCap = 4LL * 1024 * 1024;

/// batch-20261116 L2.1: the project file-set expansion's SOFT timeout. The old
/// poller gave up after 10 s (`++ticks_ > 200` at 50 ms) and dropped the job, so
/// a 20k-file project stayed an empty set for the whole session. This matches the
/// module-tree catalog's 300 s; after it the poller keeps polling the SAME job at
/// a 1 s backoff instead of discarding it, so a slow expansion still delivers.
inline constexpr int kProjectExpandSoftTimeoutMs = 300000;
/// The soft timeout in force: the constant above, or the
/// `VEDIT_DIAGNOSTICS_EXPAND_SOFT_TIMEOUT_MS` test override (>= 0).
int projectExpandSoftTimeoutMs();

namespace diagnostics {
class DiagnosticsController;
}

/// The user's backend choices (the `tools.*` settings keys; the key spellings
/// are owned by the settings lane, see `settings_keys.h`).
struct DiagnosticsBackendSettings {
    QString lspBackend = QStringLiteral("slang"); ///< `verible` / `slang`
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

/// Why one axis of the badge differs from the request (T9 / AC-3.4).
/// Machine-readable on purpose: the badge (T14) branches on the code and shows
/// `reasonMessage` verbatim; it never parses prose.
enum class BadgeReasonCode {
    Ok = 0,        ///< effective == requested
    UnknownValue,  ///< the store spelling is not a backend/engine; the default applies
    ToolMissing,   ///< the requested server/engine binary is not resolvable
    SelectFailed,  ///< the backend refused the selection (its own error text)
    NoBackend,     ///< this build links no LSP backend at all
};

/// The stable token machine lines / selftests print: `ok` / `unknown-value` /
/// `tool-missing` / `select-failed` / `no-backend`.
QString badgeReasonCodeName(BadgeReasonCode code);

/// One axis (LSP backend or lint engine) of the status badge.
struct BadgeAxis {
    QString requested;     ///< canonical token the store asks for (unknown: as written)
    QString effective;     ///< what the selection really is
    QString path;          ///< the binary in use; empty when none was resolved
    /// The request is really in force: the selected kind (and, for an LSP
    /// backend, its name) matches what the store asked for.
    bool applied = false;
    BadgeReasonCode reason = BadgeReasonCode::Ok;
    QString reasonMessage; ///< the sentence; empty when reason == Ok
};

/// The badge facts T14 renders (AC-3.2/3.3/3.4/3.5). DATA ONLY: no widget and no
/// timer - the value comes from the real Rust/C++ selection state, so the badge
/// can never show a pair the process is not actually running.
struct BadgeState {
    BadgeAxis lsp;
    BadgeAxis lint;
    /// `measured` / `live` / `declared`; `unknown` when the session has not
    /// answered yet (never silently upgraded).
    QString basis;
    /// The active file's project-registry language id ("" = not a known source
    /// language, e.g. no file open).
    QString language;
    /// The backend that serves `language`: the selected LSP backend for
    /// Verilog/SystemVerilog, `vhdl_ls` for VHDL (AC-3.5). Empty when the tool
    /// for that language is missing, so the UI never names a backend that
    /// cannot run.
    QString languageBackend;
    /// `VdSettings.warnings` from the store load, forwarded verbatim (AC-T9.1):
    /// unknown/invalid/refused values explain themselves instead of being
    /// dropped on the floor.
    QStringList warnings;

    /// `requested→effective · reason` when a pair differs, else the effective
    /// pair. Ready for a status line; the badge widget itself is T14's.
    QString summary() const;
};

/// The switchable backend pair plus the selected toolchain. No widgets, no
/// application shell: both the product integration and the evidence harness own
/// an instance. `apply()` selects; the caller restarts the analysis (the
/// controller's reparse()/analyzeFile()) so a switch never recreates a panel.
class BackendSwitchState final {
public:
    BackendSwitchState(diagnostics::LspSessionBackend *lsp, diagnostics::SlangBackend *lint);

    /// Apply `settings` on top of `toolchain`. Returns false + `error` when ANY
    /// requested axis cannot be applied; the two axes are applied INDEPENDENTLY
    /// (AC-T9.2), so a missing language server never cancels a valid lint-engine
    /// change and vice versa. What did not apply keeps its previous, working
    /// selection - a switch never leaves a half-configured pair, and a failed
    /// request is visible through badge() (requested vs effective + reason).
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

    /// T9 / AC-3.4: requested vs effective for BOTH axes, the capability basis,
    /// a machine-readable reason when a pair differs, and the per-language
    /// route (AC-3.5). `languageId` is the active file's project-registry id
    /// ("" when no file/unknown); "vhdl" answers with the vhdl_ls route instead
    /// of the Verilog backend. Data only - no widget is touched here.
    BadgeState badge(const QString &languageId = QString()) const;

    /// T9 / AC-T9.1: keep the `VdSettings.warnings` a store load produced, so
    /// badge() can explain a refused/unknown value instead of dropping it.
    void setLoadWarnings(const QStringList &warnings);

private:
    /// Project values when the project declared any, else the tools-page ones.
    QStringList effectiveIncludeDirs() const;
    QStringList effectiveDefines() const;

    diagnostics::LspSessionBackend *lsp_ = nullptr;
    diagnostics::SlangBackend *lint_ = nullptr;
    diagnostics::LspBackendKind lspKind_ = diagnostics::LspBackendKind::Verible;
    diagnostics::LintEngineKind lintKind_ = diagnostics::LintEngineKind::Slang;
    /// What the LAST apply() asked for (canonical; an unknown spelling is kept
    /// as written so the badge can show `emacs→slang`). Defaults mirror the
    /// documented default pair, so a first launch has a coherent request.
    QString requestedLsp_ = QStringLiteral("slang");
    QString requestedLint_ = QStringLiteral("slang");
    BadgeReasonCode lspReason_ = BadgeReasonCode::Ok;
    BadgeReasonCode lintReason_ = BadgeReasonCode::Ok;
    QString lspReasonMessage_;
    QString lintReasonMessage_;
    QStringList loadWarnings_;
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

/// AC-4.5 test seam: install the Project Lint panel's CONTENT into `host`, which
/// `installDiagnostics` takes from `ApplicationShell::projectLintHost()`.
///
/// The shell always creates that host, so the missing-host branch (no crash,
/// exactly ONE explicit shell diagnostic saying the panel cannot be built) is
/// reachable in a test only by calling this directly with nullptr - which is what
/// `vedit-project-lint-e2e` does. Returns false when no host was given; the
/// caller then publishes `vedit.diagnostics.projectLintPanel` as false.
bool installProjectLintContents(QWidget *host, vedit::app::ApplicationShell &shell,
                                diagnostics::DiagnosticsController *controller);

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
