#pragma once

// `verible-verilog-format` options (UX batch 2 / A4, plan §1 F3).
//
// One table drives all three views of the same knobs — the Preferences widgets,
// the store keys and the argv the formatter receives — so a flag can never be
// wired in one place and forgotten in another:
//
//   * [`kFormatKnobs`] — the knob table: store key, verible flag, widget kind,
//     whether it belongs to the collapsible "advanced" section, and the
//     translated label/tooltip source text;
//   * `formatValuesFromEntries` / `writeFormatEntries` — the store view;
//   * `formatArguments` — the argv view, asserted one-to-one in
//     `settings_selftest.cpp`.
//
// # "Default = no flag"
//
// Every knob is OPTIONAL. An absent store key means "verible's own default":
// `formatArguments` then emits nothing for it, so an untouched page produces
// exactly the argv the product produced before this page existed (just `-`).
// The defaults documented next to each knob are verible's own, measured with
// `verible-verilog-format --helpfull` (v0.0-4298-g085eef05).
//
// The argv DELIVERY is `src/workspace/diagnostics/toolchain.cpp`
// (`formatVerilog`): it appends this list in front of the final `-`. The list is
// built here, in the settings lane, because the flag names and the value sets
// are part of the settings contract — see `toolchain.cpp` for the one call.
//
// Everything this header needs at the transport boundary is `inline`: the
// diagnostics lane consumes `formatArguments` / `currentFormatValues` without a
// link edge to the settings library.
#include "settings_keys.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>

