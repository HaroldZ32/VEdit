//! Backend identity and the capability table (T138).
//!
//! VEdit can drive two language servers. The *identity* decides how the process
//! is started ([`crate::LspConfig`]); the *capability table* decides which
//! actions the UI may offer. A capability is never guessed silently: every row
//! says whether it is supported and, when it is not, why — and every table says
//! how it was established ([`CapabilityBasis`]).
//!
//! # Where the numbers come from
//!
//! The Verible rows are **measured**: `verible-verilog-ls v0.0-4298-g085eef05`
//! answers `initialize` with exactly
//! `definitionProvider`, `referencesProvider`, `documentSymbolProvider`,
//! `renameProvider`, `documentHighlightProvider`, `documentFormattingProvider`,
//! `documentRangeFormattingProvider`, `codeActionProvider`,
//! `diagnosticProvider`, incremental `textDocumentSync` — and with
//! `hoverProvider: false` and no `completionProvider` / `signatureHelpProvider` /
//! `inlayHintProvider`. The transcript is `evidence/w1-rust-engine/ls-capabilities.json`.
//!
//! The slang-server rows are **measured** against the source-built
//! `slang-server v0.3.1+2ad996f` (evidence/w2-spike/04-built-initialize-handshake.txt):
//! it advertises definition, references, documentSymbol, rename,
//! documentHighlight, codeAction, hover, completion, inlayHint, callHierarchy
//! and executeCommand, and it has **no** `documentFormattingProvider`,
//! `documentRangeFormattingProvider` or `signatureHelpProvider` — the exact
//! opposite of verible, which can format but has no hover/completion. The live
//! handshake refines both tables once a session is ready
//! ([`capabilities_from_server`]).

use lsp_types::{CodeActionProviderCapability, HoverProviderCapability, OneOf, ServerCapabilities};

/// Which language server a session drives.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LspBackend {
    /// Verible's `verible-verilog-ls` (the only backend before T138).
    #[default]
    Verible,
    /// slang's `slang-server`.
    Slang,
}

impl LspBackend {
    /// Stable code for the C ABI: 0 verible, 1 slang.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Verible => 0,
            Self::Slang => 1,
        }
    }

    /// Parse the C ABI code; unknown codes are rejected, never defaulted.
    #[must_use]
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Verible),
            1 => Some(Self::Slang),
            _ => None,
        }
    }

    /// Parse a settings value (`verible` / `slang`, case-insensitive aliases).
    #[must_use]
    pub fn parse(text: &str) -> Option<Self> {
        match text.trim().to_ascii_lowercase().as_str() {
            "verible" | "verible-verilog-ls" | "verible_ls" => Some(Self::Verible),
            "slang" | "slang-server" | "slang_server" => Some(Self::Slang),
            _ => None,
        }
    }

    /// Short name used in the status line (`backend=slang`).
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Verible => "verible",
            Self::Slang => "slang",
        }
    }

    /// The server executable this backend drives.
    #[must_use]
    pub const fn binary_name(self) -> &'static str {
        match self {
            Self::Verible => "verible-verilog-ls",
            Self::Slang => "slang-server",
        }
    }

    /// Human-readable name for the settings page and messages.
    #[must_use]
    pub const fn display_name(self) -> &'static str {
        match self {
            Self::Verible => "verible-verilog-ls",
            Self::Slang => "slang-server",
        }
    }
}

/// One user-visible action the editor can route to a language server.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LspAction {
    /// Go to definition (`textDocument/definition`).
    Definition,
    /// Find references (`textDocument/references`).
    References,
    /// Outline (`textDocument/documentSymbol`).
    DocumentSymbol,
    /// Rename (`textDocument/rename`).
    Rename,
    /// Highlight occurrences (`textDocument/documentHighlight`).
    DocumentHighlight,
    /// Format the whole document.
    Formatting,
    /// Format a range.
    RangeFormatting,
    /// Code actions (`textDocument/codeAction`).
    CodeAction,
    /// Diagnostics of an open document.
    Diagnostics,
    /// Hover information.
    Hover,
    /// LSP completion (FIM is a separate path).
    Completion,
    /// Signature help.
    SignatureHelp,
    /// Inlay hints.
    InlayHint,
}

