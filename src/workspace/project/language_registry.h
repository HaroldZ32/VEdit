#pragma once

// The language registry (RM-11): what this build can analyse, expressed
// POSITIVELY, for every C++ consumer of a file set.
//
// One table answers, for a path, which LANGUAGE it is (by extension) and which
// CAPABILITIES this build provides for that language. The consumers ask
//   * `Capability::Source`      - may this path be a project source member?
//   * `Capability::Diagnostics` - can an LSP session analyse it?
//   * `Capability::Lint`        - can the CLI lint pass run on it?
//   * `Capability::Catalog`     - can the module catalog ingest it?
// and never carry an extension list of their own.
//
// A `.f` filelist is a project MANIFEST, not a language: it is READ as
// configuration input and has no entry below, so no consumer ever asks "should
// I skip it?" - it was never a candidate, and a manifest can never be parsed as
// HDL, linted or catalogued.
//
// RELATIONSHIP TO THE RUST REGISTRY: the authoritative data lives in
// `crates/vd-config/src/language.rs` (the project scan and the filelist
// expansion run there). This header mirrors the same table for the C++ lanes;
// the ABI does not publish the Rust table yet, and `crates/vd-ffi` is outside
// this change's write scope. Both sides pin the SAME extension contract in their
// own tests (`the_builtin_set_is_the_documented_hdl_set` and
// `rm2.*/registry-source-extensions`), so the two tables cannot drift silently.
// Follow-up once the ABI publishes it: delete the table here and read the
// registry through a `vd_language_*` call.
//
// ADDING A LANGUAGE is data: one `registerLanguage()` row (or a built-in entry
// here for the C++-side consumers) with its extensions and the capabilities
// landed for it. No consumer changes - the `rm11.*` regression registers a fake
// language and proves exactly that.

#include <QString>
#include <QStringList>
#include <QVector>

namespace vedit::workspace::project {

/// What a consumer can ask about a file (mirrors `vd_config::language::Capability`).
enum class Capability {
    Source,
    Diagnostics,
    Lint,
    Catalog,
};

/// One language this build knows, and what it can do with it.
struct LanguageSpec {
    QString id;            ///< stable id (`systemverilog`)
    QString displayName;   ///< user-facing name for a status line
    QStringList extensions; ///< without the dot, matched case-insensitively
    QVector<Capability> capabilities;
};

class LanguageRegistry {
public:
    /// The process-wide registry, seeded with the built-in languages.
    static LanguageRegistry &instance();

    const QVector<LanguageSpec> &languages() const { return languages_; }

    /// Add (or replace, by id) a language. This is the ONE seam a new language
    /// arrives through; every consumer follows automatically.
    void registerLanguage(const LanguageSpec &spec);

    /// The language of `path`, by extension (case-insensitive), or nullptr.
    const LanguageSpec *languageFor(const QString &path) const;

    /// Does this build provide `capability` for `path`?
    bool supports(const QString &path, Capability capability) const;

    /// The projection used by tests and status lines: every extension of a
    /// language that has `capability`, sorted.
    QStringList extensionsWith(Capability capability) const;

    /// The user-visible outcome when a language has no analyzer for a
    /// capability: a STATE, never a syntax error and never a "skip" log line.
    QString unavailableReason(const QString &path, Capability capability) const;

private:
    LanguageRegistry();
    QVector<LanguageSpec> languages_;
};

inline LanguageRegistry::LanguageRegistry()
{
    // The built-in table. It mirrors `crates/vd-config/src/language.rs`; keep
    // the two rows identical (both sides' tests pin the same extension set).
    languages_.append(LanguageSpec{
        QStringLiteral("systemverilog"),
        QStringLiteral("Verilog / SystemVerilog"),
        {QStringLiteral("sv"), QStringLiteral("svh"), QStringLiteral("v"), QStringLiteral("vh")},
        {Capability::Source, Capability::Diagnostics, Capability::Lint, Capability::Catalog},
    });
}

inline LanguageRegistry &LanguageRegistry::instance()
{
    static LanguageRegistry registry;
    return registry;
}

inline void LanguageRegistry::registerLanguage(const LanguageSpec &spec)
{
    for (int index = 0; index < languages_.size(); ++index) {
        if (languages_.at(index).id == spec.id) {
            languages_[index] = spec;
            return;
        }
    }
    languages_.append(spec);
}

inline const LanguageSpec *LanguageRegistry::languageFor(const QString &path) const
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == path.size() - 1) {
        return nullptr;
    }
    QString extension = path.mid(dot + 1);
    // ADDENDUM A-17: a literal trailing `$` in an expanded filelist path is part
    // of the file NAME, not of the extension (`out_tail.sv$` is Verilog).
    while (extension.endsWith(QLatin1Char('$'))) {
        extension.chop(1);
    }
    for (const LanguageSpec &spec : languages_) {
        for (const QString &known : spec.extensions) {
            if (extension.compare(known, Qt::CaseInsensitive) == 0) {
                return &spec;
            }
        }
    }
    return nullptr;
}

inline bool LanguageRegistry::supports(const QString &path, Capability capability) const
{
    const LanguageSpec *language = languageFor(path);
    return language != nullptr && language->capabilities.contains(capability);
}

inline QStringList LanguageRegistry::extensionsWith(Capability capability) const
{
    QStringList extensions;
    for (const LanguageSpec &spec : languages_) {
        if (!spec.capabilities.contains(capability)) {
            continue;
        }
        for (const QString &extension : spec.extensions) {
            if (!extensions.contains(extension, Qt::CaseInsensitive)) {
                extensions.append(extension);
            }
        }
    }
    extensions.sort(Qt::CaseInsensitive);
    return extensions;
}

inline QString LanguageRegistry::unavailableReason(const QString &path,
                                                   Capability capability) const
{
    const LanguageSpec *language = languageFor(path);
    if (language == nullptr) {
        // A manifest / note / build script is not a language at all: naming a
        // "missing analyzer" for it would be misleading, so the state says the
        // file has no analysis because it is not a source language.
        return QStringLiteral("not a source language; nothing to analyse");
    }
    Q_UNUSED(capability);
    return QStringLiteral("no analyzer for %1 in this build").arg(language->displayName);
}

} // namespace vedit::workspace::project
