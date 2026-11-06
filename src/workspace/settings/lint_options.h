#pragma once

// The Preferences "Lint" page contract (UX batch 2 / A4, plan §0.1 R1 — a
// BINDING user ruling).
//
// R1 in one paragraph: Preferences has ONE Lint page (the knobs are presented
// together), but the store keeps TWO independent configurations —
// `lint.verible.*` and `lint.slang.*`. The existing "Lint engine" selector
// decides which set the page shows; switching the engine must never lose the
// other set. A knob the current engine does not have is DISABLED with the reason
// in its tooltip, and nothing invalid is ever sent to a backend.
//
// # Two different "not available" cases, kept apart
//
// The page must not blur them, so this module distinguishes them explicitly:
//
//   * [`LintKnobStateKind::BackendUnsupported`] — the notion does not exist in
//     this engine at all (slang has no ruleset, no `.rules.verible_lint`, no
//     waivers, no autofix; verible-verilog-lint has no `-W`/`--error-limit`).
//     The control is GREYED OUT and the tooltip says why. The engine's no-op
//     list comes from the C ABI itself ([`lintUnsupportedKnobGroups`] wraps
//     `vd_lint_unsupported_knobs`), so the page cannot drift from the Rust side.
//   * [`LintKnobStateKind::TransportPending`] — the engine does support it, but
//     the lint job's C ABI (`VdLintBatchOptions`) has no field for it. The
//     control stays USABLE and the value is persisted (so nothing the user typed
//     is lost), but the reason is reported: the value is stored, not applied.
//     ONLY `lint.verible.autofix` is in this state: `VdLintBatchOptions` carries
//     `ruleset` / `rules` / `waiver_files` / `slang_suppressions` /
//     `error_limit` but has no autofix field (ux-batch-2 U6 finding; the real
//     autofix path is the hover one-click fix, not the batch pass).
//
// `lint.verible.rules_config` is carried by `VdLintBatchOptions.rules_path`;
// the other five wired knobs reach the same struct through
// [`buildLintJobOptions`] + [`fillLintBatchOptions`].
//
// The mechanism is deliberately one seam: the diagnostics lane (which builds the
// C ABI options) calls `buildLintJobOptions` and then `fillLintBatchOptions`,
// and nothing else, so wiring a new parameter is a single edit here — never a
// second, parallel path. Everything is header-only (the diagnostics lane links
// no settings library) and the C ABI part is compiled only when `vd_ffi.h` is
// on the include path.
#include "settings_keys.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#if defined(__has_include)
#if __has_include(<vd_ffi.h>)
#include <vd_ffi.h>
#define VEDIT_LINT_OPTIONS_HAVE_ABI 1
#endif
#endif

namespace vedit::workspace::settings {

struct Entry;

// The lint engine, as stored under `tools.lint_engine`.
enum class LintBackend {
    Verible, // "verible" — verible-verilog-lint
    Slang,   // "slang"    — slang --lint-only (the default, see vd-settings)
};

// One knob of the Lint page.
enum class LintKnob {
    Ruleset,
    Rules,
    RulesConfig,
    WaiverFiles,
    Autofix,
    SlangWarnings,
    SlangErrorLimit,
};

enum class LintKnobStateKind {
    Supported,
    BackendUnsupported,
    TransportPending,
};

struct LintKnobState {
    LintKnobStateKind kind = LintKnobStateKind::Supported;
    // Already translated (the caller shows it in a tooltip as-is).
    QString reason;