impl LspAction {
    /// Every action, in the stable C ABI order.
    pub const ALL: [Self; 13] = [
        Self::Definition,
        Self::References,
        Self::DocumentSymbol,
        Self::Rename,
        Self::DocumentHighlight,
        Self::Formatting,
        Self::RangeFormatting,
        Self::CodeAction,
        Self::Diagnostics,
        Self::Hover,
        Self::Completion,
        Self::SignatureHelp,
        Self::InlayHint,
    ];

    /// Stable code for the C ABI (index into [`Self::ALL`]).
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Definition => 0,
            Self::References => 1,
            Self::DocumentSymbol => 2,
            Self::Rename => 3,
            Self::DocumentHighlight => 4,
            Self::Formatting => 5,
            Self::RangeFormatting => 6,
            Self::CodeAction => 7,
            Self::Diagnostics => 8,
            Self::Hover => 9,
            Self::Completion => 10,
            Self::SignatureHelp => 11,
            Self::InlayHint => 12,
        }
    }

    /// Parse a C ABI action code.
    #[must_use]
    pub const fn from_code(code: i32) -> Option<Self> {
        match code {
            0 => Some(Self::Definition),
            1 => Some(Self::References),
            2 => Some(Self::DocumentSymbol),
            3 => Some(Self::Rename),
            4 => Some(Self::DocumentHighlight),
            5 => Some(Self::Formatting),
            6 => Some(Self::RangeFormatting),
            7 => Some(Self::CodeAction),
            8 => Some(Self::Diagnostics),
            9 => Some(Self::Hover),
            10 => Some(Self::Completion),
            11 => Some(Self::SignatureHelp),
            12 => Some(Self::InlayHint),
            _ => None,
        }
    }

    /// Stable settings/UI name.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::Definition => "definition",
            Self::References => "references",
            Self::DocumentSymbol => "documentSymbol",
            Self::Rename => "rename",
            Self::DocumentHighlight => "documentHighlight",
            Self::Formatting => "formatting",
            Self::RangeFormatting => "rangeFormatting",
            Self::CodeAction => "codeAction",
            Self::Diagnostics => "diagnostics",
            Self::Hover => "hover",
            Self::Completion => "completion",
            Self::SignatureHelp => "signatureHelp",
            Self::InlayHint => "inlayHint",
        }
    }
}

/// Whether an action is available on one backend.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Support {
    /// The backend advertises it (measured) or implements it (declared).
    Supported,
    /// The backend does not advertise it; [`ActionSupport::reason`] says so.
    Unsupported,
    /// Not measured in this environment — the live handshake decides.
    Unknown,
}

impl Support {
    /// Stable code for the C ABI: 1 supported, 0 unsupported, -1 unknown.
    #[must_use]
    pub const fn code(self) -> i32 {
        match self {
            Self::Supported => 1,
            Self::Unsupported => 0,
            Self::Unknown => -1,
        }
    }

    /// Stable name for dumps and messages.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Supported => "supported",
            Self::Unsupported => "unsupported",
            Self::Unknown => "unknown",
        }
    }
}

/// One row of a capability table.
#[derive(Debug, Clone)]
pub struct ActionSupport {
    /// The action being described.
    pub action: LspAction,
    /// Whether it is available.
    pub support: Support,
    /// Why it is or is not available (never empty).
    pub reason: String,
}

/// How a capability table was established.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CapabilityBasis {
    /// Measured against a real server binary (the `initialize` result).
    Measured,
    /// Taken from the server's live `initialize` result in this process.
    Live,
    /// Declared for a backend that has not been measured here.
    Declared,
}

impl CapabilityBasis {
    /// Stable name for dumps and messages.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Measured => "measured",
            Self::Live => "live",
            Self::Declared => "declared",
        }
    }
}

/// The capability table of one backend.
#[derive(Debug, Clone)]
pub struct BackendCapabilities {
    /// Which backend this table describes.
    pub backend: LspBackend,
    /// How the table was established.
    pub basis: CapabilityBasis,
    /// Where the rows come from (binary version + method).
    pub source: String,
    /// One sentence stating what this backend offers that the other does not —
    /// the asymmetry drives the UI gating, so it is part of the table.
    pub note: String,
    /// One row per [`LspAction`], in [`LspAction::ALL`] order.
    pub actions: Vec<ActionSupport>,
}

