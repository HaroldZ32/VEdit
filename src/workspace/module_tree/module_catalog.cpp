#include "module_catalog.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace vedit::workspace::module_tree {

namespace {

QString canonicalOrAbsolute(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace

const ModuleDefinition *ModuleCatalog::find(const QString &name) const
{
    for (const ModuleDefinition &module : modules) {
        if (module.name == name) {
            return &module;
        }
    }
    return nullptr;
}

QStringList ModuleCatalog::topModuleNames() const
{
    QStringList names;
    for (const ModuleDefinition &module : modules) {
        if (module.isTop) {
            names.append(module.name);
        }
    }
    return names;
}

QString ModuleCatalog::summaryLine() const
{
    if (!error.isEmpty()) {
        return QCoreApplication::translate("VEdit", "module catalog: %1").arg(error);
    }
    if (!valid) {
        return QCoreApplication::translate("VEdit", "no module catalog yet");
    }
    int tops = 0;
    for (const ModuleDefinition &module : modules) {
        if (module.isTop) {
            ++tops;
        }
    }
    QString line = QCoreApplication::translate("VEdit",
                                               "%1 · %2 module(s) · %3 top(s) · %4 ms · %5 KiB "
                                               "peak")
                       .arg(tool.isEmpty() ? QStringLiteral("slang") : tool)
                       .arg(modules.size())
                       .arg(tops)
                       .arg(elapsedMs)
                       .arg(peakRssKb);
    if (!warning.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · ⚠ %1").arg(warning);
    }
    if (!top.isEmpty()) {
        line += QCoreApplication::translate("VEdit", " · top: %1").arg(top);
    }
    return line;
}

ModuleJump jumpFromPosition(const QString &file, int line, int column)
{
    ModuleJump jump;
    if (file.isEmpty() || line < 0) {
        return jump;
    }
    jump.file = file;
    jump.line = line + 1;
    jump.column = column >= 0 ? column + 1 : 1;
    return jump;
}

ModuleJump jumpFromDefinition(const ModuleDefinition &module)
{
    return jumpFromPosition(module.file, module.line, module.column);
}

ModuleJump jumpFromPort(const ModulePort &port)
{
    return jumpFromPosition(port.file, port.line, port.column);
}

ModuleJump jumpFromInstance(const ModuleInstance &instance)
{
    return jumpFromPosition(instance.file, instance.line, instance.column);
}

QString describePosition(const QString &file, int line, int column)
{
    if (file.isEmpty() || line < 0) {
        return QStringLiteral("-");
    }
    const QString name = QFileInfo(file).fileName();
    return QStringLiteral("%1:%2:%3").arg(name).arg(line + 1).arg(column + 1);
}

PortWidth portWidthFromType(const QString &type)
{
    PortWidth answer;
    const QString text = type.trimmed();
    if (text.isEmpty()) {
        answer.problem = QCoreApplication::translate(
            "VEdit", "the ABI reported no type string for this port");
        return answer;
    }

    // One literal packed dimension is `[<integer> : <integer>]`; a range that
    // mentions a parameter (`[WIDTH-1:0]`) is deliberately NOT evaluated here:
    // the panel must not invent a width the source does not literally state.
    static const QRegularExpression rangePattern(
        QStringLiteral("^\\s*([+-]?\\d+)\\s*:\\s*([+-]?\\d+)\\s*$"));

    qlonglong product = 1;
    int dimensions = 0;
    int cursor = 0;
    while (true) {
        const int open = text.indexOf(QLatin1Char('['), cursor);
        if (open < 0) {
            break;
        }
        const int close = text.indexOf(QLatin1Char(']'), open + 1);
        if (close < 0) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the type `%1` has an unclosed packed dimension")
                                 .arg(text);
            return answer;
        }
        const QString body = text.mid(open + 1, close - open - 1);
        const QRegularExpressionMatch match = rangePattern.match(body);
        if (!match.hasMatch()) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit",
                                 "the packed range `[%1]` in `%2` is not a literal msb:lsb "
                                 "pair, so the width cannot be derived")
                                 .arg(body.trimmed(), text);
            return answer;
        }
        bool msbOk = false;
        bool lsbOk = false;
        const qlonglong msb = match.captured(1).toLongLong(&msbOk);
        const qlonglong lsb = match.captured(2).toLongLong(&lsbOk);
        if (!msbOk || !lsbOk) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the packed range `[%1]` in `%2` is too large to "
                                          "represent")
                                 .arg(body.trimmed(), text);
            return answer;
        }
        const qlonglong dimension = std::llabs(msb - lsb) + 1;
        if (dimension <= 0 || product > static_cast<qlonglong>(INT_MAX) / dimension) {
            answer.problem = QCoreApplication::translate(
                                 "VEdit", "the packed dimensions in `%1` exceed the supported "
                                          "width")
                                 .arg(text);
            return answer;
        }
        product *= dimension;
        ++dimensions;
        cursor = close + 1;
    }

    answer.known = true;
    answer.width = dimensions == 0 ? 1 : static_cast<int>(product);
    return answer;
}

bool sameFile(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
    const QString a = canonicalOrAbsolute(left);
    const QString b = canonicalOrAbsolute(right);
    return !a.isEmpty() && a == b;
}

const ModuleDefinition *moduleAtCaret(const ModuleCatalog &catalog, const QString &file,
                                      int caretLine0)
{
    if (file.isEmpty() || caretLine0 < 0) {
        return nullptr;
    }
    const ModuleDefinition *best = nullptr;
    for (const ModuleDefinition &module : catalog.modules) {
        if (module.line < 0 || !sameFile(module.file, file)) {
            continue;
        }
        if (module.line > caretLine0) {
            continue;
        }
        // "nearest definition at or above the caret": the largest line wins; an
        // exact tie keeps catalog order (sorted by name) and is deterministic.
        if (best == nullptr || module.line > best->line) {
            best = &module;
        }
    }
    return best;
}

QVector<InstantiationSite> instantiationsOf(const ModuleCatalog &catalog,
                                            const QString &moduleName)
{
    QVector<InstantiationSite> sites;
    if (moduleName.isEmpty()) {
        return sites;
    }
    for (const ModuleDefinition &parent : catalog.modules) {
        for (const ModuleInstance &instance : parent.children) {
            if (instance.module != moduleName) {
                continue;
            }
            InstantiationSite site;
            site.parentModule = parent.name;
            site.instance = instance;
            sites.append(site);
        }
    }
    std::stable_sort(sites.begin(), sites.end(),
                     [](const InstantiationSite &left, const InstantiationSite &right) {
                         if (left.parentModule != right.parentModule) {
                             return left.parentModule < right.parentModule;
                         }
                         if (left.instance.file != right.instance.file) {
                             return left.instance.file < right.instance.file;
                         }
                         if (left.instance.line != right.instance.line) {
                             return left.instance.line < right.instance.line;
                         }
                         return left.instance.column < right.instance.column;
                     });
    return sites;
}

} // namespace vedit::workspace::module_tree