    bool enabled() const { return kind != LintKnobStateKind::BackendUnsupported; }
    bool pending() const { return kind == LintKnobStateKind::TransportPending; }
};

// "verible" (any case) selects verible; everything else is slang, which is the
// same fallback direction vd-settings uses, so the two can never disagree.
LintBackend lintBackendFromSetting(const QString &value);
// The knob's command-line name ("--ruleset", "-W", ...) for tooltips and hints.
QString lintKnobFlagName(LintKnob knob);
QString lintBackendSetting(LintBackend backend); // "verible" / "slang"

// The state of one knob for one engine (see the two cases in the header comment).
LintKnobState lintKnobState(LintBackend backend, LintKnob knob);

// The value struct of the page. Both halves are always present and always
// persisted, which is exactly what makes "switching the engine keeps the other
// set" true (R1).
struct LintVeribleValues {
    QString ruleset;     // "" = no flag (verible's own default)
    QString rules;       // comma-separated rule list, stored verbatim
    QString rulesConfig; // path to a .rules.verible_lint file
    QString waiverFiles; // one path per line
    QString autofix;     // "" = no flag
};

struct LintSlangValues {
    QString warnings;   // `-W` values, one per line
    QString errorLimit; // `--error-limit`, "" = no flag
};

struct LintValues {
    LintVeribleValues verible;
    LintSlangValues slang;
};

// The store view. Reads and writes BOTH halves, always: writing one half leaves
// the other half's keys untouched, and an empty value REMOVES its key.
LintValues lintValuesFromEntries(const QVector<Entry> &entries);
void writeLintEntries(QVector<Entry> *entries, const LintValues &values);
// Every knob empty — the state "Restore defaults" produces.
LintValues defaultLintValues();

// The C ABI group name of one knob (`vd_lint_unsupported_knobs` spells them
// "ruleset", "rules", "waiver_files", "slang_suppressions", "error_limit").
inline QString lintKnobAbiGroup(LintKnob knob)
{
    switch (knob) {
    case LintKnob::Ruleset:
        return QStringLiteral("ruleset");
    case LintKnob::Rules:
        return QStringLiteral("rules");
    case LintKnob::RulesConfig:
        return QStringLiteral("rules_config");
    case LintKnob::WaiverFiles:
        return QStringLiteral("waiver_files");
    case LintKnob::Autofix:
        return QStringLiteral("autofix");
    case LintKnob::SlangWarnings:
        return QStringLiteral("slang_suppressions");
    case LintKnob::SlangErrorLimit:
        return QStringLiteral("error_limit");
    }
    return QString();
}

// The documented no-ops of the selected engine, read from the C ABI itself
// (`vd_lint_unsupported_knobs`) so the page and the Rust argv builder cannot
// drift. A build without `vd_ffi.h` falls back to the same list.
inline QStringList lintUnsupportedKnobGroups(LintBackend backend)
{
#if defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
    const int engine = backend == LintBackend::Verible ? 1 : 0;
    QStringList groups;
    char *raw = vd_lint_unsupported_knobs(engine);
    if (raw != nullptr) {
        const QString text = QString::fromUtf8(raw);
        vd_string_free(raw);
        for (const QString &group : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString trimmed = group.trimmed();
            if (!trimmed.isEmpty()) {
                groups.append(trimmed);
            }
        }
    }
    return groups;
#else
    return backend == LintBackend::Verible
        ? QStringList{QStringLiteral("slang_suppressions"), QStringLiteral("error_limit"),
                      QStringLiteral("include_dirs"), QStringLiteral("defines")}
        : QStringList{QStringLiteral("ruleset"), QStringLiteral("rules"),
                      QStringLiteral("waiver_files")};
#endif
}

// True when the selected engine has no such knob: either the ABI lists its group
// as a documented no-op, or it is one of the two verible-only TOOL options the
// ABI does not model (`rules_config`, `autofix`) - vd-deeplint's verible arm
// emits those, the slang arm never does.
inline bool lintKnobBackendUnsupported(LintBackend backend, LintKnob knob)
{
    if (lintUnsupportedKnobGroups(backend).contains(lintKnobAbiGroup(knob))) {
        return true;
    }
    return (knob == LintKnob::RulesConfig || knob == LintKnob::Autofix)
        && backend != LintBackend::Verible;
}

// What the lint job should carry, composed from the page.
//
// Precedence for the rules file:
//   1. the page's `lint.verible.rules_config` (the dedicated knob),
//   2. the legacy `tools.verible_rules` path (the Tools page field, kept so an
//      existing settings.toml keeps working),
//   3. the project's DISCOVERED `.rules.verible_lint`
//      (`VdProjectInfo.rules_path`, published by the diagnostics lane through
//      [`currentDiscoveredRulesPath`]).
// A missing path stays empty, i.e. the lint runs with no `--rules_config=` flag.
struct LintJobOptions {
    LintBackend backend = LintBackend::Slang;
    QString rulesPath;
    // Wired through VdLintBatchOptions (ux-batch 2 / U6):
    quint8 ruleset = 0;          // 0 default -> NO --ruleset flag; 1 all; 2 none
    QString rules;               // --rules= (verbatim, trimmed); empty = no flag
    QStringList waiverFiles;     // one path per entry; empty = no flag
    QStringList slangWarnings;   // each a "-W..." token; empty = no flag
    quint32 slangErrorLimit = 0; // 0 = NO --error-limit flag
    // `lint.verible.autofix` is deliberately absent: VdLintBatchOptions has no
    // autofix field, so the value is never composed (and the knob stays
    // TransportPending) - R1 forbids sending an unsupported knob as a no-op.
};

// `--ruleset` code: "all" -> 1, "none" -> 2, anything else ("" / "default" /
// an unknown stored value) -> 0 (no flag). The ABI rejects any other code.
inline quint8 lintRulesetCode(const QString &stored)
{
    const QString value = stored.trimmed().toLower();
    if (value == QLatin1String("all")) {
        return 1;
    }
    if (value == QLatin1String("none")) {
        return 2;
    }
    return 0;
}

// One item per line, blank lines dropped (the store's list convention).
inline QStringList lintLineList(const QString &text)
{
    QStringList items;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            items.append(trimmed);
        }
    }
    return items;
}

// The page stores the bare -W values ("everything", "no-unused-variable"); the
// ABI wants complete tokens, each its own argv entry. A value typed with the
// "-W" prefix is accepted as-is.
inline QStringList lintSlangWarningTokens(const QString &text)
{
    QStringList tokens;
    for (const QString &value : lintLineList(text)) {
        tokens.append(value.startsWith(QLatin1String("-W")) ? value
                                                            : QStringLiteral("-W") + value);
    }
    return tokens;
}

