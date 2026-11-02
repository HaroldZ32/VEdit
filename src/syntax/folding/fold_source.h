#pragma once

// Where fold ranges come from (task T19). The injector never looks at the
// document itself: a source produces whole-line ranges for the current text, and
// the injector validates and injects them (fold_range.h / fold_injector.h).
//
// A2.1: a source is asked for ranges ONLY on an allowed rebuild trigger (first
// open / tree-sitter toggle / reparse complete). Asking per key press would be a
// full rebuild by another name, so the injector's key path never touches a
// source.
#include "fold_range.h"

#include <QString>
#include <QStringView>

#include <vector>

namespace vedit::folding {

class FoldSource {
public:
    virtual ~FoldSource() = default;

    // A stable name for diagnostics and for the A1f on/off evidence.
    virtual QString name() const = 0;

    // Whole-line fold ranges of `text` (0-based, see fold_range.h).
    virtual std::vector<FoldRange> ranges(QStringView text) = 0;

    // A1f: whether this source currently reflects the tree-sitter parse. The
    // runtime switch (SyntaxService::setTreeSitterEnabled) turns it off, and the
    // fold set must then be observably different.
    virtual bool treeSitterActive() const
    {
        return false;
    }
};

// A fixed range list. Used by the lane's unit tests and as the "syntax
// definition" fallback state of A1f (the KSyntaxHighlighting definitions are
// consumed by the a4-fold-residual lane, not here).
class StaticFoldSource final : public FoldSource {
public:
    StaticFoldSource(QString name, std::vector<FoldRange> ranges, bool treeSitterActive = false);

    QString name() const override;
    std::vector<FoldRange> ranges(QStringView text) override;
    bool treeSitterActive() const override;

private:
    QString name_;
    std::vector<FoldRange> ranges_;
    bool treeSitterActive_ = false;
};

} // namespace vedit::folding
