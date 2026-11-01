#pragma once

// Closed-state fold source (task T29 / AC-S3.5 remainder): fold ranges from
// VEdit's OWN KSyntaxHighlighting definition, not from tree-sitter.
//
// The A4 premise: with the runtime tree-sitter switch OFF the editor must still
// fold. resources/syntax/vedit-systemverilog.xml declares its fold regions in the
// KDE keyword-list idiom (task T45): the list `veditFoldBegin` is bound to
// `beginRegion="veditFold"` and `veditFoldEnd` to `endRegion="veditFold"`, so the
// XML itself is the folding rule. This source applies exactly those rules to a
// text: it parses the definition file (never the editor's repository, so no
// embedded KDE definition can win) and matches the XML's own begin/end keyword
// lists with a balanced stack.
//
// It is deliberately independent of the injector: the expected fold set in the
// acceptance test is hard-coded from the sample's construction, and the registry
// is never an assertion source (AMENDMENTS A3.2). It is also independent of the
// loaded KSyntaxHighlighting library: the file is read directly, so the result is
// the same on the delivery (F2, QRC_SYNTAX=OFF) library and on a host library
// that embeds 451 KDE definitions.
//
// Supported text subset (what the XML's Normal context folds): `//` line comments
// and double-quoted strings are ignored while matching keywords; the XML's
// block-comment region is not a keyword list and is not folded here. The sample
// used by the acceptance test is inside this subset.
#include "fold_source.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace vedit::folding {

class KsyntaxFoldSource final : public FoldSource {
public:
    // `definitionPath` is a KSyntaxHighlighting XML file, normally
    // resources/syntax/vedit-systemverilog.xml.
    explicit KsyntaxFoldSource(QString definitionPath);

    QString name() const override;
    std::vector<FoldRange> ranges(QStringView text) override;

    // A1f: this source is the tree-sitter-OFF side, so it never claims to
    // reflect the parse.
    bool treeSitterActive() const override { return false; }

    // Facts read from the XML. The acceptance test asserts them so a foreign
    // definition can never be mistaken for ours.
    bool isLoaded() const { return loaded_; }
    QString definitionPath() const { return path_; }
    QString definitionName() const { return languageName_; }
    QString definitionSection() const { return section_; }
    QString definitionAuthor() const { return author_; }
    QString regionName() const { return regionName_; }
    QStringList beginKeywords() const { return beginKeywords_; }
    QStringList endKeywords() const { return endKeywords_; }

    QString lastDiagnostic() const { return lastDiagnostic_; }
    int lastRangeCount() const { return lastRangeCount_; }

    // The ONE path resolution (T109): `VEDIT_SYNTAX_DIR` is a data root, not the
    // `syntax` subdirectory; the shared helper also accepts the subdirectory
    // spelling and the bare-build checkout fallback.
    static QString definitionDirectoryFromEnvironment();
    static QString definitionPathForMode(const QString &mode);
    int lastUnbalancedCount() const { return lastUnbalancedCount_; }

private:
    void load();

    QString path_;
    bool loaded_ = false;
    QString languageName_;
    QString section_;
    QString author_;
    QString regionName_;
    QStringList beginKeywords_;
    QStringList endKeywords_;
    QString lastDiagnostic_;
    int lastRangeCount_ = 0;
    int lastUnbalancedCount_ = 0;
};

} // namespace vedit::folding
