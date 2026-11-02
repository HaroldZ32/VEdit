#pragma once

// Fold range model and the A1.1 self-validation of the folding lane (task T19,
// acceptance AC-S2.2 / AMENDMENTS A1-A3).
//
// Why validation exists (AMENDMENTS A1, KDE KTextEditor 6.24.0
// src/utils/katecmds.cpp:258/261): when the range handed to the `fold` command
// is invalid, KatePart falls back to `View::selectionRange()` - an invalid range
// does NOT mean "do nothing", it means "fold whatever the user has selected".
// The command's return value is therefore useless as a success check (an
// inverted range returns true and does nothing, evidence/r2-fold/decision.md
// §3.3). Every range is validated here BEFORE `Command::exec` is called, and a
// rejected range never reaches the command, the registry or the fold counters.
#include <string>
#include <vector>

namespace vedit::folding {

// One foldable region of whole lines, 0-based and half-open in the KTextEditor
// sense: `startLine` is the construct's first line, which stays VISIBLE (that is
// where the fold marker is drawn), and [startLine+1 .. endLine] are the lines it
// hides. KTextEditor::Range(startLine, 0, endLine, 0) is exactly this region
// (evidence/r2-fold/02-run.log B1: Range(2,0,10,0) hides 8 lines, 3..10).
struct FoldRange {
    int startLine = 0;
    int endLine = 0;

    bool operator==(const FoldRange &other) const
    {
        return startLine == other.startLine && endLine == other.endLine;
    }
};

// A1.1's four checks, as explicit outcomes: a rejected range must be reportable
// with the reason, so the reason is part of the model and not just a bool.
enum class RejectReason {
    None,            // the range passed all four checks
    Invalid,         // check 1: default/invalid range (negative line = "no range")
    Inverted,        // check 2: start > end
    OutOfDocument,   // check 3: a line is outside [0, lineCount-1]
    ZeroLines,       // check 4: the range hides no line (endLine <= startLine)
};

const char *rejectReasonName(RejectReason reason); // "none|invalid|inverted|out-of-document|zero-lines"

// One rejected range plus the context a diagnostic needs (A1.2: rejected reason +
// raw range + the selection at that moment).
struct Rejection {
    FoldRange range;
    RejectReason reason = RejectReason::None;
    std::string detail; // human-readable, includes the raw range and the selection
};

struct Validation {
    std::vector<FoldRange> accepted;  // order preserved
    std::vector<Rejection> rejected;  // one entry per rejected input range
};

// Check one range against a document of `lineCount` lines (>= 1).
RejectReason validateFoldRange(const FoldRange &range, int lineCount);

// Validate a batch. The rejected entries carry `selectionStart`/`selectionEnd`
// (0-based, inclusive; {-1,-1} for "no selection") for the diagnostic line.
struct SelectionRange {
    int startLine = -1;
    int endLine = -1;
};

Validation validateFoldRanges(const std::vector<FoldRange> &ranges, int lineCount,
                              SelectionRange selection);

} // namespace vedit::folding
