#include "ksyntax_fold_source.h"

#include <QChar>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace vedit::folding {
namespace {

bool isWordStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isWordPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

} // namespace

KsyntaxFoldSource::KsyntaxFoldSource(QString definitionPath)
    : path_(std::move(definitionPath))
{
    load();
}

void KsyntaxFoldSource::load()
{
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
        lastDiagnostic_ = QStringLiteral("cannot open %1: %2").arg(path_, file.errorString());
        return;
    }
    QHash<QString, QStringList> lists;
    QHash<QString, QString> beginList; // region -> list name
    QHash<QString, QString> endList;   // region -> list name
    QStringList listOrder;
    QString currentList;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        const QString element = xml.name().toString();
        if (element == QLatin1String("language")) {
            languageName_ = xml.attributes().value(QLatin1String("name")).toString();
            section_ = xml.attributes().value(QLatin1String("section")).toString();
            author_ = xml.attributes().value(QLatin1String("author")).toString();
        } else if (element == QLatin1String("list")) {
            currentList = xml.attributes().value(QLatin1String("name")).toString();
            lists.insert(currentList, QStringList{});
            listOrder.append(currentList);
        } else if (element == QLatin1String("item") && !currentList.isEmpty()) {
            lists[currentList].append(xml.readElementText());
        } else if (element == QLatin1String("keyword")) {
            const QString string = xml.attributes().value(QLatin1String("String")).toString();
            const QString begin = xml.attributes().value(QLatin1String("beginRegion")).toString();
            const QString end = xml.attributes().value(QLatin1String("endRegion")).toString();
            if (!string.isEmpty() && !begin.isEmpty()) {
                beginList.insert(begin, string);
            }
            if (!string.isEmpty() && !end.isEmpty()) {
                endList.insert(end, string);
            }
        }
    }
    if (xml.hasError()) {
        lastDiagnostic_ = QStringLiteral("cannot parse %1: %2").arg(path_, xml.errorString());
        return;
    }

    // Prefer the region our definitions use ("veditFold"), otherwise the first
    // region that has BOTH a begin and an end keyword-list rule.
    QStringList candidates;
    for (auto it = beginList.constBegin(); it != beginList.constEnd(); ++it) {
        if (endList.contains(it.key()) && lists.contains(it.value())
            && lists.contains(endList.value(it.key()))) {
            candidates.append(it.key());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    if (candidates.contains(QStringLiteral("veditFold"))) {
        regionName_ = QStringLiteral("veditFold");
    } else if (!candidates.isEmpty()) {
        regionName_ = candidates.first();
    }
    if (regionName_.isEmpty()) {
        lastDiagnostic_ = QStringLiteral("%1 declares no keyword-list fold region")
                              .arg(QFileInfo(path_).fileName());
        return;
    }
    beginKeywords_ = lists.value(beginList.value(regionName_));
    endKeywords_ = lists.value(endList.value(regionName_));
    if (beginKeywords_.isEmpty() || endKeywords_.isEmpty()) {
        lastDiagnostic_ = QStringLiteral("fold lists of region %1 are empty").arg(regionName_);
        return;
    }
    loaded_ = true;
    lastDiagnostic_ = QStringLiteral("loaded %1 (%2) region=%3 begin=%4 end=%5")
                          .arg(QFileInfo(path_).fileName(), languageName_, regionName_)
                          .arg(beginKeywords_.size())
                          .arg(endKeywords_.size());
}

QString KsyntaxFoldSource::name() const
{
    return QStringLiteral("ksyntax:%1").arg(languageName_.isEmpty() ? path_ : languageName_);
}

std::vector<FoldRange> KsyntaxFoldSource::ranges(QStringView text)
{
    std::vector<FoldRange> out;
    lastUnbalancedCount_ = 0;
    if (!loaded_) {
        lastDiagnostic_ = QStringLiteral("source is not loaded: %1").arg(lastDiagnostic_);
        lastRangeCount_ = 0;
        return out;
    }

    std::vector<int> stack;
    const QStringList lines = text.toString().split(QLatin1Char('\n'));
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &line = lines.at(lineIndex);
        int index = 0;
        while (index < line.size()) {
            const QChar ch = line.at(index);
            if (ch == QLatin1Char('/') && index + 1 < line.size()
                && line.at(index + 1) == QLatin1Char('/')) {
                break; // the XML's Normal context treats // to end of line as comment
            }
            if (ch == QLatin1Char('"')) {
                ++index;
                while (index < line.size() && line.at(index) != QLatin1Char('"')) {
                    ++index;
                }
                ++index;
                continue;
            }
            if (!isWordStart(ch)) {
                ++index;
                continue;
            }
            const int wordStart = index;
            while (index < line.size() && isWordPart(line.at(index))) {
                ++index;
            }
            const QString word = line.mid(wordStart, index - wordStart);
            if (beginKeywords_.contains(word)) {
                stack.push_back(lineIndex);
            } else if (endKeywords_.contains(word)) {
                if (stack.empty()) {
                    ++lastUnbalancedCount_;
                    continue;
                }
                const int start = stack.back();
                stack.pop_back();
                if (lineIndex > start) {
                    out.push_back(FoldRange{start, lineIndex});
                } else {
                    ++lastUnbalancedCount_;
                }
            }
        }
    }
    lastUnbalancedCount_ += static_cast<int>(stack.size());
    lastRangeCount_ = static_cast<int>(out.size());
    return out;
}

} // namespace vedit::folding