impl BackendCapabilities {
    /// The row of one action; every action has exactly one row.
    #[must_use]
    pub fn support(&self, action: LspAction) -> Support {
        self.actions
            .iter()
            .find(|row| row.action == action)
            .map_or(Support::Unknown, |row| row.support)
    }

    /// The reason of one action.
    #[must_use]
    pub fn reason(&self, action: LspAction) -> &str {
        self.actions
            .iter()
            .find(|row| row.action == action)
            .map_or("no capability row", |row| row.reason.as_str())
    }

    /// One line per action, for dumps and evidence.
    #[must_use]
    pub fn describe(&self) -> String {
        let mut text = format!(
            "backend={} basis={} source={}\n  note: {}",
            self.backend.as_str(),
            self.basis.as_str(),
            self.source,
            self.note
        );
        for row in &self.actions {
            text.push_str(&format!(
                "\n  {:<18} {:<11} {}",
                row.action.name(),
                row.support.as_str(),
                row.reason
            ));
        }
        text
    }
}

fn row(action: LspAction, support: Support, reason: impl Into<String>) -> ActionSupport {
    ActionSupport {
        action,
        support,
        reason: reason.into(),
    }
}

/// The static capability table of a backend.
///
/// Verible is measured (see the module docs); slang is declared as unknown until
/// a live handshake answers, because no slang-server binary is built here.
#[must_use]
pub fn capabilities(backend: LspBackend) -> BackendCapabilities {
    match backend {
        LspBackend::Verible => verible_capabilities(),
        LspBackend::Slang => slang_capabilities(),
    }
}

fn verible_capabilities() -> BackendCapabilities {
    const SOURCE: &str =
        "verible-verilog-ls v0.0-4298-g085eef05 initialize result (evidence/w1-rust-engine/ls-capabilities.json)";
    // Measured: these are the ten capabilities the server advertises.
    let supported = [
        (
            LspAction::Definition,
            "definitionProvider = true in the initialize result",
        ),
        (
            LspAction::References,
            "referencesProvider = true in the initialize result",
        ),
        (
            LspAction::DocumentSymbol,
            "documentSymbolProvider = true; drives the outline",
        ),
        (
            LspAction::Rename,
            "renameProvider = true in the initialize result",
        ),
        (
            LspAction::DocumentHighlight,
            "documentHighlightProvider = true in the initialize result",
        ),
        (
            LspAction::Formatting,
            "documentFormattingProvider = true; byte-identical to verible-verilog-format",
        ),
        (
            LspAction::RangeFormatting,
            "documentRangeFormattingProvider = true in the initialize result",
        ),
        (
            LspAction::CodeAction,
            "codeActionProvider = true in the initialize result",
        ),
        (
            LspAction::Diagnostics,
            "diagnosticProvider is advertised and diagnostics are pushed for open documents",
        ),
    ];
    let mut actions: Vec<ActionSupport> = supported
        .into_iter()
        .map(|(action, reason)| row(action, Support::Supported, reason))
        .collect();
    actions.push(row(
        LspAction::Hover,
        Support::Unsupported,
        "hoverProvider = false in the initialize result (measured)",
    ));
    actions.push(row(
        LspAction::Completion,
        Support::Unsupported,
        "the initialize result has no completionProvider; FIM is the only completion source",
    ));
    actions.push(row(
        LspAction::SignatureHelp,
        Support::Unsupported,
        "the initialize result has no signatureHelpProvider (measured)",
    ));
    actions.push(row(
        LspAction::InlayHint,
        Support::Unsupported,
        "the initialize result has no inlayHintProvider (measured)",
    ));
    // Keep the ABI order, which the C side indexes by code.
    actions.sort_by_key(|row| row.action.code());
    BackendCapabilities {
        backend: LspBackend::Verible,
        basis: CapabilityBasis::Measured,
        source: SOURCE.to_string(),
        note: "verible formats (document + range) but has no hover, completion, \
               signature help or inlay hints; slang-server is the exact mirror image"
            .to_string(),
        actions,
    }
}

