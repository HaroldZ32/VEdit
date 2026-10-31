#include "toolchain.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace vedit::workspace::diagnostics {

namespace {

QString readableCandidates(const QStringList &searched)
{
    if (searched.isEmpty()) {
        return QStringLiteral("none");
    }
    return searched.join(QStringLiteral(", "));
}

/// Resolve one tool with the delivery order bundled > PATH > env.
ToolResolution resolveOne(ToolKind kind,
                          const QString &name,
                          const QString &envName,
                          const QStringList &bundledDirs,
                          const QStringList &pathDirs,
                          const QHash<QString, QString> &env)
{
    ToolResolution resolution;
    resolution.kind = kind;
    resolution.name = name;
    resolution.envName = envName;

    const auto probe = [&resolution](const QString &candidate) -> bool {
        if (candidate.isEmpty()) {
            return false;
        }
        const QFileInfo info(candidate);
        resolution.searched.append(candidate);
        return info.isFile() && info.isExecutable();
    };

    // 1) bundled: next to the product binary.
    for (const QString &dir : bundledDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            resolution.path = QFileInfo(candidate).absoluteFilePath();
            resolution.source = ToolSource::Bundled;
            return resolution;
        }
    }

    // 2) PATH.
    for (const QString &dir : pathDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            resolution.path = QFileInfo(candidate).absoluteFilePath();
            resolution.source = ToolSource::Path;
            return resolution;
        }
    }

    // 3) env: explicit override, last (never silently preferred over bundled/PATH).
    const QString override = env.value(envName).trimmed();
    if (!override.isEmpty()) {
        if (probe(override)) {
            resolution.path = QFileInfo(override).absoluteFilePath();
            resolution.source = ToolSource::Env;
            return resolution;
        }
    }
    // `*_DIR` form: directory holding the tool under its canonical name.
    const QString dirOverride = env.value(envName + QStringLiteral("_DIR")).trimmed();
    if (!dirOverride.isEmpty()) {
        const QString candidate = dirOverride + QLatin1Char('/') + name;
        if (probe(candidate)) {
            resolution.path = QFileInfo(candidate).absoluteFilePath();
            resolution.source = ToolSource::Env;
            return resolution;
        }
    }

    resolution.source = ToolSource::Missing;
    return resolution;
}

ToolchainSearchContext contextFor(const QStringList &bundledDirs,
                                  const QStringList &pathDirs,
                                  const QHash<QString, QString> &env)
{
    ToolchainSearchContext context;
    context.bundledDirs = bundledDirs;
    context.pathDirs = pathDirs;
    context.env = env;
    return context;
}

} // namespace

QString toolSourceLabel(ToolSource source, const QString &envName)
{
    switch (source) {
    case ToolSource::Bundled:
        return QStringLiteral("bundled");
    case ToolSource::Path:
        return QStringLiteral("PATH");
    case ToolSource::Env:
        return envName.isEmpty() ? QStringLiteral("env") : QStringLiteral("env:") + envName;
    case ToolSource::Missing:
        return QStringLiteral("missing");
    }
    return QStringLiteral("missing");
}

QString ToolResolution::describe() const
{
    if (found()) {
        return QStringLiteral("%1: %2 [%3]").arg(name, path, sourceLabel());
    }
    return QStringLiteral("%1: missing (probed %2)").arg(name, readableCandidates(searched));
}

QStringList ToolchainReport::missing() const
{
    QStringList absent;
    if (!verible.found()) {
        absent.append(QStringLiteral("verible"));
    }
    if (!slang.found()) {
        absent.append(QStringLiteral("slang"));
    }
    return absent;
}

QString ToolchainReport::statusLine() const
{
    const QString missingList =
        QStringLiteral("[%1]").arg(missing().join(QStringLiteral(", ")));
    return QStringLiteral("source=verible:%1 slang:%2 missing=%3")
        .arg(verible.sourceLabel(), slang.sourceLabel(), missingList);
}

