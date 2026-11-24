#pragma once

// Toolchain discovery + observability for the diagnostics lane.
//
// ONE resolution order for every runtime tool (the product-wide contract, the
// same order as `crate::provenance::ResolutionStage::ORDER` in vd-config):
//
//   1. explicit setting — the `tools.*` settings keys (`applyToolOverrides`, or
//      `ToolchainSearchContext::explicitPaths` when the caller has them);
//   2. bundled — a binary next to the product binary (`bin/` next to `vd-app`,
//      or the legacy `lib/vedit/bin/`);
//   3. local   — a SOURCE CHECKOUT's own toolchain outputs, so a bare build works
//      after one documented command: `<repo>/build/slang/out/slang`,
//      `<repo>/target/verible-pin/extract/verible-*/bin/`, and
//      `<repo>/.mpd/recon/verible/bin/` (the repo root is found by walking up
//      from the product binary until `scripts/fetch-verible.sh` is seen);
//   4. imported project — RESERVED: a variable imported from the PROJECT's shell
//      environment. NOT populated by the product (see the note below);
//   5. imported global  — RESERVED: the USER's global shell environment (project
//      wins over global). NOT populated by the product (see the note below);
//   6. PATH    — the user's shell PATH;
//   7. env     — `VEDIT_VERIBLE_LS` / `VEDIT_SLANG` (explicit override, last).
//
// NOTE (E5, batch-20261114): stages 4 and 5 are part of the declared order, but
// the product never fills them. The merged imported selection lives in the Rust
// service layer (`vd_deeplint::binary::set_imported_environment`) and the C ABI
// exposes no synchronous read, so `applicationToolchainContext()` passes empty
// maps and toolchain discovery cannot see imported variables. Imported variables
// reach lint/catalog/hierarchy child processes and filelist expansion only — not
// discovery. A caller that holds the selection may inject it; only the self-test
// does today.
//
// The order and the chosen source are REPORTED, never implicit: the status line
// names each tool's source (`settings|bundled|local|imported-project|imported-global|PATH|env:<name>|missing`)
// and a `missing` list, so a user who has no toolchain sees why diagnostics are
// unavailable, not a silently empty panel.

#include <QHash>
#include <QString>
#include <QStringList>

namespace vedit::workspace::diagnostics {

/// Which command-line tool is being resolved.
enum class ToolKind {
    Verible,       ///< `verible-verilog-ls` (diagnostics / navigation / outline)
    Slang,         ///< `slang` (deep lint)
    SlangServer,   ///< `slang-server` (the alternate LSP backend, T138/T11)
    VeribleFormat, ///< `verible-verilog-format` (formatting ALWAYS uses it)
    VeribleLint,   ///< `verible-verilog-lint` (the alternate lint engine, T138/T11)
    VhdlLs,        ///< `vhdl_ls` (the VHDL language server, T11)
    Ghdl,          ///< `ghdl` (VHDL analysis/elaboration, T11)
    Vsg,           ///< `vsg` (VHDL style guide, T11)
    // T12: the offline Scala toolchain the AppDir bundles. They are their own
    // group for the same reason the VHDL tools are: an absent Scala payload must
    // never flip the "diagnostics unavailable" status of the Verilog tools.
    ScalaJava,     ///< `java` (the bundled JDK 21)
    ScalaCli,      ///< `scala-cli` (static)
    Sbt,           ///< `sbt`
    Metals,        ///< `metals` (launched through the bundled coursier cache)
};

/// Where the resolved binary came from.
enum class ToolSource {
    Bundled,
    Local, ///< the source checkout's own artifact (`build/slang/out`, verible extract)
    Path,
    Env,
    Settings,         ///< an explicit path from `settings.toml` (`tools.*` keys)
    ImportedProject,  ///< RESERVED (the product never fills it): a project-imported variable
    ImportedGlobal,   ///< RESERVED (the product never fills it): a globally imported variable
    Missing,
};

/// The stable label used in status output: `bundled` / `PATH` / `env:<name>` /
/// `imported-project` / `imported-global` / `missing`.
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

/// The joint report for the toolchain (the visible observability surface).
///
/// `missing()` and `statusLine()` keep their historical meaning: they cover the
/// TWO tools the default configuration needs (`verible-verilog-ls`, `slang`).
/// The T138/T11 tools (`slang-server`, `verible-verilog-format`,
/// `verible-verilog-lint`) live in their own fields and are reported through
/// `auxiliaryMissing()` / `auxiliaryStatusLine()`, so an optional engine being
/// absent never flips the existing "diagnostics unavailable" status.
struct ToolchainReport {
    ToolResolution verible;
    ToolResolution slang;
    ToolResolution slangServer;
    ToolResolution veribleFormat;
    ToolResolution veribleLint;
    ToolResolution vhdlLs;
    ToolResolution ghdl;
    ToolResolution vsg;
    // T12: the offline Scala toolchain (own group, own status line).
    ToolResolution scalaJava;
    ToolResolution scalaCli;
    ToolResolution sbt;
    ToolResolution metals;

    /// The resolution for one kind (never nullptr; an unknown kind returns
    /// the `slang` slot so a caller can never dereference null).
    const ToolResolution &toolFor(ToolKind kind) const;