namespace vedit::workspace::settings {

struct Entry;

// The widget a knob gets on the Format page (the store value is always a
// string; the kind only decides the editor and the validation).
enum class FormatKnobKind {
    Integer,        // 0 = "Default (verible)"; stored "" when unset
    Boolean,        // Default / true / false, stored "" / "true" / "false"
    LineTerminator, // Default / CRLF / LF / auto
    Alignment,      // Default / align / flush-left / preserve / infer
    Indentation,    // Default / indent / wrap
};

struct FormatKnobSpec {
    const char *key;    // store key, e.g. "format.column_limit"
    const char *flag;   // verible flag name without "--"
    FormatKnobKind kind;
    bool advanced;      // lives in the collapsible "Advanced" section
    const char *label;  // English source text (translate with tr())
    const char *hint;   // English tooltip source text: flag + verible default
};

// The knob table. Order is the page order and the argv order.
inline constexpr FormatKnobSpec kFormatKnobs[] = {
    // --- the four knobs the plan names as the main ones ---------------------
    {keys::kFormatIndentationSpaces, "indentation_spaces", FormatKnobKind::Integer, false,
     "Indentation spaces:",
     "--indentation_spaces: spaces per indentation level (verible default 2)."},
    {keys::kFormatColumnLimit, "column_limit", FormatKnobKind::Integer, false,
     "Column limit:",
     "--column_limit: target line length (verible default 100)."},
    {keys::kFormatWrapSpaces, "wrap_spaces", FormatKnobKind::Integer, false,
     "Wrap spaces:",
     "--wrap_spaces: spaces per wrap level (verible default 4)."},
    {keys::kFormatLineTerminator, "line_terminator", FormatKnobKind::LineTerminator, false,
     "Line terminator:",
     "--line_terminator: CRLF, LF or auto (verible default auto)."},

    // --- advanced: three-state switches -------------------------------------
    {keys::kFormatTryWrapLongLines, "try_wrap_long_lines", FormatKnobKind::Boolean, true,
     "Try wrap long lines:",
     "--try_wrap_long_lines (verible default false)."},
    {keys::kFormatCompactIndexingAndSelections, "compact_indexing_and_selections",
     FormatKnobKind::Boolean, true,
     "Compact indexing and selections:",
     "--compact_indexing_and_selections (verible default true)."},

    // --- advanced: the alignment enums ({align,flush-left,preserve,infer}) --
    {keys::kFormatAssignmentStatementAlignment, "assignment_statement_alignment",
     FormatKnobKind::Alignment, true, "Assignment statements:",
     "--assignment_statement_alignment (verible default infer)."},
    {keys::kFormatCaseItemsAlignment, "case_items_alignment", FormatKnobKind::Alignment, true,
     "Case items:", "--case_items_alignment (verible default infer)."},
    {keys::kFormatClassMemberVariableAlignment, "class_member_variable_alignment",
     FormatKnobKind::Alignment, true, "Class member variables:",
     "--class_member_variable_alignment (verible default infer)."},
    {keys::kFormatDistributionItemsAlignment, "distribution_items_alignment",
     FormatKnobKind::Alignment, true, "Distribution items:",
     "--distribution_items_alignment (verible default infer)."},
    {keys::kFormatEnumAssignmentStatementAlignment, "enum_assignment_statement_alignment",
     FormatKnobKind::Alignment, true, "Enum assignments:",
     "--enum_assignment_statement_alignment (verible default infer)."},
    {keys::kFormatFormalParametersAlignment, "formal_parameters_alignment",
     FormatKnobKind::Alignment, true, "Formal parameters:",
     "--formal_parameters_alignment (verible default infer)."},
    {keys::kFormatModuleNetVariableAlignment, "module_net_variable_alignment",
     FormatKnobKind::Alignment, true, "Module net/variable declarations:",
     "--module_net_variable_alignment (verible default infer)."},
    {keys::kFormatNamedParameterAlignment, "named_parameter_alignment",
     FormatKnobKind::Alignment, true, "Named parameters:",
     "--named_parameter_alignment (verible default infer)."},
    {keys::kFormatNamedPortAlignment, "named_port_alignment", FormatKnobKind::Alignment, true,
     "Named ports:", "--named_port_alignment (verible default infer)."},
    {keys::kFormatPortDeclarationsAlignment, "port_declarations_alignment",
     FormatKnobKind::Alignment, true, "Port declarations:",
     "--port_declarations_alignment (verible default infer)."},
    {keys::kFormatStructUnionMembersAlignment, "struct_union_members_alignment",
     FormatKnobKind::Alignment, true, "Struct/union members:",
     "--struct_union_members_alignment (verible default infer)."},

    // --- advanced: the {indent,wrap} knobs ----------------------------------
    {keys::kFormatFormalParametersIndentation, "formal_parameters_indentation",
     FormatKnobKind::Indentation, true, "Formal parameter indentation:",
     "--formal_parameters_indentation (verible default wrap)."},
    {keys::kFormatNamedParameterIndentation, "named_parameter_indentation",
     FormatKnobKind::Indentation, true, "Named parameter indentation:",
     "--named_parameter_indentation (verible default wrap)."},
    {keys::kFormatNamedPortIndentation, "named_port_indentation",
     FormatKnobKind::Indentation, true, "Named port indentation:",
     "--named_port_indentation (verible default wrap)."},
    {keys::kFormatPortDeclarationsIndentation, "port_declarations_indentation",
     FormatKnobKind::Indentation, true, "Port declaration indentation:",
     "--port_declarations_indentation (verible default wrap)."},

    // --- advanced: the two right-align switches -----------------------------
    {keys::kFormatPortDeclarationsRightAlignPackedDimensions,
     "port_declarations_right_align_packed_dimensions", FormatKnobKind::Boolean, true,
     "Right-align packed dimensions:",
     "--port_declarations_right_align_packed_dimensions (verible default false)."},
    {keys::kFormatPortDeclarationsRightAlignUnpackedDimensions,
     "port_declarations_right_align_unpacked_dimensions", FormatKnobKind::Boolean, true,
     "Right-align unpacked dimensions:",
     "--port_declarations_right_align_unpacked_dimensions (verible default false)."},
};

inline constexpr std::size_t kFormatKnobCount =
    sizeof(kFormatKnobs) / sizeof(kFormatKnobs[0]);

// Store key -> stored value (empty string = absent = verible's default).
using FormatValues = QHash<QString, QString>;

// The store view: reads every knob key (absent keys become empty strings) and
// writes every knob key back, an empty value REMOVING the key so "default"
// leaves no residue in settings.toml.
FormatValues formatValuesFromEntries(const QVector<Entry> &entries);
void writeFormatEntries(QVector<Entry> *entries, const FormatValues &values);
// Every knob absent — the state "Restore defaults" produces.
FormatValues defaultFormatValues();
bool formatValuesAreDefault(const FormatValues &values);

// The argv for `verible-verilog-format`, WITHOUT the trailing `-` (the caller
// appends the stdin marker). Deterministic order (the table order), and empty
// entries are skipped so "default" really means "no flag".
inline QStringList formatArguments(const FormatValues &values)
{
    QStringList arguments;
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        const QString value = values.value(QString::fromLatin1(knob.key)).trimmed();
        if (value.isEmpty()) {
            continue;
        }
        arguments.append(QStringLiteral("--%1=%2")
                             .arg(QString::fromLatin1(knob.flag), value));
    }
    return arguments;
}

// The process-wide configuration `formatVerilog` reads. `main.cpp` seeds it from
// the store at startup and the Preferences page updates it on every change, so
// the formatter always sees the values the page shows (the diagnostics lane
// cannot read the settings store itself: it links no settings code).
inline FormatValues &currentFormatValues()
{
    static FormatValues values;
    return values;
}

inline void setCurrentFormatValues(const FormatValues &values)
{
    currentFormatValues() = values;
}

} // namespace vedit::workspace::settings
