#pragma once

#include <QThreadPool>

namespace vedit::editor {

// Admit workers with tryStart so each editor can replace its work with the
// latest revision while the shared slot is occupied.
inline QThreadPool &editorAnalysisPool()
{
    struct Pool : QThreadPool {
        Pool() { setMaxThreadCount(1); }
    };
    static Pool pool;
    return pool;
}

} // namespace vedit::editor
