#include "lint_options.h"

#include "settings_client.h"

#include <QCoreApplication>

namespace vedit::workspace::settings {

namespace {

QString translate(const char *text)
{
    return QCoreApplication::translate("VEdit", text);
}

} // namespace

QString lintKnobFlagName(LintKnob knob)
{
    switch (knob) {
    case LintKnob::Ruleset:
        return QStringLiteral("--ruleset");
    case LintKnob::Rules:
        return QStringLiteral("--rules");
    case LintKnob::RulesConfig:
        return QStringLiteral("--rules_config");
    case LintKnob::WaiverFiles:
        return QStringLiteral("--waiver_files");
    case LintKnob::Autofix:
        return QStringLiteral("--autofix");
    case LintKnob::SlangWarnings:
        return QStringLiteral("-W");
    case LintKnob::SlangErrorLimit:
        return QStringLiteral("--error-limit");
    }
    return QString();
}

namespace {
bool isVeribleKnob(LintKnob knob)
{
    return knob != LintKnob::SlangWarnings && knob != LintKnob::SlangErrorLimit;
}

} // namespace

LintBackend lintBackendFromSetting(const QString &value)
{
    return value.trimmed().compare(QStringLiteral("verible"), Qt::CaseInsensitive) == 0
        ? LintBackend::Verible
        : LintBackend::Slang;
}

QString lintBackendSetting(LintBackend backend)
{
    return backend == LintBackend::Verible ? QString::fromLatin1(keys::kLintEngineVerible)
                                           : QString::fromLatin1(keys::kLintEngineSlang);
}

LintKnobState lintKnobState(LintBackend backend, LintKnob knob)
{
    LintKnobState state;
    const bool veribleKnob = isVeribleKnob(knob);

    // --- case 1: the current engine does not have the concept --------------
    // The no-op list is the C ABI's own table (plus the two verible-only tool
    // options the ABI does not model), so the page and the argv builder agree.
    if (lintKnobBackendUnsupported(backend, knob)) {
        state.kind = LintKnobStateKind::BackendUnsupported;
        if (veribleKnob) {
            if (knob == LintKnob::RulesConfig) {
                state.reason = translate(
                    "Not available: slang has no .rules.verible_lint (this is a "
                    "verible-verilog-lint option).");
            } else if (knob == LintKnob::WaiverFiles) {
                state.reason = translate(
                    "Not available: slang takes no verible waiver files (this is a "
                    "verible-verilog-lint option).");
            } else if (knob == LintKnob::Autofix) {
                state.reason = translate(
                    "Not available: slang has no automatic fixing (this is a "
                    "verible-verilog-lint option).");
            } else {
                state.reason = translate(
                    "Not available: slang has no rule set / rule selection (this is "
                    "a verible-verilog-lint option).");
            }
        } else {
            state.reason = knob == LintKnob::SlangWarnings
                ? translate("Not available: verible-verilog-lint has no -W warning "
                            "selection (this is a slang option).")
                : translate("Not available: verible-verilog-lint has no --error-limit "
                            "(this is a slang option).");
        }
        return state;
    }

    // --- case 2: the engine supports it; is it carried by the lint job? ----
    // U6 wired ruleset / rules / waiver_files / -W / --error-limit through
    // VdLintBatchOptions. `--autofix` is the last one without a field: the value
    // is persisted and usable, but it is NOT sent (R1: no silent no-op).
    if (knob == LintKnob::Autofix) {
        state.kind = LintKnobStateKind::TransportPending;
        state.reason = translate(
            "%1 is stored, but the lint job's C ABI has no field for it (transport "
            "pending).")
                           .arg(lintKnobFlagName(knob));
        return state;
    }
    state.kind = LintKnobStateKind::Supported;
    return state;
}

LintValues lintValuesFromEntries(const QVector<Entry> &entries)
{
    const auto text = [&entries](const char *key) {
        return SettingsClient::valueOf(entries, QString::fromLatin1(key));
    };
    LintValues values;
    values.verible.ruleset = text(keys::kLintVeribleRuleset);
    values.verible.rules = text(keys::kLintVeribleRules);
    values.verible.rulesConfig = text(keys::kLintVeribleRulesConfig);
    values.verible.waiverFiles = text(keys::kLintVeribleWaiverFiles);
    values.verible.autofix = text(keys::kLintVeribleAutofix);
    values.slang.warnings = text(keys::kLintSlangWarnings);
    values.slang.errorLimit = text(keys::kLintSlangErrorLimit);
    return values;
}

void writeLintEntries(QVector<Entry> *entries, const LintValues &values)
{
    if (entries == nullptr) {
        return;
    }
    const auto set = [entries](const char *key, const QString &value) {
        SettingsClient::setValue(entries, QString::fromLatin1(key), value.trimmed());
    };
    // BOTH halves are written on every change. Each half owns disjoint keys, so
    // writing the inactive half is a no-op unless its stored value is empty —
    // which is precisely the R1 guarantee: switching the engine cannot lose the
    // other set (and a stale value in the panel can never leak into it, because
    // the panel's LintValues were read from that same store).
    set(keys::kLintVeribleRuleset, values.verible.ruleset);
    set(keys::kLintVeribleRules, values.verible.rules);
    set(keys::kLintVeribleRulesConfig, values.verible.rulesConfig);
    set(keys::kLintVeribleWaiverFiles, values.verible.waiverFiles);
    set(keys::kLintVeribleAutofix, values.verible.autofix);
    set(keys::kLintSlangWarnings, values.slang.warnings);
    set(keys::kLintSlangErrorLimit, values.slang.errorLimit);
}

LintValues defaultLintValues()
{
    // All-empty values remove their keys, so "restore defaults" writes nothing.
    return LintValues{};
}

} // namespace vedit::workspace::settings
