#pragma once

// Private-path loading of the base-syntax data (C6) for VEdit.
//
// Strategy (chosen and recorded; see README.md §Strategy):
//   * the AppDir ships our definitions in
//       <VEDIT_SYNTAX_DIR>/syntax/*.xml        (VEDIT_SYNTAX_DIR = AppDir/share/ksyntaxhighlighting)
//   * the app calls KSyntaxHighlighting::Repository::addCustomSearchPath(<VEDIT_SYNTAX_DIR>)
//     at startup — a runtime, per-repository, revertible hook;
//   * XDG_DATA_DIRS is NOT mutated (it would leak into unrelated processes) and
//     the compile-time NO_STANDARD_PATHS is NOT relied on (it needs a rebuilt
//     library and would also drop user definitions in development).
//
// The loader never ships KDE's own systemverilog.xml / verilog.xml (C7/T56): it
// detects them by sha256 and reports them as forbidden instead of loading them.
#include <QString>
#include <QStringList>

#include <memory>

namespace KSyntaxHighlighting {
class Repository;
}

namespace vedit::kde {

struct DataConfig {
    // Directory that contains a "syntax/" subdirectory; packaging sets it from
    // VEDIT_SYNTAX_DIR. T84 tolerance: passing the `syntax` subdirectory itself is
    // normalised to its parent (the effective root is recorded in LoadReport).
    // Empty = standard paths only (development).
    QString private_dir;
    // Extra search paths, development only (recorded in the report).
    QStringList fallback_dirs;
};

struct LoadReport {
    bool ok = false;
    QString strategy;
    QString private_dir;
    QStringList search_paths;
    QStringList diagnostics;

    QStringList definition_names;
    // Definitions shipped in OUR private data dir (file-name based; the distro
    // library may also embed KDE definitions, which must not be used — C7).
    QStringList private_definition_files;
    bool private_verilog_shipped = false;
    bool private_systemverilog_shipped = false;
    QStringList theme_names;
    bool verilog_available = false;
    bool systemverilog_available = false;

    // What definitionForFileName() resolves for probe files (the .v trap).
    QString probe_v_definition;
    QString probe_sv_definition;

    // sha256 hits of KDE definitions that must never be shipped.
    QStringList forbidden_definitions;
};

class KsyntaxData {
public:
    explicit KsyntaxData(DataConfig config = {});
    ~KsyntaxData();

    KsyntaxData(const KsyntaxData &) = delete;
    KsyntaxData &operator=(const KsyntaxData &) = delete;

    // Builds the repository and the report. Idempotent.
    const LoadReport &load();
    const LoadReport &report() const;
    KSyntaxHighlighting::Repository *repository() const;

    // Explicit mapping that does not depend on file-name resolution:
    // .v/.vh/.vlg -> "Verilog", .sv/.svh -> "SystemVerilog".
    static QString languageNameForExtension(const QString &fileName);

    // resolveLanguageForFile() degrades .sv to "Verilog" (with a recorded
    // diagnostic) while the SystemVerilog definition is not shipped (C7 open).
    QString resolveLanguageForFile(const QString &fileName);

    static QStringList forbiddenSha256();
    static bool isForbiddenDefinitionFile(const QString &path);

private:
    DataConfig config_;
    std::unique_ptr<KSyntaxHighlighting::Repository> repository_;
    LoadReport report_;
    bool loaded_ = false;
    bool degradation_reported_ = false;
};

} // namespace vedit::kde
