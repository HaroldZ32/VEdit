#include "toolchain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>

// UX batch 2 / A4: the Preferences Format page's knobs reach the formatter here.
// `formatArguments` is inline in the header, so this lane gains no link
// dependency on the settings library.
#if __has_include("../settings/format_options.h")
#include "../settings/format_options.h"
#define VEDIT_FORMAT_HAVE_OPTIONS 1
#endif

namespace vedit::workspace::diagnostics {

namespace {

QString readableCandidates(const QStringList &searched)
{
    if (searched.isEmpty()) {
        return QStringLiteral("none");
    }
    return searched.join(QStringLiteral(", "));
}

/// Resolve one tool in the product-wide order: explicit setting -> bundled ->
/// local checkout -> imported project -> imported global -> PATH -> env.
///
/// The two imported stages are RESERVED: the product's contexts pass empty maps
/// (the merged selection lives in the Rust service layer and the C ABI exposes
/// no synchronous read); only an injected context populates them. The branches
/// stay because the order is a contract the self-test exercises.
ToolResolution resolveOne(ToolKind kind,
                          const QString &name,
                          const QString &envName,
                          const QStringList &bundledDirs,
                          const QStringList &localDirs,
                          const QHash<QString, QString> &explicitPaths,
                          const QHash<QString, QString> &importedProjectVars,
                          const QHash<QString, QString> &importedGlobalVars,
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

    const auto accept = [&resolution](const QString &candidate, ToolSource source) {
        resolution.path = QFileInfo(candidate).absoluteFilePath();
        resolution.source = source;
        return resolution;
    };

    // 1) explicit setting: the `tools.*` path wins over every discovered source.
    //    A set-but-unusable path is recorded and resolution falls through.
    const QString setting = explicitPaths.value(name).trimmed();
    if (probe(setting)) {
        return accept(setting, ToolSource::Settings);
    }

    // 2) bundled: next to the product binary.
    for (const QString &dir : bundledDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            return accept(candidate, ToolSource::Bundled);
        }
    }

    // 3) local checkout: the source tree's own toolchain artifacts (bare build).
    for (const QString &dir : localDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            return accept(candidate, ToolSource::Local);
        }
    }

    // 4-5) imported variables: the project's layer first, then the global one.
    //      Each layer is the `VEDIT_<TOOL>` override plus its `_DIR` form.
    //      RESERVED in the product: both maps are empty there (see toolchain.h);
    //      this branch is reached only by a context that injects them.
    const auto probeVariableLayer = [&](const QHash<QString, QString> &vars,
                                        ToolSource source) -> bool {
        const QString override = vars.value(envName).trimmed();
        if (probe(override)) {
            resolution.path = QFileInfo(override).absoluteFilePath();
            resolution.source = source;
            return true;
        }
        const QString dirOverride = vars.value(envName + QStringLiteral("_DIR")).trimmed();
        if (!dirOverride.isEmpty()) {
            const QString candidate = dirOverride + QLatin1Char('/') + name;
            if (probe(candidate)) {
                resolution.path = QFileInfo(candidate).absoluteFilePath();
                resolution.source = source;
                return true;
            }
        }
        // The Verible trio shares one legacy `VEDIT_VERIBLE_DIR`. The Rust
        // resolver honours it, so this half must too: one variable, one answer.
        if (envName.startsWith(QStringLiteral("VEDIT_VERIBLE_"))) {
            const QString legacy = vars.value(QStringLiteral("VEDIT_VERIBLE_DIR")).trimmed();
            if (!legacy.isEmpty()) {
                const QString candidate = legacy + QLatin1Char('/') + name;
                if (probe(candidate)) {
                    resolution.path = QFileInfo(candidate).absoluteFilePath();
                    resolution.source = source;
                    return true;
                }
            }
        }
        return false;
    };
    if (probeVariableLayer(importedProjectVars, ToolSource::ImportedProject)) {
        return resolution;
    }
    if (probeVariableLayer(importedGlobalVars, ToolSource::ImportedGlobal)) {
        return resolution;
    }

