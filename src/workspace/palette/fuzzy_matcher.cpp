#include "fuzzy_matcher.h"

#include <QChar>
#include <QtGlobal>

#include <algorithm>

namespace vedit::workspace {
namespace {

inline char16_t foldChar(char16_t value)
{
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    if (value < 0x80) {
        return value;
    }
    return QChar(value).toLower().unicode();
}

inline bool isBoundaryChar(QChar value)
{
    return value == u'/' || value == u'\\' || value == u'_' || value == u'-' || value == u'.' ||
           value == u' ';
}

inline bool betterResult(const FuzzyMatcher::Result &left, const FuzzyMatcher::Result &right)
{
    if (left.score != right.score) {
        return left.score > right.score;
    }
    return left.index < right.index;
}

} // namespace

bool FuzzyMatcher::match(const QString &query, const QString &candidate, const QString &basename,
                         FuzzyMatch *out)
{
    if (out != nullptr) {
        *out = FuzzyMatch{};
    }
    if (query.isEmpty()) {
        return true;
    }
    const qsizetype queryLength = query.size();
    const qsizetype candidateLength = candidate.size();
    if (queryLength > candidateLength) {
        return false;
    }

    FuzzyMatch match;
    int score = 0;
    qsizetype queryIndex = 0;
    qsizetype lastPosition = -1;
    for (qsizetype index = 0; index < candidateLength && queryIndex < queryLength; ++index) {
        const QChar current = candidate.at(index);
        if (foldChar(current.unicode()) != foldChar(query.at(queryIndex).unicode())) {
            continue;
        }

        int bonus = 0;
        if (index == 0) {
            bonus += 16;
        } else {
            const QChar previous = candidate.at(index - 1);
            const bool boundary =
                isBoundaryChar(previous) || (previous.isLower() && current.isUpper());
            if (boundary) {
                bonus += 12;
            }
        }
        if (lastPosition >= 0) {
            const qsizetype gap = index - lastPosition - 1;
            if (gap == 0) {
                bonus += 10;
            } else {
                bonus -= static_cast<int>(std::min<qsizetype>(gap, 8));
            }
        }
        if (match.positionCount < FuzzyMatch::kMaxPositions) {
            match.positions[static_cast<std::size_t>(match.positionCount)] = static_cast<int>(index);
            ++match.positionCount;
        }
        score += bonus;
        lastPosition = index;
        ++queryIndex;
    }
    if (queryIndex < queryLength) {
        return false;
    }

    // The basename carries most of the user's intent: "reg" should find
    // command_registry.cpp before it finds a directory named "register".
    const QString name = basename.isEmpty() ? candidate.section(u'/', -1) : basename;
    if (name.compare(query, Qt::CaseInsensitive) == 0) {
        score += 600;
    } else if (name.startsWith(query, Qt::CaseInsensitive)) {
        score += 300 - static_cast<int>(std::min<qsizetype>(name.size(), 60));
    } else if (name.contains(query, Qt::CaseInsensitive)) {
        score += 160 - static_cast<int>(std::min<qsizetype>(name.size(), 60));
    }
    if (candidate.compare(query, Qt::CaseInsensitive) == 0) {
        score += 900;
    }
    // Prefer shallower paths when everything else is equal.
    score += std::max<int>(
                 0, 80 - static_cast<int>(std::min<qsizetype>(candidate.size(), 80))) /
             4;

    if (out != nullptr) {
        *out = match;
        out->score = score;
    }
    return true;
}

QVector<FuzzyMatcher::Result> FuzzyMatcher::filter(const QString &query,
                                                   const QStringList &candidates,
                                                   const QStringList &basenames, int limit)
{
    const qsizetype effectiveLimit = limit > 0 ? limit : 100;
    const qsizetype candidateCount = candidates.size();
    QVector<Result> matches;

    if (query.isEmpty()) {
        const qsizetype keep = std::min<qsizetype>(effectiveLimit, candidateCount);
        matches.reserve(keep);
        for (qsizetype index = 0; index < keep; ++index) {
            matches.push_back(Result{static_cast<int>(index), 0});
        }
        return matches;
    }

    matches.reserve(std::min<qsizetype>(candidateCount, 4096));
    const QString fallbackBasename;
    for (qsizetype index = 0; index < candidateCount; ++index) {
        const QString &basename =
            index < basenames.size() ? basenames.at(index) : fallbackBasename;
        FuzzyMatch match;
        if (FuzzyMatcher::match(query, candidates.at(index), basename, &match)) {
            matches.push_back(Result{static_cast<int>(index), match.score});
        }
    }

    const qsizetype keep = std::min<qsizetype>(effectiveLimit, matches.size());
    if (keep < matches.size()) {
        std::nth_element(matches.begin(), matches.begin() + keep, matches.end(), betterResult);
        matches.resize(keep);
    }
    std::sort(matches.begin(), matches.end(), betterResult);
    return matches;
}

} // namespace vedit::workspace
