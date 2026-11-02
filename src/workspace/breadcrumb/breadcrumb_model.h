#pragma once

// Breadcrumb bar model of the workspace lane (task T19, AC-S2.2: "面包屑随语义走").
//
// A breadcrumb is the path of SEMANTIC containers around the cursor (module >
// function > ...), produced by the parser's structure query - not a text
// heuristic and not the fold registry. The model owns only the presentation
// state:
//
//   * setPath() is fed the semantic path for the current cursor position (the
//     source side of this lane asks the syntax provider); it detects whether the
//     path changed, which is what a view uses to decide whether to repaint;
//   * display()/describe() render the path;
//   * nothing here touches folding: a cursor move or a semantic update can never
//     trigger AMENDMENTS A2's fold rebuild (the breadcrumb lane has no dependency
//     on src/syntax/folding at all, see CMakeLists.txt).
#include <QString>
#include <QStringView>

#include <cstdint>
#include <string>
#include <vector>

namespace vedit::breadcrumb {

// One semantic container: the parser's capture kind ("module", "function", ...),
// the identifier text when the structure query captured one, and the 0-based
// line the container starts on.
struct Entry {
    std::string kind;
    std::string name;
    int line = 0;

    bool operator==(const Entry &other) const
    {
        return kind == other.kind && name == other.name && line == other.line;
    }
};

class Model {
public:
    // Semantic update for the current cursor position (outer -> inner).
    void setPath(std::vector<Entry> path);

    const std::vector<Entry> &path() const;

    // "m0 > inc" - the names (kind when the query captured no name), outermost
    // first. Empty path -> empty string (the caller hides the bar).
    QString display(QStringView separator = QStringLiteral(" > ")) const;

    // "module:m0 > function:inc" - what the evidence logs.
    QString describe() const;

    quint64 updateCount() const;  // setPath calls
    quint64 changeCount() const;  // setPath calls whose path differed from the previous one

    bool matches(const std::vector<Entry> &other) const;

private:
    std::vector<Entry> path_;
    quint64 updateCount_ = 0;
    quint64 changeCount_ = 0;
};

} // namespace vedit::breadcrumb