// `--error-limit`: "" / 0 / an unparsable stored value -> 0 (no flag).
inline quint32 lintErrorLimitValue(const QString &text)
{
    bool ok = false;
    const uint value = text.trimmed().toUInt(&ok);
    return ok ? value : 0;
}

inline LintJobOptions buildLintJobOptions(const LintValues &values, LintBackend backend,
                                          const QString &legacyToolsRulesPath,
                                          const QString &discoveredRulesPath)
{
    LintJobOptions options;
    options.backend = backend;
    if (backend != LintBackend::Verible) {
        // slang: only its two knobs. A rules file / ruleset / rules / waiver is
        // an argument the engine never reads - exactly the "invalid parameter"
        // R1 forbids - so those fields stay empty.
        if (!lintKnobBackendUnsupported(backend, LintKnob::SlangWarnings)) {
            options.slangWarnings = lintSlangWarningTokens(values.slang.warnings);
        }
        if (!lintKnobBackendUnsupported(backend, LintKnob::SlangErrorLimit)) {
            options.slangErrorLimit = lintErrorLimitValue(values.slang.errorLimit);
        }
        return options;
    }
    const QStringList candidates{
        values.verible.rulesConfig,
        legacyToolsRulesPath,
        discoveredRulesPath,
    };
    for (const QString &candidate : candidates) {
        const QString trimmed = candidate.trimmed();
        if (!trimmed.isEmpty()) {
            options.rulesPath = trimmed;
            break;
        }
    }
    if (!lintKnobBackendUnsupported(backend, LintKnob::Ruleset)) {
        options.ruleset = lintRulesetCode(values.verible.ruleset);
    }
    if (!lintKnobBackendUnsupported(backend, LintKnob::Rules)) {
        options.rules = values.verible.rules.trimmed();
    }
    if (!lintKnobBackendUnsupported(backend, LintKnob::WaiverFiles)) {
        options.waiverFiles = lintLineList(values.verible.waiverFiles);
    }
    return options;
}

#if defined(VEDIT_LINT_OPTIONS_HAVE_ABI)
// Owned storage for the strings `VdLintBatchOptions` borrows. It must outlive
// the call that consumes the options (the caller keeps it on the stack next to
// the VdLintBatchOptions it fills).
struct LintBatchAbiStorage {
    QByteArray rulesPath;
    QByteArray rules;
    QByteArray waiverFiles;
    QByteArray slangSuppressions;
};

// Fill the lint job's C ABI struct from the composed options - the ONE mapping
// between the Preferences values and `VdLintBatchOptions`. Fields another caller
// owns (binaries, include dirs, defines, timeouts) are left untouched.
inline void fillLintBatchOptions(const LintJobOptions &options, VdLintBatchOptions *abi,
                                 LintBatchAbiStorage *storage)
{
    if (abi == nullptr || storage == nullptr) {
        return;
    }
    abi->engine = options.backend == LintBackend::Verible ? 1 : 0;
    abi->ruleset = options.ruleset;
    storage->rulesPath = options.rulesPath.toUtf8();
    abi->rules_path = storage->rulesPath.isEmpty() ? nullptr : storage->rulesPath.constData();
    storage->rules = options.rules.toUtf8();
    abi->rules = storage->rules.isEmpty() ? nullptr : storage->rules.constData();
    storage->waiverFiles = options.waiverFiles.join(QLatin1Char(',')).toUtf8();
    abi->waiver_files =
        storage->waiverFiles.isEmpty() ? nullptr : storage->waiverFiles.constData();
    storage->slangSuppressions = options.slangWarnings.join(QLatin1Char(' ')).toUtf8();
    abi->slang_suppressions =
        storage->slangSuppressions.isEmpty() ? nullptr : storage->slangSuppressions.constData();
    abi->error_limit = options.slangErrorLimit;
}
#endif

// The process-wide lint configuration the diagnostics lane reads (it links no
// settings code). `main.cpp` seeds it at startup, the Preferences page updates
// it on every change.
inline LintValues &currentLintValues()
{
    static LintValues values;
    return values;
}

inline void setCurrentLintValues(const LintValues &values)
{
    currentLintValues() = values;
}

// The `.rules.verible_lint` the project loader DISCOVERED for the current
// project (`VdProjectInfo.rules_path`), published by the diagnostics lane when
// the project file set arrives and cleared on a root change. It is the THIRD
// rules-precedence level only: a hand-typed `lint.verible.rules_config` and the
// legacy `tools.verible_rules` path win over it.
inline QString &currentDiscoveredRulesPath()
{
    static QString path;
    return path;
}

inline void setCurrentDiscoveredRulesPath(const QString &path)
{
    currentDiscoveredRulesPath() = path;
}

} // namespace vedit::workspace::settings
