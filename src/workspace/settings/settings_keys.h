#pragma once

// Store keys of the Preferences dialog (item 10.2, settings pages).
//
// The store is a flat TOML table of strings (`crates/vd-settings`); this header
// is the Qt side's half of the key contract. Its other half is
// `vd_settings::schema::keys` in `crates/vd-settings/src/schema.rs`, which is the
// reference: the two lists spell every key identically, and that Rust module
// also documents the types and defaults.
//
// List-valued keys (`tools.include_dirs`, `tools.defines`) hold ONE ITEM PER
// LINE. Blank lines are ignored on both sides, so a file edited with CRLF still
// reads back the same list.
//
// The FIM API key is deliberately absent: it is never a store value. It comes
// from `VEDIT_FIM_API_KEY` first, then from the user-level key file beside
// `settings.toml`, which must be readable by the owner only (0600).
namespace vedit::workspace::settings::keys {

// Tools page.
inline constexpr char kLspBackend[] = "tools.lsp_backend";
inline constexpr char kLintEngine[] = "tools.lint_engine";
inline constexpr char kVeribleLs[] = "tools.verible_ls";
inline constexpr char kSlang[] = "tools.slang";
inline constexpr char kSlangServer[] = "tools.slang_server";
inline constexpr char kVeribleFormat[] = "tools.verible_format";
inline constexpr char kVeribleRules[] = "tools.verible_rules";
inline constexpr char kIncludeDirs[] = "tools.include_dirs";
inline constexpr char kDefines[] = "tools.defines";

// FIM page.
inline constexpr char kFimEnabled[] = "fim.enabled";
inline constexpr char kFimProtocol[] = "fim.protocol";
inline constexpr char kFimEndpoint[] = "fim.endpoint";
inline constexpr char kFimModel[] = "fim.model";
inline constexpr char kFimTimeoutMs[] = "fim.timeout_ms";
inline constexpr char kFimMaxTokens[] = "fim.max_tokens";
inline constexpr char kFimGhostText[] = "fim.ghost_text";

// Secret material: an environment variable and an optional owner-only file.
inline constexpr char kFimApiKeyEnvironment[] = "VEDIT_FIM_API_KEY";
inline constexpr char kFimApiKeyFile[] = "fim_api_key";

// Stored values of the two selectors (the display text is the binary name).
inline constexpr char kLspBackendVerible[] = "verible";
inline constexpr char kLspBackendSlang[] = "slang";
inline constexpr char kLintEngineSlang[] = "slang";
inline constexpr char kLintEngineVerible[] = "verible";
inline constexpr char kFimProtocolOpenAi[] = "open_ai";
inline constexpr char kFimProtocolLlamaCpp[] = "llama_cpp";

// --- UX batch 2 / A4: appearance -------------------------------------------
// `ui.font_point_size` is an integer (empty = follow the system font);
// `ui.scale_percent` is one of 100 / 125 / 150 / 200 and takes effect on the
// NEXT start (main.cpp puts QT_SCALE_FACTOR into the environment before the
// QApplication exists — see `ui_scale.h`).
inline constexpr char kUiFontPointSize[] = "ui.font_point_size";
inline constexpr char kUiScalePercent[] = "ui.scale_percent";

// --- UX batch 2 / A4: format (verible-verilog-format, plan §1 F3) ----------
// One key per formatter flag; an ABSENT key means "verible's own default", and
// the argv then carries no flag at all (`format_options.h`).
inline constexpr char kFormatIndentationSpaces[] = "format.indentation_spaces";
inline constexpr char kFormatColumnLimit[] = "format.column_limit";
inline constexpr char kFormatWrapSpaces[] = "format.wrap_spaces";
inline constexpr char kFormatLineTerminator[] = "format.line_terminator";
inline constexpr char kFormatTryWrapLongLines[] = "format.try_wrap_long_lines";
inline constexpr char kFormatCompactIndexingAndSelections[] =
    "format.compact_indexing_and_selections";
inline constexpr char kFormatAssignmentStatementAlignment[] =
    "format.assignment_statement_alignment";
inline constexpr char kFormatCaseItemsAlignment[] = "format.case_items_alignment";
inline constexpr char kFormatClassMemberVariableAlignment[] =
    "format.class_member_variable_alignment";
inline constexpr char kFormatDistributionItemsAlignment[] = "format.distribution_items_alignment";
inline constexpr char kFormatEnumAssignmentStatementAlignment[] =
    "format.enum_assignment_statement_alignment";
inline constexpr char kFormatFormalParametersAlignment[] = "format.formal_parameters_alignment";
inline constexpr char kFormatFormalParametersIndentation[] =
    "format.formal_parameters_indentation";
inline constexpr char kFormatModuleNetVariableAlignment[] = "format.module_net_variable_alignment";
inline constexpr char kFormatNamedParameterAlignment[] = "format.named_parameter_alignment";
inline constexpr char kFormatNamedParameterIndentation[] = "format.named_parameter_indentation";
inline constexpr char kFormatNamedPortAlignment[] = "format.named_port_alignment";
inline constexpr char kFormatNamedPortIndentation[] = "format.named_port_indentation";
inline constexpr char kFormatPortDeclarationsAlignment[] = "format.port_declarations_alignment";
inline constexpr char kFormatPortDeclarationsIndentation[] =
    "format.port_declarations_indentation";
inline constexpr char kFormatPortDeclarationsRightAlignPackedDimensions[] =
    "format.port_declarations_right_align_packed_dimensions";
inline constexpr char kFormatPortDeclarationsRightAlignUnpackedDimensions[] =
    "format.port_declarations_right_align_unpacked_dimensions";
inline constexpr char kFormatStructUnionMembersAlignment[] =
    "format.struct_union_members_alignment";

// --- UX batch 2 / A4 / §0.1 R1: TWO independent lint configurations -------
// The Lint page shows one set of controls, but the store keeps two key sets:
// switching `tools.lint_engine` must never overwrite the other engine's values.
inline constexpr char kLintVeribleRuleset[] = "lint.verible.ruleset";
inline constexpr char kLintVeribleRules[] = "lint.verible.rules";
inline constexpr char kLintVeribleRulesConfig[] = "lint.verible.rules_config";
inline constexpr char kLintVeribleWaiverFiles[] = "lint.verible.waiver_files";
inline constexpr char kLintVeribleAutofix[] = "lint.verible.autofix";
inline constexpr char kLintSlangWarnings[] = "lint.slang.warnings";
inline constexpr char kLintSlangErrorLimit[] = "lint.slang.error_limit";

// Stored values (verible's own spellings, measured with --helpfull).
inline constexpr char kRulesetDefault[] = "default";
inline constexpr char kRulesetAll[] = "all";
inline constexpr char kRulesetNone[] = "none";
inline constexpr char kAutofixNo[] = "no";
inline constexpr char kAutofixPatch[] = "patch";
inline constexpr char kAutofixPatchInteractive[] = "patch-interactive";
inline constexpr char kAutofixInplace[] = "inplace";
inline constexpr char kAutofixInplaceInteractive[] = "inplace-interactive";
inline constexpr char kAutofixGenerateWaiver[] = "generate-waiver";

} // namespace vedit::workspace::settings::keys