fn slang_capabilities() -> BackendCapabilities {
    const SOURCE: &str =
        "slang-server v0.3.1+2ad996f (hudson-trading/slang-server, source build) initialize result \
         (evidence/w2-spike/04-built-initialize-handshake.txt)";
    let supported = [
        (
            LspAction::Definition,
            "definitionProvider = true in the initialize result",
        ),
        (
            LspAction::References,
            "referencesProvider = true in the initialize result",
        ),
        (
            LspAction::DocumentSymbol,
            "documentSymbolProvider = true; drives the outline",
        ),
        (
            LspAction::Rename,
            "renameProvider = true in the initialize result",
        ),
        (
            LspAction::DocumentHighlight,
            "documentHighlightProvider = true in the initialize result",
        ),
        (
            LspAction::CodeAction,
            "codeActionProvider = true in the initialize result",
        ),
        (
            LspAction::Diagnostics,
            "diagnostics are pushed for open documents (publishDiagnostics observed in the spike)",
        ),
        (
            LspAction::Hover,
            "hoverProvider = true (verible has hoverProvider: false)",
        ),
        (
            LspAction::Completion,
            "completionProvider = true, with the trigger characters (. # : ( [ $ { and `)",
        ),
        (
            LspAction::InlayHint,
            "inlayHintProvider = true (resolver = false)",
        ),
    ];
    let mut actions: Vec<ActionSupport> = supported
        .into_iter()
        .map(|(action, reason)| row(action, Support::Supported, reason))
        .collect();
    actions.push(row(
        LspAction::Formatting,
        Support::Unsupported,
        "the initialize result has no documentFormattingProvider (measured)",
    ));
    actions.push(row(
        LspAction::RangeFormatting,
        Support::Unsupported,
        "the initialize result has no documentRangeFormattingProvider (measured)",
    ));
    actions.push(row(
        LspAction::SignatureHelp,
        Support::Unsupported,
        "the initialize result has no signatureHelpProvider (measured)",
    ));
    // Not in LspAction (yet): callHierarchyProvider / executeCommandProvider /
    // workspaceSymbolProvider / codeLensProvider / documentLinkProvider are
    // advertised too; the spike evidence lists them.
    actions.sort_by_key(|row| row.action.code());
    BackendCapabilities {
        backend: LspBackend::Slang,
        basis: CapabilityBasis::Measured,
        source: SOURCE.to_string(),
        note: "slang hovers, completes, offers inlay hints, callHierarchy and \
               executeCommand, but has NO document/range formatting and no signature \
               help; verible is the exact mirror image (it formats, it never hovers)"
            .to_string(),
        actions,
    }
}

/// A `OneOf<bool, Options>` capability: `false` means the server does NOT offer
/// it (the field is still present in the JSON), which is different from absent.
fn one_of_advertised<T>(value: &Option<OneOf<bool, T>>) -> bool {
    match value {
        Some(OneOf::Left(advertised)) => *advertised,
        Some(OneOf::Right(_)) => true,
        None => false,
    }
}

/// `HoverProviderCapability` is `Simple(bool)` or `Options(..)`.
fn hover_advertised(value: &Option<HoverProviderCapability>) -> bool {
    match value {
        Some(HoverProviderCapability::Simple(advertised)) => *advertised,
        Some(HoverProviderCapability::Options(_)) => true,
        None => false,
    }
}

/// `CodeActionProviderCapability` is `Simple(bool)` or `Options(..)`.
fn code_action_advertised(value: &Option<CodeActionProviderCapability>) -> bool {
    match value {
        Some(CodeActionProviderCapability::Simple(advertised)) => *advertised,
        Some(CodeActionProviderCapability::Options(_)) => true,
        None => false,
    }
}

fn live_row(action: LspAction, advertised: bool, flag: &str) -> ActionSupport {
    if advertised {
        row(
            action,
            Support::Supported,
            format!("the running server advertises {flag} (live initialize result)"),
        )
    } else {
        row(
            action,
            Support::Unsupported,
            format!("the running server advertises no {flag} (live initialize result)"),
        )
    }
}

