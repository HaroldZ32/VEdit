#include "toolchain.h"

#include <QCoreApplication>
#include <QDir>
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
                          const QStringList &localDirs,
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

    // 2) local checkout: the source tree's own toolchain artifacts (bare build).
    for (const QString &dir : localDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            resolution.path = QFileInfo(candidate).absoluteFilePath();
            resolution.source = ToolSource::Local;
            return resolution;
        }
    }

    // 3) PATH.
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

    // 4) env: explicit override, last (never silently preferred over bundled/local/PATH).
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

/// The SOURCE CHECKOUT root: walk up from the product binary until the repo's
/// own marker file is seen. A packaged AppDir has no such marker, so the shipped
/// runtime never probes a development tree.
QString findSourceCheckoutRoot(const QString &startDir)
{
    QDir dir(startDir);
    for (int depth = 0; depth < 10; ++depth) {
        if (QFileInfo(dir.filePath(QStringLiteral("scripts/fetch-verible.sh"))).isFile()) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

/// The toolchain artifacts a bare build can produce with one command each:
/// `bash build/slang/build.sh` and `bash scripts/fetch-verible.sh`.
QStringList sourceCheckoutToolchainDirs(const QString &applicationDir)
{
    QStringList dirs;
    const QString root = findSourceCheckoutRoot(applicationDir);
    if (root.isEmpty()) {
        return dirs;
    }
    dirs.append(root + QStringLiteral("/build/slang/out"));
    dirs.append(root + QStringLiteral("/.mpd/recon/verible/bin"));
    const QDir extract(root + QStringLiteral("/target/verible-pin/extract"));
    if (extract.exists()) {
        const QStringList versions = extract.entryList(QStringList{QStringLiteral("verible-*")},
                                                       QDir::Dirs | QDir::NoDotAndDotDot,
                                                       QDir::Name);
        for (const QString &version : versions) {
            dirs.append(extract.filePath(version) + QStringLiteral("/bin"));
        }
    }
    return dirs;
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
    case ToolSource::Local:
        return QStringLiteral("local");
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
        parts.append(QStringLiteral("Verible diagnostics, navigation and the outline "
                                    "are unavailable"));
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
    return QStringLiteral("Fix: bundle `verible-verilog-ls`/`slang` in the product's bin/ "
                          "next to vd-app, run `bash scripts/fetch-verible.sh` and "
                          "`bash build/slang/build.sh` in a source checkout, install them on "
                          "PATH, or set VEDIT_VERIBLE_LS / VEDIT_SLANG.");
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
    return QStringLiteral("%1 not found (bundled, local checkout, PATH and env all empty)")
        .arg(name);
}

QString ToolchainReport::probeDetail() const
{
    QStringList parts;
    parts.append(QStringLiteral("verible-verilog-ls probed: %1")
                     .arg(readableCandidates(verible.searched)));
    parts.append(QStringLiteral("slang probed: %1").arg(readableCandidates(slang.searched)));
    return parts.join(QStringLiteral(" | "));
}

QString ToolchainReport::userMessage() const
{
    if (missing().isEmpty()) {
        // Nothing to tell the user: healthy is silent (the status line still
        // carries the chosen sources for logs and the selfcheck).
        return {};
    }
    QStringList absent;
    if (!verible.found()) {
        absent.append(verible.name.isEmpty() ? QStringLiteral("verible-verilog-ls")
                                             : verible.name);
    }
    if (!slang.found()) {
        absent.append(slang.name.isEmpty() ? QStringLiteral("slang") : slang.name);
    }
    return QStringLiteral("Diagnostics unavailable: %1 not found (bundled, local checkout, "
                          "PATH and env all empty). Impact: %2 %3")
        .arg(absent.join(QStringLiteral(", ")), impact(), fixHint())
        .simplified();
}

ToolchainReport resolveToolchain(const ToolchainSearchContext &context)
{
    ToolchainReport report;
    report.verible = resolveOne(ToolKind::Verible,
                                context.veribleName,
                                QStringLiteral("VEDIT_VERIBLE_LS"),
                                context.bundledDirs,
                                context.localDirs,
                                context.pathDirs,
                                context.env);
    report.slang = resolveOne(ToolKind::Slang,
                              context.slangName,
                              QStringLiteral("VEDIT_SLANG"),
                              context.bundledDirs,
                              context.localDirs,
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

    QStringList localDirs = sourceCheckoutToolchainDirs(applicationDir);

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

    ToolchainSearchContext context = contextFor(bundledDirs, pathDirs, env);
    context.localDirs = localDirs;
    return context;
}

QString toolchainStatusLine()
{
    const ToolchainSearchContext context = applicationToolchainContext();
    const ToolchainReport report = resolveToolchain(context);
    return QStringLiteral("TOOLCHAIN %1 local=[%2]")
        .arg(report.statusLine(), context.localDirs.join(QLatin1Char(',')));
}

} // namespace vedit::workspace::diagnostics
