#pragma once

// Fuzzy subsequence matcher for the Ctrl+P file palette (task T22, A2b).
//
// The matcher is intentionally small and allocation-free in its hot path: a
// query is matched against a pre-split display path with a single forward scan.
// Scoring follows Lapce's file-picker behaviour:
//
//   * case-insensitive subsequence match (not substring);
//   * a match on the basename beats a match in a directory component;
//   * matches on path-component boundaries (/ _ - . space and camelCase) and
//     consecutive runs beat scattered matches;
//   * a longer gap costs more, a shorter path wins ties.
//
// Results are deterministic: equal scores are ordered by candidate index, so a
// benchmark or a regression test sees the same order on every run.
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace vedit::workspace {

// Result of matching one query against one candidate path.
struct FuzzyMatch {
    // Matched character positions in the candidate, for highlighting. A query
    // longer than this still matches; only the highlight is truncated.
    static constexpr int kMaxPositions = 64;

    int score = 0;
    int positionCount = 0;
    std::array<int, kMaxPositions> positions{};
};

class FuzzyMatcher {
public:
    // `candidate` is the display path ("src/app/command_registry.cpp").
    // `basename` may be pre-split by the caller (an empty one is derived from
    // `candidate`). Returns false when `query` is not a subsequence.
    static bool match(const QString &query, const QString &candidate, const QString &basename,
                      FuzzyMatch *out);

    struct Result {
        int index = 0; // index into the candidate list
        int score = 0;
    };

    // Returns at most `limit` best results, ordered by descending score and
    // then by candidate index. `basenames` may be shorter than `candidates`;
    // missing entries are derived from the candidate itself.
    static QVector<Result> filter(const QString &query, const QStringList &candidates,
                                  const QStringList &basenames, int limit);
};

} // namespace vedit::workspace