    // 6) PATH.
    for (const QString &dir : pathDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = dir + QLatin1Char('/') + name;
        if (probe(candidate)) {
            return accept(candidate, ToolSource::Path);
        }
    }

    // 7) env: explicit override, last (never silently preferred over
    //    bundled/local/imported/PATH).
    if (probeVariableLayer(env, ToolSource::Env)) {
        return resolution;
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
    case ToolSource::Settings:
        return QStringLiteral("settings");
    case ToolSource::ImportedProject:
        return QStringLiteral("imported-project");
    case ToolSource::ImportedGlobal:
        return QStringLiteral("imported-global");
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

const ToolResolution &ToolchainReport::toolFor(ToolKind kind) const
{
    switch (kind) {
    case ToolKind::Verible:
        return verible;
    case ToolKind::Slang:
        return slang;
    case ToolKind::SlangServer:
        return slangServer;
    case ToolKind::VeribleFormat:
        return veribleFormat;
    case ToolKind::VeribleLint:
        return veribleLint;
    case ToolKind::VhdlLs:
        return vhdlLs;
    case ToolKind::Ghdl:
        return ghdl;
    case ToolKind::Vsg:
        return vsg;
    case ToolKind::ScalaJava:
        return scalaJava;
    case ToolKind::ScalaCli:
        return scalaCli;
    case ToolKind::Sbt:
        return sbt;
    case ToolKind::Metals:
        return metals;
    }
    return slang;
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

QStringList ToolchainReport::auxiliaryMissing() const
{
    QStringList absent;
    if (!slangServer.found()) {
        absent.append(QStringLiteral("slang-server"));
    }
    if (!veribleFormat.found()) {
        absent.append(QStringLiteral("verible-format"));
    }
    if (!veribleLint.found()) {
        absent.append(QStringLiteral("verible-lint"));
    }
    return absent;
}

QString ToolchainReport::auxiliaryStatusLine() const
{
    return QStringLiteral("aux source=slang-server:%1 verible-format:%2 verible-lint:%3 missing=[%4]")
        .arg(slangServer.sourceLabel(),
             veribleFormat.sourceLabel(),
             veribleLint.sourceLabel(),
             auxiliaryMissing().join(QStringLiteral(", ")));
}

QStringList ToolchainReport::scalaMissing() const
{
    QStringList absent;
    for (const ToolResolution *tool : {&scalaJava, &scalaCli, &sbt, &metals}) {
        if (!tool->found()) {
            absent.append(tool->name);
        }
    }
    return absent;
}

QString ToolchainReport::scalaStatusLine() const
{
    return QStringLiteral("source=java:%1 scala-cli:%2 sbt:%3 metals:%4 missing=[%5]")
        .arg(scalaJava.sourceLabel(), scalaCli.sourceLabel(), sbt.sourceLabel(),
             metals.sourceLabel(), scalaMissing().join(QStringLiteral(", ")));
}

QStringList ToolchainReport::vhdlMissing() const
{
    QStringList absent;
    for (const ToolResolution *tool : {&vhdlLs, &ghdl, &vsg}) {
        if (!tool->found()) {
            absent.append(tool->name);
        }
    }
    return absent;
}

QString ToolchainReport::vhdlStatusLine() const
{
    return QStringLiteral("source=vhdl_ls:%1 ghdl:%2 vsg:%3 missing=[%4]")
        .arg(vhdlLs.sourceLabel(), ghdl.sourceLabel(), vsg.sourceLabel(),
             vhdlMissing().join(QStringLiteral(", ")));
}

QString ToolchainReport::vhdlImpact() const
{
    if (vhdlMissing().isEmpty()) {
        return QString();
    }
    // T60: one clause PER MISSING TOOL, so a missing GHDL is never described as
    // a navigation problem (navigation is `vhdl_ls`'s job). The clauses are
    // translated: the same text reaches the status bar, the tooltip and the
    // shell diagnostic.
    QStringList parts;
    if (!vhdlLs.found()) {
        parts.append(QCoreApplication::translate(
            "VEdit", "VHDL diagnostics, hover and go-to-definition are unavailable"));
    }
    if (!ghdl.found() && !vsg.found()) {
        parts.append(QCoreApplication::translate("VEdit", "GHDL+VSG VHDL lint is unavailable"));
    } else if (!ghdl.found()) {
        parts.append(QCoreApplication::translate(
            "VEdit",
            "GHDL analysis and the merged GHDL+VSG VHDL lint are unavailable"));
    } else if (!vsg.found()) {
        parts.append(QCoreApplication::translate(
            "VEdit", "the VSG VHDL style rules and their --fix formatting are unavailable"));
    }
    return parts.join(QStringLiteral("; ")) + QLatin1Char('.');
}

QString ToolchainReport::vhdlFixHint() const
{
    if (vhdlMissing().isEmpty()) {
        return QString();
    }
    return QCoreApplication::translate(
        "VEdit", "Fix: bundle `vhdl_ls`/`ghdl`/`vsg` in the product's bin/ next to vd-app "
                 "(bash packaging/runtime/build-vhdl-payload.sh), install them on PATH, or "
                 "set VEDIT_VHDL_LS / VEDIT_GHDL / VEDIT_VSG.");
}

QString ToolchainReport::vhdlUserMessage() const
{
    const QStringList absent = vhdlMissing();
    if (absent.isEmpty()) {
        // AC-6.7: a healthy VHDL toolchain is silent — no status line at all.
        return {};
    }
    return QCoreApplication::translate(
               "VEdit", "VHDL tools unavailable: %1 not found (bundled, local checkout, "
                        "imported vars, PATH and env all empty). Impact: %2 %3")
        .arg(absent.join(QStringLiteral(", ")), vhdlImpact(), vhdlFixHint());
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
    const ToolResolution &tool = toolFor(kind);
    if (tool.found()) {
        return tool.describe();
    }
    QString fallback;
    switch (kind) {
    case ToolKind::Verible:
        fallback = QStringLiteral("verible-verilog-ls");
        break;
    case ToolKind::Slang:
        fallback = QStringLiteral("slang");
        break;
    case ToolKind::SlangServer:
        fallback = QStringLiteral("slang-server");
        break;
    case ToolKind::VeribleFormat:
        fallback = QStringLiteral("verible-verilog-format");
        break;
    case ToolKind::VeribleLint:
        fallback = QStringLiteral("verible-verilog-lint");
        break;
    case ToolKind::VhdlLs:
        fallback = QStringLiteral("vhdl_ls");
        break;
    case ToolKind::Ghdl:
        fallback = QStringLiteral("ghdl");
        break;
    case ToolKind::Vsg:
        fallback = QStringLiteral("vsg");
        break;
    case ToolKind::ScalaJava:
        fallback = QStringLiteral("java");
        break;
    case ToolKind::ScalaCli:
        fallback = QStringLiteral("scala-cli");
        break;
    case ToolKind::Sbt:
        fallback = QStringLiteral("sbt");
        break;
    case ToolKind::Metals:
        fallback = QStringLiteral("metals");
        break;
    }
    const QString name = tool.name.isEmpty() ? fallback : tool.name;
    return QStringLiteral("%1 not found (bundled, local checkout, imported vars, PATH and "
                          "env all empty)")
        .arg(name);
}

QString ToolchainReport::probeDetail() const
{
    QStringList parts;
    parts.append(QStringLiteral("verible-verilog-ls probed: %1")
                     .arg(readableCandidates(verible.searched)));
    parts.append(QStringLiteral("slang probed: %1").arg(readableCandidates(slang.searched)));
    if (!slangServer.searched.isEmpty() || slangServer.found()) {
        parts.append(QStringLiteral("slang-server probed: %1")
                         .arg(readableCandidates(slangServer.searched)));
    }
    if (!veribleFormat.searched.isEmpty() || veribleFormat.found()) {
        parts.append(QStringLiteral("verible-verilog-format probed: %1")
                         .arg(readableCandidates(veribleFormat.searched)));
    }
    if (!veribleLint.searched.isEmpty() || veribleLint.found()) {
        parts.append(QStringLiteral("verible-verilog-lint probed: %1")
                         .arg(readableCandidates(veribleLint.searched)));
    }
    for (const ToolResolution *tool : {&vhdlLs, &ghdl, &vsg}) {
        if (!tool->searched.isEmpty() || tool->found()) {
            parts.append(QStringLiteral("%1 probed: %2").arg(tool->name, readableCandidates(tool->searched)));
        }
    }
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
    // NOTE (E5): the three user-facing "not found" messages (this one and the
    // two above) still name "imported vars" among the places probed. The
    // catalogue translations (`translations/vedit_zh_CN.ts`) carry those exact
    // source strings and the lrelease gate requires zero untranslated entries,
    // so the wording is kept as-is in this batch; the imported stages are
    // RESERVED and empty (see `applicationToolchainContext()`), so they can
    // never be the reason a tool was found. Removing the phrase needs a
    // translation update, not a comment change.
    return QStringLiteral("Diagnostics unavailable: %1 not found (bundled, local checkout, "
                          "imported vars, PATH and env all empty). Impact: %2 %3")
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
                                context.explicitPaths,
                                context.importedProjectVars,
                                context.importedGlobalVars,
                                context.pathDirs,
                                context.env);
    report.slang = resolveOne(ToolKind::Slang,
                              context.slangName,
                              QStringLiteral("VEDIT_SLANG"),
                              context.bundledDirs,
                              context.localDirs,
                              context.explicitPaths,
                              context.importedProjectVars,
                              context.importedGlobalVars,
                              context.pathDirs,
                              context.env);
    report.slangServer = resolveOne(ToolKind::SlangServer,
                                    context.slangServerName,
                                    QStringLiteral("VEDIT_SLANG_SERVER"),
                                    context.bundledDirs,
                                    context.localDirs,
                                    context.explicitPaths,
                                    context.importedProjectVars,
                                    context.importedGlobalVars,
                                    context.pathDirs,
                                    context.env);
    report.veribleFormat = resolveOne(ToolKind::VeribleFormat,
                                      context.veribleFormatName,
                                      QStringLiteral("VEDIT_VERIBLE_FORMAT"),
                                      context.bundledDirs,
                                      context.localDirs,
                                      context.explicitPaths,
                                      context.importedProjectVars,
                                      context.importedGlobalVars,
                                      context.pathDirs,
                                      context.env);
    report.veribleLint = resolveOne(ToolKind::VeribleLint,
                                    context.veribleLintName,
                                    QStringLiteral("VEDIT_VERIBLE_LINT"),
                                    context.bundledDirs,
                                    context.localDirs,
                                    context.explicitPaths,
                                    context.importedProjectVars,
                                    context.importedGlobalVars,
                                    context.pathDirs,
                                    context.env);
    report.vhdlLs = resolveOne(ToolKind::VhdlLs,
                               context.vhdlLsName,
                               QStringLiteral("VEDIT_VHDL_LS"),
                               context.bundledDirs,
                               context.localDirs,
                               context.explicitPaths,
                               context.importedProjectVars,
                               context.importedGlobalVars,
                               context.pathDirs,
                               context.env);
    report.ghdl = resolveOne(ToolKind::Ghdl,
                             context.ghdlName,
                             QStringLiteral("VEDIT_GHDL"),
                             context.bundledDirs,
                             context.localDirs,
                             context.explicitPaths,
                             context.importedProjectVars,
                             context.importedGlobalVars,
                             context.pathDirs,
                             context.env);
    report.vsg = resolveOne(ToolKind::Vsg,
                            context.vsgName,
                            QStringLiteral("VEDIT_VSG"),
                            context.bundledDirs,
                            context.localDirs,
                            context.explicitPaths,
                            context.importedProjectVars,
                            context.importedGlobalVars,
                            context.pathDirs,
                            context.env);
    // T12: the offline Scala toolchain. Bundled means `bin/java`, `bin/scala-cli`,
    // `bin/sbt`, `bin/metals` next to the product binary — the wrappers the
    // AppDir installs, each of which redirects the cache/daemon homes inside the
    // AppDir (see THIRD_PARTY_NOTICES.md §11).
    report.scalaJava = resolveOne(ToolKind::ScalaJava,
                                  context.scalaJavaName,
                                  QStringLiteral("VEDIT_JAVA"),
                                  context.bundledDirs,
                                  context.localDirs,
                                  context.explicitPaths,
                                  context.importedProjectVars,
                                  context.importedGlobalVars,
                                  context.pathDirs,
                                  context.env);
    report.scalaCli = resolveOne(ToolKind::ScalaCli,
                                 context.scalaCliName,
                                 QStringLiteral("VEDIT_SCALA_CLI"),
                                 context.bundledDirs,
                                 context.localDirs,
                                 context.explicitPaths,
                                 context.importedProjectVars,
                                 context.importedGlobalVars,
                                 context.pathDirs,
                                 context.env);
    report.sbt = resolveOne(ToolKind::Sbt,
                            context.sbtName,
                            QStringLiteral("VEDIT_SBT"),
                            context.bundledDirs,
                            context.localDirs,
                            context.explicitPaths,
                            context.importedProjectVars,
                            context.importedGlobalVars,
                            context.pathDirs,
                            context.env);
    report.metals = resolveOne(ToolKind::Metals,
                               context.metalsName,
                               QStringLiteral("VEDIT_METALS"),
                               context.bundledDirs,
                               context.localDirs,
                               context.explicitPaths,
                               context.importedProjectVars,
                               context.importedGlobalVars,
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
    remember("VEDIT_VERIBLE_DIR");
    remember("VEDIT_SLANG");
    remember("VEDIT_SLANG_DIR");
    remember("VEDIT_SLANG_SERVER");
    remember("VEDIT_SLANG_SERVER_DIR");
    remember("VEDIT_VERIBLE_FORMAT");
    remember("VEDIT_VERIBLE_FORMAT_DIR");
    remember("VEDIT_VERIBLE_LINT");
    remember("VEDIT_VERIBLE_LINT_DIR");
    remember("VEDIT_VHDL_LS");
    remember("VEDIT_VHDL_LS_DIR");
    remember("VEDIT_GHDL");
    remember("VEDIT_GHDL_DIR");
    remember("VEDIT_VSG");
    remember("VEDIT_VSG_DIR");
    // The Scala group (T12/E5): `resolveOne` passes exactly these four names for
    // java / scala-cli / sbt / metals. They were missing from this table, so
    // `env:VEDIT_JAVA` (and the other three) could never resolve.
    remember("VEDIT_JAVA");
    remember("VEDIT_SCALA_CLI");
    remember("VEDIT_SBT");
    remember("VEDIT_METALS");

    // `explicitPaths` stays empty here: the `tools.*` settings keys reach the
    // report through `applyToolOverrides` (this lane has no settings dependency).
    //
    // `importedProjectVars` / `importedGlobalVars` stay empty here PERMANENTLY as
    // far as this function is concerned, and that is a design limit, not a TODO:
    // the imported shell selection is merged in the Rust service layer
    // (`vd_deeplint::binary::set_imported_environment`) and handed to CHILD
    // processes; the C ABI has no synchronous accessor, so C++ discovery cannot
    // read it. Do NOT "fix" this by exporting the selection into the process
    // environment: that would leak every imported value (including revealed
    // secrets) into every child and the terminal, and it needs a user decision.
    // Imported variables therefore reach lint/catalog/hierarchy children and
    // filelist expansion — never this resolver. See `toolchain.h` and E5.

    ToolchainSearchContext context = contextFor(bundledDirs, pathDirs, env);
    context.localDirs = localDirs;
    return context;
}

QString toolchainStatusLine()
{
    const ToolchainSearchContext context = applicationToolchainContext();
    const ToolchainReport report = resolveToolchain(context);
    // T12: the Scala group is reported in the same line, with its own chosen
    // source, so a machine-readable selfcheck shows whether the offline payload
    // (or a user's own toolchain) answered for java/scala-cli/sbt/metals.
    return QStringLiteral("TOOLCHAIN %1 %2 local=[%3] vhdl %4 scala %5")
        .arg(report.statusLine(), report.auxiliaryStatusLine(),
             context.localDirs.join(QLatin1Char(',')), report.vhdlStatusLine(),
             report.scalaStatusLine());
}

namespace {

ToolResolution *resolutionForName(ToolchainReport *report, const QString &name)
{
    const QString canonical = QFileInfo(name).fileName();
    if (canonical == QStringLiteral("verible-verilog-ls")) {
        return &report->verible;
    }
    if (canonical == QStringLiteral("slang")) {
        return &report->slang;
    }
    if (canonical == QStringLiteral("slang-server")) {
        return &report->slangServer;
    }
    if (canonical == QStringLiteral("verible-verilog-format")) {
        return &report->veribleFormat;
    }
    if (canonical == QStringLiteral("verible-verilog-lint")) {
        return &report->veribleLint;
    }
    return nullptr;
}

} // namespace

void applyToolOverrides(ToolchainReport *report, const QHash<QString, QString> &paths)
{
    if (report == nullptr) {
        return;
    }
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        const QString value = it.value().trimmed();
        if (value.isEmpty()) {
            continue;
        }
        ToolResolution *tool = resolutionForName(report, it.key());
        if (tool == nullptr) {
            continue;
        }
        const QFileInfo info(value);
        // Record the explicit probe even when it is unusable: stage 1 was
        // attempted, so a stale settings path must be visible rather than
        // silently replaced by the discovered tool.
        tool->searched.prepend(value);
        if (!info.isFile() || !info.isExecutable()) {
            continue;
        }
        tool->path = info.absoluteFilePath();
        tool->source = ToolSource::Settings;
    }
}

bool formatVerilog(const QString &formatterPath, const QString &text, QString *formatted,
                   QString *error)
{
    if (formatted == nullptr) {
        return false;
    }
    if (formatterPath.isEmpty() || !QFileInfo(formatterPath).isExecutable()) {
        if (error != nullptr) {
            *error = QStringLiteral("verible-verilog-format is missing");
        }
        return false;
    }
    QProcess process;
    QStringList arguments;
#if defined(VEDIT_FORMAT_HAVE_OPTIONS)
    // One flag per non-empty knob, in the table's order (plan §1 F3); an unset
    // knob emits nothing, so an untouched page keeps the pre-A4 argv exactly.
    arguments = vedit::workspace::settings::formatArguments(
        vedit::workspace::settings::currentFormatValues());
#endif
    // The formatter reads stdin when the file argument is `-`.
    arguments.append(QStringLiteral("-"));
    process.start(formatterPath, arguments);
    if (!process.waitForStarted(3000)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot start %1: %2").arg(formatterPath, process.errorString());
        }
        return false;
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error != nullptr) {
            *error = QStringLiteral("%1 timed out").arg(QFileInfo(formatterPath).fileName());
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 failed: %2")
                         .arg(QFileInfo(formatterPath).fileName(),
                              QString::fromUtf8(process.readAllStandardError()).trimmed());
        }
        return false;
    }
    *formatted = QString::fromUtf8(process.readAllStandardOutput());
    return true;
}

} // namespace vedit::workspace::diagnostics
