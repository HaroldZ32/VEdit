#include "lint_options.h"

#include "settings_client.h"

#include <QCoreApplication>

namespace vedit::workspace::settings {

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

/// The no-TTY reason both interactive autofix modes carry (batch 20261110b).
QString interactiveAutofixReason(const char *flag)
{
    return QCoreApplication::translate(
               "VEdit",
               "%1 is not available: verible-verilog-lint prompts on a terminal for this "
               "mode, and the product always starts it with pipes (no TTY). Use a "
               "non-interactive mode instead.")
        .arg(QString::fromLatin1(flag));
}

} // namespace

QVector<LintAutofixValue> lintAutofixValues()
{
    const auto entry = [](const char *value, quint8 code) {
        LintAutofixValue item;
        item.value = QString::fromLatin1(value);
        item.code = code;
        return item;
    };
    QVector<LintAutofixValue> values;
    values.append(entry("", 0)); // the "No flag" entry (verible's own `no`)
    values.append(entry(keys::kAutofixNo, 0));
    values.append(entry(keys::kAutofixPatch, 1));
    LintAutofixValue patchInteractive = entry(keys::kAutofixPatchInteractive, 2);
    patchInteractive.available = false;
    patchInteractive.reason = interactiveAutofixReason(keys::kAutofixPatchInteractive);
    values.append(patchInteractive);
    values.append(entry(keys::kAutofixInplace, 3));
    LintAutofixValue inplaceInteractive = entry(keys::kAutofixInplaceInteractive, 4);
    inplaceInteractive.available = false;
    inplaceInteractive.reason = interactiveAutofixReason(keys::kAutofixInplaceInteractive);
    values.append(inplaceInteractive);
    values.append(entry(keys::kAutofixGenerateWaiver, 5));
    return values;
}

LintAutofixValue lintAutofixValue(const QString &stored)
{
    const QString value = stored.trimmed().toLower();
    const QVector<LintAutofixValue> values = lintAutofixValues();
    for (const LintAutofixValue &item : values) {
        if (item.value == value) {
            return item;
        }
    }
    // An unknown/empty stored value is the no-flag entry (the store's typed
    // parse refuses unknown values, so this cannot lose a real setting).
    return values.isEmpty() ? LintAutofixValue{} : values.first();
}

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
                state.reason = QCoreApplication::translate(
                    "VEdit",
                    "Not available: slang has no .rules.verible_lint (this is a "
                    "verible-verilog-lint option).");
            } else if (knob == LintKnob::WaiverFiles) {
                state.reason = QCoreApplication::translate(
                    "VEdit",
                    "Not available: slang takes no verible waiver files (this is a "
                    "verible-verilog-lint option).");
            } else if (knob == LintKnob::Autofix) {
                state.reason = QCoreApplication::translate(
                    "VEdit",
                    "Not available: slang has no automatic fixing (this is a "
                    "verible-verilog-lint option).");
            } else {
                state.reason = QCoreApplication::translate(
                    "VEdit",
                    "Not available: slang has no rule set / rule selection (this is "
                    "a verible-verilog-lint option).");
            }
        } else {
            state.reason = knob == LintKnob::SlangWarnings
                ? QCoreApplication::translate("VEdit",
                                              "Not available: verible-verilog-lint has no -W warning "
                                              "selection (this is a slang option).")
                : QCoreApplication::translate("VEdit",
                                              "Not available: verible-verilog-lint has no --error-limit "
                                              "(this is a slang option).");
        }
        return state;
    }

    // --- case 2: the engine supports it; is it carried by the lint job? ----
    // Batch 20261110b: every knob below is carried by VdLintBatchOptions
    // (ruleset / rules / waiver_files / autofix / autofix_output_file /
    // slang_waiver_file / -W / --error-limit), so none of them is a
    // "stored but not sent" value any more.
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
    // Read-only on this page: the waiver lane registers this file, so the value
    // is transported but never rewritten (see writeLintEntries).
    values.slang.waiverFile = text(kLintSlangWaiverFile);
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
