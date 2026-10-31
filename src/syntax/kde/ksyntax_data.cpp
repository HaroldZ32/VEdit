#include "ksyntax_data.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <memory>

namespace vedit::kde {

namespace {

const char *const kForbiddenSystemVerilog =
    "3361d8b4ef62c4868165ef045c02d8b6e85a1e57f61c304b399661bdc06be015"; // KDE systemverilog.xml (GPL-3.0+)
const char *const kForbiddenVerilog =
    "7199e3677b0a942e65b40f95cb2481be062b1ca48eb048519e7d4efc098f5970"; // KDE verilog.xml (licence unstated)

QString sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QString languageNameInFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString content = QString::fromUtf8(file.readAll());
    const QRegularExpression re(QStringLiteral("<language\\s+name=\"([^\"]+)\""));
    const auto match = re.match(content);
    return match.hasMatch() ? match.captured(1) : QString();
}

} // namespace

QStringList KsyntaxData::forbiddenSha256()
{
    return {QString::fromLatin1(kForbiddenSystemVerilog), QString::fromLatin1(kForbiddenVerilog)};
}

bool KsyntaxData::isForbiddenDefinitionFile(const QString &path)
{
    const QString hash = sha256OfFile(path);
    return !hash.isEmpty() && forbiddenSha256().contains(hash);
}

KsyntaxData::KsyntaxData(DataConfig config)
    : config_(std::move(config))
{
}

KsyntaxData::~KsyntaxData() = default;

QString KsyntaxData::languageNameForExtension(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("v") || suffix == QLatin1String("vh") ||
        suffix == QLatin1String("vlg")) {
        return QStringLiteral("Verilog");
    }
    if (suffix == QLatin1String("sv") || suffix == QLatin1String("svh")) {
        return QStringLiteral("SystemVerilog");
    }
    return {};
}

const LoadReport &KsyntaxData::load()
{
    if (loaded_) {
        return report_;
    }
    loaded_ = true;
    report_.private_dir = config_.private_dir;
    report_.strategy = QStringLiteral(
        "Repository::addCustomSearchPath(private_dir) at startup; XDG_DATA_DIRS is not "
        "mutated; NO_STANDARD_PATHS is not required (see src/syntax/kde/README.md)");

    repository_ = std::make_unique<KSyntaxHighlighting::Repository>();

    const auto addSearchPath = [this](const QString &path, bool required) {
        if (path.isEmpty()) {
            return;
        }
        const bool hasSyntaxDir = QDir(path + QStringLiteral("/syntax")).exists();
        if (!hasSyntaxDir) {
            if (required) {
                report_.diagnostics.append(
                    QStringLiteral("private syntax directory missing: %1/syntax (set VEDIT_SYNTAX_DIR)")
                        .arg(path));
            }
            return;
        }
        repository_->addCustomSearchPath(path);
        report_.search_paths.append(path + QStringLiteral("/syntax"));
    };

    if (!config_.private_dir.isEmpty()) {
        addSearchPath(config_.private_dir, true);
        // Refuse to load forbidden KDE definitions even if they were copied in.
        QDirIterator it(config_.private_dir + QStringLiteral("/syntax"),
                        QStringList{QStringLiteral("*.xml")}, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            if (isForbiddenDefinitionFile(path)) {
                report_.forbidden_definitions.append(path);
                continue;
            }
            report_.private_definition_files.append(QFileInfo(path).fileName());
            const QString language = languageNameInFile(path);
            if (language == QLatin1String("Verilog")) {
                report_.private_verilog_shipped = true;
            } else if (language == QLatin1String("SystemVerilog")) {
                report_.private_systemverilog_shipped = true;
            }
        }
        if (!report_.forbidden_definitions.isEmpty()) {
            report_.diagnostics.append(
                QStringLiteral("forbidden KDE syntax definition present in the private data "
                               "directory (C7/T56): %1")
                    .arg(report_.forbidden_definitions.join(QStringLiteral(", "))));
        }
    } else {
        report_.diagnostics.append(
            QStringLiteral("no private syntax directory configured; falling back to standard "
                           "paths (development only)"));
    }
    for (const QString &fallback : config_.fallback_dirs) {
        addSearchPath(fallback, false);
        report_.diagnostics.append(
            QStringLiteral("development fallback syntax path added: %1").arg(fallback));
    }

    const auto definitions = repository_->definitions();
    for (const KSyntaxHighlighting::Definition &definition : definitions) {
        report_.definition_names.append(definition.name());
    }
    const auto themes = repository_->themes();
    for (const KSyntaxHighlighting::Theme &theme : themes) {
        report_.theme_names.append(theme.name());
    }

    const bool repository_verilog =
        repository_->definitionForName(QStringLiteral("Verilog")).isValid();
    const bool repository_systemverilog =
        repository_->definitionForName(QStringLiteral("SystemVerilog")).isValid();
    const bool enforce_private = !config_.private_dir.isEmpty();
    report_.verilog_available =
        repository_verilog && (!enforce_private || report_.private_verilog_shipped);
    report_.systemverilog_available =
        repository_systemverilog &&
        (!enforce_private || report_.private_systemverilog_shipped);

    report_.probe_v_definition =
        repository_->definitionForFileName(QStringLiteral("probe.v")).name();
    report_.probe_sv_definition =
        repository_->definitionForFileName(QStringLiteral("probe.sv")).name();

    if (!report_.verilog_available) {
        report_.diagnostics.append(QStringLiteral(
            "Verilog syntax definition is not available; .v files fall back to plain text"));
    }
    if (repository_systemverilog && enforce_private && !report_.private_systemverilog_shipped) {
        report_.diagnostics.append(QStringLiteral(
            "SystemVerilog definition exists only inside the syntax library (KDE build) and is "
            "not shipped by VEdit (C7): it is deliberately not used"));
    }
    if (!report_.systemverilog_available) {
        report_.diagnostics.append(QStringLiteral(
            "SystemVerilog definition not shipped (C7 pending): .sv files degrade to the Verilog "
            "definition (SystemVerilog-only constructs lose highlighting/folding); this is a "
            "diagnosable degradation, not a crash"));
    }
    if (report_.probe_v_definition != QLatin1String("Verilog")) {
        report_.diagnostics.append(
            QStringLiteral(".v resolves to definition '%1' by file name (the built-in 'V'/vlang "
                           "definition also claims *.v); use resolveLanguageForFile()/explicit "
                           "language names instead")
                .arg(report_.probe_v_definition));
    }
    if (report_.systemverilog_available &&
        report_.probe_sv_definition != QLatin1String("SystemVerilog")) {
        report_.diagnostics.append(
            QStringLiteral(".sv resolves to definition '%1' by file name; use "
                           "resolveLanguageForFile()")
                .arg(report_.probe_sv_definition));
    }

    report_.ok = report_.verilog_available && report_.forbidden_definitions.isEmpty();
    return report_;
}

const LoadReport &KsyntaxData::report() const
{
    return report_;
}

KSyntaxHighlighting::Repository *KsyntaxData::repository() const
{
    return repository_.get();
}

QString KsyntaxData::resolveLanguageForFile(const QString &fileName)
{
    QString language = languageNameForExtension(fileName);
    if (language == QLatin1String("SystemVerilog") && !report_.systemverilog_available) {
        if (!degradation_reported_) {
            degradation_reported_ = true;
            report_.diagnostics.append(
                QStringLiteral("%1: SystemVerilog definition missing (C7) -> falling back to "
                               "Verilog base highlighting")
                    .arg(fileName));
        }
        return QStringLiteral("Verilog");
    }
    return language;
}

} // namespace vedit::kde