QString ToolchainReport::impact() const
{
    if (missing().isEmpty()) {
        return QString();
    }
    QStringList parts;
    if (!verible.found()) {
        parts.append(QStringLiteral("Verible diagnostics, go-to-definition, references, "
                                    "rename, formatting and the outline are unavailable"));
    }
    if (!slang.found()) {
        parts.append(QStringLiteral("SLANG deep lint is unavailable"));
    }
    return parts.join(QStringLiteral("; ")) + QLatin1Char('.');
}

QString ToolchainReport::fixHint() const
{
    if (missing().isEmpty()) {
        return QString();
    }
    QStringList fixes;
    if (!verible.found()) {
        fixes.append(QStringLiteral("put `verible-verilog-ls` in the product's bin/ next to "
                                    "vd-app, install it on PATH, or set VEDIT_VERIBLE_LS"));
    }
    if (!slang.found()) {
        fixes.append(QStringLiteral("put `slang` in the product's bin/ next to vd-app, "
                                    "install it on PATH, or set VEDIT_SLANG"));
    }
    return QStringLiteral("Fix: ") + fixes.join(QStringLiteral("; ")) + QLatin1Char('.');
}

QString ToolchainReport::missingMessage(ToolKind kind) const
{
    const ToolResolution &tool = kind == ToolKind::Verible ? verible : slang;
    if (tool.found()) {
        return tool.describe();
    }
    const QString name = tool.name.isEmpty()
        ? (kind == ToolKind::Verible ? QStringLiteral("verible-verilog-ls")
                                     : QStringLiteral("slang"))
        : tool.name;
    return QStringLiteral("%1 not found (bundled, PATH, env — all empty; probed %2)")
        .arg(name, readableCandidates(tool.searched));
}

QString ToolchainReport::userMessage() const
{
    if (missing().isEmpty()) {
        return QStringLiteral("Toolchain: verible=%1, slang=%2; missing=[]")
            .arg(verible.sourceLabel(), slang.sourceLabel());
    }
    QStringList details;
    if (!verible.found()) {
        details.append(missingMessage(ToolKind::Verible));
    }
    if (!slang.found()) {
        details.append(missingMessage(ToolKind::Slang));
    }
    return QStringLiteral("Diagnostics unavailable: %1 Impact: %2 %3")
        .arg(details.join(QStringLiteral("; ")), impact(), fixHint())
        .simplified();
}

ToolchainReport resolveToolchain(const ToolchainSearchContext &context)
{
    ToolchainReport report;
    report.verible = resolveOne(ToolKind::Verible,
                                context.veribleName,
                                QStringLiteral("VEDIT_VERIBLE_LS"),
                                context.bundledDirs,
                                context.pathDirs,
                                context.env);
    report.slang = resolveOne(ToolKind::Slang,
                              context.slangName,
                              QStringLiteral("VEDIT_SLANG"),
                              context.bundledDirs,
                              context.pathDirs,
                              context.env);
    return report;
}

ToolchainSearchContext applicationToolchainContext()
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
    QStringList bundledDirs;
    if (!applicationDir.isEmpty()) {
        bundledDirs.append(applicationDir);                                 // <app>/bin
        bundledDirs.append(applicationDir + QStringLiteral("/bin"));        // portable layout
        bundledDirs.append(applicationDir + QStringLiteral("/../lib/vedit/bin")); // legacy layout
        bundledDirs.append(applicationDir + QStringLiteral("/../share/vedit/bin"));
    }

    QStringList pathDirs;
    const QString path = qEnvironmentVariable("PATH");
    if (!path.isEmpty()) {
        pathDirs = path.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    }

    QHash<QString, QString> env;
    const auto remember = [&env](const char *name) {
        const QString value = qEnvironmentVariable(name);
        if (!value.isEmpty()) {
            env.insert(QString::fromLatin1(name), value);
        }
    };
    remember("VEDIT_VERIBLE_LS");
    remember("VEDIT_VERIBLE_LS_DIR");
    remember("VEDIT_SLANG");
    remember("VEDIT_SLANG_DIR");

    return contextFor(bundledDirs, pathDirs, env);
}

QString toolchainStatusLine()
{
    const ToolchainReport report = resolveToolchain(applicationToolchainContext());
    return QStringLiteral("TOOLCHAIN %1").arg(report.statusLine());
}

} // namespace vedit::workspace::diagnostics
