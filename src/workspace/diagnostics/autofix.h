#pragma once

// item 9: the engine's autofix, measured instead of assumed.
//
// verible-verilog-lint supports `--autofix=[...]` (measured, F4), and
// `--ruleset=none --rules=<id> --autofix=inplace` isolates ONE rule, so the
// answer to "can this diagnostic be fixed automatically?" is not a hardcoded
// rule list but a real run on a temp copy of the editor's text:
//
//   * the copy keeps the ORIGINAL FILE NAME (rules such as `module-filename`
//     compare the module name with the file name),
//   * the text is written byte-for-byte (the editor buffer, not the disk file),
//   * the rule is supported for this text iff the engine CHANGED the copy.
//
// A backend that cannot autofix (missing binary, unusable rules config, slang
// rows whose rule id the verible engine does not know) answers `supported =
// false` with the real reason, so the hover can omit the button honestly.
//
// The answer is cached per (rule, base name, rules config, text): the same text
// in two different files can legitimately have two different answers
// (`module-filename`), so the base name and the active rules config are part of
// the key. `probeAsync()` is what the hover uses: it never runs a child process
// on the caller's stack, so moving the mouse can never freeze the GUI waiting
// for verible. `probe()` stays for callers that already own a worker (and for
// the headless tests).

#include <QHash>
#include <QString>

#include <functional>

namespace KTextEditor {
class Document;
}

namespace vedit::workspace::diagnostics {

struct AutofixOutcome {
    bool attempted = false; ///< a child process was really started
    bool supported = false; ///< the engine produced a DIFFERENT text
    QString fixedText;
    QString error; ///< why it could not run / why nothing changed

    bool ok() const { return attempted && supported; }
};

/// One isolated-rule autofix run on a temp copy of `text`. `filePathHint` only
/// supplies the base name the copy wears; nothing is written next to it.
AutofixOutcome autofixRuleInTempCopy(const QString &lintBinary,
                                     const QString &rule,
                                     const QString &filePathHint,
                                     const QString &text,
                                     const QString &rulesConfigPath = QString());

/// The cached front end the hover uses.
class AutofixEngine {
public:
    AutofixEngine() = default;
    explicit AutofixEngine(QString lintBinary);
    ~AutofixEngine();

    AutofixEngine(const AutofixEngine &) = delete;
    AutofixEngine &operator=(const AutofixEngine &) = delete;

    void setLintBinary(const QString &path);
    const QString &lintBinary() const { return lintBinary_; }
    /// The project's rules config (empty when the built-in ruleset applies).
    /// Changing it invalidates every cached answer: the same rule/text can have
    /// a different fix under a different rules file.
    void setRulesConfig(const QString &path);
    const QString &rulesConfig() const { return rulesConfig_; }

    /// "Can the engine autofix `rule` in exactly this text?" - synchronous. Use
    /// only where blocking is acceptable (tests, a worker); the hover path uses
    /// `probeAsync()`.
    AutofixOutcome probe(const QString &rule, const QString &filePathHint, const QString &text);
    /// The non-blocking probe: returns immediately, never starts the process on
    /// the caller's stack. A cached answer is delivered on the next event-loop
    /// turn; otherwise the child process runs asynchronously and `done` fires
    /// when it exits. The callback is always invoked exactly once.
    void probeAsync(const QString &rule, const QString &filePathHint, const QString &text,
                    std::function<void(const AutofixOutcome &)> done);
    /// Produce the fixed text for `rule` (never cached: the caller applies it).
    AutofixOutcome fix(const QString &rule, const QString &filePathHint, const QString &text);

    quint64 probeCount() const { return probeCount_; }
    quint64 asyncProbeCount() const { return asyncProbeCount_; }
    quint64 fixCount() const { return fixCount_; }
    bool probeInFlight() const { return !jobs_.isEmpty(); }
    void clearCache();

private:
    struct ProbeJob;
    QString cacheKey(const QString &rule, const QString &filePathHint, const QString &text) const;
    void storeOutcome(const QString &key, const AutofixOutcome &outcome);
    void finishProbe(quint64 id, const QString &error = QString());

    QString lintBinary_;
    QString rulesConfig_;
    QHash<QString, AutofixOutcome> cache_;
    qint64 cacheBytes_ = 0;
    quint64 probeCount_ = 0;
    quint64 asyncProbeCount_ = 0;
    quint64 fixCount_ = 0;
    quint64 nextJob_ = 1;
    QHash<quint64, ProbeJob *> jobs_;
};

/// item 9: install `fixedText` into an open document as ONE undoable edit.
/// Returns false when the document is null, empty, or REFUSES the replacement
/// (a read-only document) - the caller must not report success in that case.
bool applyAutofixToDocument(KTextEditor::Document *document, const QString &fixedText);

} // namespace vedit::workspace::diagnostics