    /// Tool families with no executable, e.g. `{"verible", "slang"}`.
    QStringList missing() const;
    /// The T11 extra tools that are absent (`slang-server`, `verible-format`,
    /// `verible-lint`), for a backend-specific status line only.
    QStringList auxiliaryMissing() const;
    /// `aux source=slang-server:local verible-format:local verible-lint:local missing=[...]`
    QString auxiliaryStatusLine() const;
    /// `toolchain source=verible:bundled slang:missing missing=[slang]` style line.
    QString statusLine() const;
    /// The T11 VHDL tools with no executable.
    QStringList vhdlMissing() const;
    /// `source=vhdl_ls:bundled ghdl:bundled vsg:bundled missing=[]` — the VHDL
    /// half of the same resolver, reported with its own chosen source.
    QString vhdlStatusLine() const;
    /// AC-6.7: what the missing VHDL tools disable (empty when nothing is
    /// missing). The VHDL-specific counterpart of impact(): a missing GHDL must
    /// never be described as a missing Verilog engine.
    QString vhdlImpact() const;
    /// AC-6.7: how the user can supply the missing VHDL tools (empty when
    /// nothing is missing).
    QString vhdlFixHint() const;
    /// AC-6.7: the ONE human sentence for a missing VHDL tool — what is
    /// missing, what it disables and how to fix it. Single line, no probe wall;
    /// empty (silent) when every VHDL tool is present.
    QString vhdlUserMessage() const;
    /// The T12 Scala tools with no executable.
    QStringList scalaMissing() const;
    /// `source=java:bundled scala-cli:bundled sbt:bundled metals:bundled
    /// missing=[]` — the Scala half of the same resolver (T12).
    QString scalaStatusLine() const;
    /// What the missing tools disable (empty when nothing is missing).
    QString impact() const;
    /// How the user can supply the missing tools (empty when nothing is missing).
    QString fixHint() const;
    /// A human sentence for the status bar / diagnostics panel.
    QString userMessage() const;
    /// The per-tool explanation, e.g. `verible-verilog-ls not found (...)`.
    /// Deliberately short: the probed candidate list lives in probeDetail().
    QString missingMessage(ToolKind kind) const;
    /// Every candidate probed, per tool — for logs/selfcheck, never for the UI.
    QString probeDetail() const;
};

/// Inputs of one resolution (injectable so the self-test is deterministic).
///
/// The fields are the stages of the product-wide order, in order: `explicitPaths`
/// (settings) -> `bundledDirs` -> `localDirs` -> `importedProjectVars` ->
/// `importedGlobalVars` -> `pathDirs` -> `env`.
struct ToolchainSearchContext {
    QStringList bundledDirs;            ///< directories probed as "bundled"
    QStringList localDirs;              ///< the source checkout's own toolchain dirs
    QStringList pathDirs;               ///< PATH entries
    QHash<QString, QString> env;        ///< process-environment override variables
    QHash<QString, QString> explicitPaths;       ///< `tools.*` settings, by canonical name
    // RESERVED, never filled by `applicationToolchainContext()`: the product
    // keeps the merged imported selection in the Rust service layer and no
    // synchronous C ABI accessor exists (E5, batch-20261114). Only an injected
    // context (the self-tests) populates them — do not turn them into a
    // user-facing promise.
    QHash<QString, QString> importedProjectVars; ///< reserved (the product leaves this empty)
    QHash<QString, QString> importedGlobalVars;  ///< reserved (the product leaves this empty)
    QString veribleName = QStringLiteral("verible-verilog-ls");
    QString slangName = QStringLiteral("slang");
    QString slangServerName = QStringLiteral("slang-server");
    QString veribleFormatName = QStringLiteral("verible-verilog-format");
    QString veribleLintName = QStringLiteral("verible-verilog-lint");
    QString vhdlLsName = QStringLiteral("vhdl_ls");
    QString ghdlName = QStringLiteral("ghdl");
    QString vsgName = QStringLiteral("vsg");
    QString scalaJavaName = QStringLiteral("java");
    QString scalaCliName = QStringLiteral("scala-cli");
    QString sbtName = QStringLiteral("sbt");
    QString metalsName = QStringLiteral("metals");
};

/// Resolve both tools against `context` (pure; no process spawning).
///
/// Implements the product-wide order over the context's stages; an absent stage
/// is simply skipped, and the chosen source is reported per tool.
ToolchainReport resolveToolchain(const ToolchainSearchContext &context);

/// The real application context: bundled dirs next to the product binary, the
/// process PATH and the override environment variables.
ToolchainSearchContext applicationToolchainContext();

/// Apply explicit user paths (the `tools.*` settings keys) — stage 1 of the
/// product-wide order — on top of an already resolved report.
///
/// This is the late form of the explicit-setting stage for a caller that reads
/// its settings after resolution (the settings library lives in another lane);
/// `ToolchainSearchContext::explicitPaths` is the early form. Keys are canonical
/// tool names (`verible-verilog-ls`, `slang`, `slang-server`,
/// `verible-verilog-format`, `verible-verilog-lint`); an empty or
/// non-executable path is recorded as a failed probe and keeps the resolved tool.
void applyToolOverrides(ToolchainReport *report, const QHash<QString, QString> &paths);

/// Format `text` with the given `verible-verilog-format` binary (stdin -> stdout,
/// synchronous; the GUI calls this on an explicit action only). Formatting ALWAYS
/// uses this binary, never the LSP backend. Returns false with `error` filled.
bool formatVerilog(const QString &formatterPath, const QString &text, QString *formatted,
                   QString *error);

/// `TOOLCHAIN ...` machine line for `--selfcheck` and evidence logs.
QString toolchainStatusLine();

} // namespace vedit::workspace::diagnostics
