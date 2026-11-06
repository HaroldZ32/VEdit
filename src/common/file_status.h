#pragma once

// ONE status vocabulary for every surface that lists a file (RM-9).
//
// The file-tree dot, the Project panel's Files list and the diagnostics
// controller must never disagree about what "clean" means or what colour it is.
// Before this header each of them carried its own copy of the three colours (or
// would have had to), which is two sources of truth for one user-visible
// meaning. The values below are the single definition; the surfaces only draw.
//
//   0 Unknown      no dot: the file is not part of any analysed set
//   1 Error        red   - at least one error-severity row
//   2 Warning      amber - warnings only
//   3 Clean        green - analysed and nothing found
//   4 NotAnalyzed  hollow grey ring - in the analysed set but no trustworthy
//                  verdict YET (queued / pass running) or excluded (dirty
//                  buffer). This state is deliberately visible: "no dot" and
//                  "clean" must not look the same (the user's complaint).
//
// The enum is an enum class, but the role/API surfaces carry the value as int
// (Qt data roles are int); fileStatusColor(int) is the one conversion point.

#include <QColor>
#include <QCoreApplication>
#include <QString>

namespace vedit::common {

enum class FileStatus : int {
    Unknown = 0,
    Error = 1,
    Warning = 2,
    Clean = 3,
    NotAnalyzed = 4,
};

/// The dot colour of one status. NotAnalyzed answers grey: its dot is drawn as
/// a HOLLOW ring by the delegates, so the colour is the ring, not a fill.
inline QColor fileStatusColor(int status)
{
    switch (static_cast<FileStatus>(status)) {
    case FileStatus::Error:
        return QColor(0xd3, 0x2f, 0x2f); // red
    case FileStatus::Warning:
        return QColor(0xf9, 0xa8, 0x25); // amber
    case FileStatus::Clean:
        return QColor(0x2e, 0x7d, 0x32); // green
    case FileStatus::NotAnalyzed:
        return QColor(0x9e, 0x9e, 0x9e); // grey ring
    case FileStatus::Unknown:
        break;
    }
    return QColor(Qt::transparent); // no dot at all
}

/// True when the status must be drawn as a filled dot (1/2/3). NotAnalyzed is
/// drawn as a ring and Unknown draws nothing.
inline bool fileStatusIsFilledDot(int status)
{
    return status == static_cast<int>(FileStatus::Error)
           || status == static_cast<int>(FileStatus::Warning)
           || status == static_cast<int>(FileStatus::Clean);
}

/// True when the row must be visible although there is no verdict yet.
inline bool fileStatusIsNotAnalyzed(int status)
{
    return status == static_cast<int>(FileStatus::NotAnalyzed);
}

/// The meaning a surface shows for a status (tooltip / status line), so the
/// user can read WHY a mark is hollow instead of guessing. One source: every
/// surface asks here.
inline QString fileStatusHint(int status)
{
    switch (static_cast<FileStatus>(status)) {
    case FileStatus::Error:
        return QCoreApplication::translate("VEdit", "Has errors");
    case FileStatus::Warning:
        return QCoreApplication::translate("VEdit", "Warnings only");
    case FileStatus::Clean:
        return QCoreApplication::translate("VEdit", "Analysed: no problems found");
    case FileStatus::NotAnalyzed:
        return QCoreApplication::translate(
            "VEdit",
            "Not analysed yet (queued, or excluded because the buffer has unsaved changes)");
    case FileStatus::Unknown:
        break;
    }
    return QString();
}

} // namespace vedit::common