/// Refine the static table with the capabilities the running server advertised.
///
/// Diagnostics are the one action LSP has no capability flag for: a server pushes
/// `textDocument/publishDiagnostics` for open documents, so that row stays
/// supported with that explanation.
#[must_use]
pub fn capabilities_from_server(
    backend: LspBackend,
    capabilities: &ServerCapabilities,
) -> BackendCapabilities {
    let rows = [
        live_row(
            LspAction::Definition,
            one_of_advertised(&capabilities.definition_provider),
            "definitionProvider",
        ),
        live_row(
            LspAction::References,
            one_of_advertised(&capabilities.references_provider),
            "referencesProvider",
        ),
        live_row(
            LspAction::DocumentSymbol,
            one_of_advertised(&capabilities.document_symbol_provider),
            "documentSymbolProvider",
        ),
        live_row(
            LspAction::Rename,
            one_of_advertised(&capabilities.rename_provider),
            "renameProvider",
        ),
        live_row(
            LspAction::DocumentHighlight,
            one_of_advertised(&capabilities.document_highlight_provider),
            "documentHighlightProvider",
        ),
        live_row(
            LspAction::Formatting,
            one_of_advertised(&capabilities.document_formatting_provider),
            "documentFormattingProvider",
        ),
        live_row(
            LspAction::RangeFormatting,
            one_of_advertised(&capabilities.document_range_formatting_provider),
            "documentRangeFormattingProvider",
        ),
        live_row(
            LspAction::CodeAction,
            code_action_advertised(&capabilities.code_action_provider),
            "codeActionProvider",
        ),
        row(
            LspAction::Diagnostics,
            Support::Supported,
            "the server pushes textDocument/publishDiagnostics for open documents",
        ),
        live_row(
            LspAction::Hover,
            hover_advertised(&capabilities.hover_provider),
            "hoverProvider",
        ),
        live_row(
            LspAction::Completion,
            capabilities.completion_provider.is_some(),
            "completionProvider",
        ),
        live_row(
            LspAction::SignatureHelp,
            capabilities.signature_help_provider.is_some(),
            "signatureHelpProvider",
        ),
        live_row(
            LspAction::InlayHint,
            one_of_advertised(&capabilities.inlay_hint_provider),
            "inlayHintProvider",
        ),
    ];
    let mut actions = rows.to_vec();
    actions.sort_by_key(|row| row.action.code());
    BackendCapabilities {
        backend,
        basis: CapabilityBasis::Live,
        source: format!(
            "live initialize result of {} in this process",
            backend.display_name()
        ),
        note: "rows reflect the running server; a server version that differs from \
               the measured one may move any row"
            .to_string(),
        actions,
    }
}

/// The text of a Verible file list carrying the project's `-I` / `-D` settings.
///
/// `verible-verilog-ls` has no `--include_dir` / `--define` flag (measured:
/// `--helpfull` lists only `--rules_config`, `--rules_config_search` and
/// `--file_list_path`), so the project settings reach it the way Verible's own
/// tooling passes them: a `.f` file with `+incdir+` / `+define+` entries, handed
/// over with `--file_list_path`. Verible ignores blank lines and `//` comments.
#[must_use]
pub fn filelist_text(
    files: &[std::path::PathBuf],
    include_dirs: &[std::path::PathBuf],
    defines: &[String],
) -> String {
    let mut text = String::from("// generated by VEdit: project include dirs, defines and files\n");
    for dir in include_dirs {
        text.push_str("+incdir+");
        text.push_str(&dir.to_string_lossy());
        text.push('\n');
    }
    for define in defines {
        let define = define.trim();
        if define.is_empty() {
            continue;
        }
        text.push_str("+define+");
        text.push_str(define);
        text.push('\n');
    }
    for file in files {
        text.push_str(&file.to_string_lossy());
        text.push('\n');
    }
    text
}

#[cfg(test)]
mod tests {
    use super::{
        capabilities, capabilities_from_server, filelist_text, CapabilityBasis, LspAction,
        LspBackend, Support,
    };
    use lsp_types::ServerCapabilities;
    use std::path::PathBuf;

    #[test]
    fn backend_codes_round_trip_and_unknown_codes_are_rejected() {
        assert_eq!(LspBackend::Verible.code(), 0);
        assert_eq!(LspBackend::Slang.code(), 1);
        assert_eq!(LspBackend::from_code(0), Some(LspBackend::Verible));
        assert_eq!(LspBackend::from_code(1), Some(LspBackend::Slang));
        assert_eq!(LspBackend::from_code(2), None);
        assert_eq!(LspBackend::parse("SLANG-SERVER"), Some(LspBackend::Slang));
        assert_eq!(LspBackend::parse("nonsense"), None);
    }

