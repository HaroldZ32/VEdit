#include "fold_range.h"

#include <cstdio>

namespace vedit::folding {

namespace {

std::string describeRange(const FoldRange &range)
{
    return "[" + std::to_string(range.startLine) + "," + std::to_string(range.endLine) + "]";
}

std::string describeSelection(const SelectionRange &selection)
{
    if (selection.startLine < 0 || selection.endLine < 0) {
        return "none";
    }
    return "[" + std::to_string(selection.startLine) + "," + std::to_string(selection.endLine)
           + "]";
}

} // namespace

const char *rejectReasonName(RejectReason reason)
{
    switch (reason) {
    case RejectReason::None:
        return "none";
    case RejectReason::Invalid:
        return "invalid";
    case RejectReason::Inverted:
        return "inverted";
    case RejectReason::OutOfDocument:
        return "out-of-document";
    case RejectReason::ZeroLines:
        return "zero-lines";
    }
    return "unknown";
}

RejectReason validateFoldRange(const FoldRange &range, int lineCount)
{
    // 1) neither end may be missing/invalid.
    if (range.startLine < 0 || range.endLine < 0) {
        return RejectReason::Invalid;
    }
    // 2) start <= end (an inverted range must be refused, not handed to KatePart).
    if (range.startLine > range.endLine) {
        return RejectReason::Inverted;
    }
    // 3) both lines inside the document.
    if (range.startLine >= lineCount || range.endLine >= lineCount) {
        return RejectReason::OutOfDocument;
    }
    // 4) the range must hide at least one whole line.
    if (range.endLine <= range.startLine) {
        return RejectReason::ZeroLines;
    }
    return RejectReason::None;
}

Validation validateFoldRanges(const std::vector<FoldRange> &ranges, int lineCount,
                              SelectionRange selection)
{
    Validation validation;
    for (const FoldRange &range : ranges) {
        const RejectReason reason = validateFoldRange(range, lineCount);
        if (reason == RejectReason::None) {
            validation.accepted.push_back(range);
            continue;
        }
        Rejection rejection;
        rejection.range = range;
        rejection.reason = reason;
        rejection.detail = std::string("range=") + describeRange(range)
                           + " document_lines=" + std::to_string(lineCount)
                           + " selection=" + describeSelection(selection);
        validation.rejected.push_back(std::move(rejection));
    }
    return validation;
}

} // namespace vedit::folding