    #[test]
    fn every_action_has_exactly_one_row_in_abi_order() {
        for backend in [LspBackend::Verible, LspBackend::Slang] {
            let table = capabilities(backend);
            assert_eq!(table.actions.len(), LspAction::ALL.len());
            for (index, row) in table.actions.iter().enumerate() {
                assert_eq!(row.action.code(), index as i32);
                assert!(!row.reason.is_empty(), "every row carries a reason");
            }
            for action in LspAction::ALL {
                assert_eq!(action.code(), action.code());
                assert_eq!(LspAction::from_code(action.code()), Some(action));
            }
        }
    }

    #[test]
    fn verible_rows_match_the_measured_initialize_result() {
        let table = capabilities(LspBackend::Verible);
        for action in [
            LspAction::Definition,
            LspAction::References,
            LspAction::DocumentSymbol,
            LspAction::Rename,
            LspAction::DocumentHighlight,
            LspAction::Formatting,
            LspAction::RangeFormatting,
            LspAction::CodeAction,
            LspAction::Diagnostics,
        ] {
            assert_eq!(table.support(action), Support::Supported, "{action:?}");
        }
        for action in [
            LspAction::Hover,
            LspAction::Completion,
            LspAction::SignatureHelp,
            LspAction::InlayHint,
        ] {
            assert_eq!(table.support(action), Support::Unsupported, "{action:?}");
        }
    }

    #[test]
    fn slang_rows_are_measured_and_differ_from_verible() {
        let slang = capabilities(LspBackend::Slang);
        let verible = capabilities(LspBackend::Verible);
        for action in [
            LspAction::Definition,
            LspAction::References,
            LspAction::DocumentSymbol,
            LspAction::Rename,
            LspAction::DocumentHighlight,
            LspAction::CodeAction,
            LspAction::Diagnostics,
            LspAction::Hover,
            LspAction::Completion,
            LspAction::InlayHint,
        ] {
            assert_eq!(slang.support(action), Support::Supported, "{action:?}");
        }
        for action in [
            LspAction::Formatting,
            LspAction::RangeFormatting,
            LspAction::SignatureHelp,
        ] {
            assert_eq!(slang.support(action), Support::Unsupported, "{action:?}");
        }
        // The two backends are genuinely different: verible formats, slang hovers.
        assert_eq!(verible.support(LspAction::Formatting), Support::Supported);
        assert_eq!(slang.support(LspAction::Formatting), Support::Unsupported);
        assert_eq!(verible.support(LspAction::Hover), Support::Unsupported);
        assert_eq!(slang.support(LspAction::Hover), Support::Supported);
        assert_eq!(slang.basis, CapabilityBasis::Measured);
        assert!(
            slang
                .note
                .to_lowercase()
                .contains("no document/range formatting"),
            "{}",
            slang.note
        );
        assert!(verible.note.contains("no hover"), "{}", verible.note);
    }

    #[test]
    fn a_live_handshake_replaces_the_declared_rows() {
        let table = capabilities_from_server(LspBackend::Slang, &ServerCapabilities::default());
        assert_eq!(table.basis.as_str(), "live");
        assert_eq!(table.support(LspAction::Definition), Support::Unsupported);
        assert_eq!(table.support(LspAction::Diagnostics), Support::Supported);
        assert!(table
            .reason(LspAction::Definition)
            .contains("definitionProvider"));
    }

    #[test]
    fn filelist_carries_incdir_define_and_files_in_order() {
        let text = filelist_text(
            &[PathBuf::from("/p/top.sv"), PathBuf::from("/p/mid.sv")],
            &[PathBuf::from("/p/inc")],
            &["WIDTH=8".to_string(), "SIM".to_string()],
        );
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines[1], "+incdir+/p/inc");
        assert_eq!(lines[2], "+define+WIDTH=8");
        assert_eq!(lines[3], "+define+SIM");
        assert_eq!(lines[4], "/p/top.sv");
        assert_eq!(lines[5], "/p/mid.sv");
    }
}
