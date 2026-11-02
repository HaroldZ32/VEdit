#include "fold_source.h"

#include <utility>

namespace vedit::folding {

StaticFoldSource::StaticFoldSource(QString name, std::vector<FoldRange> ranges, bool treeSitterActive)
    : name_(std::move(name))
    , ranges_(std::move(ranges))
    , treeSitterActive_(treeSitterActive)
{
}

QString StaticFoldSource::name() const
{
    return name_;
}

std::vector<FoldRange> StaticFoldSource::ranges(QStringView text)
{
    (void)text;
    return ranges_;
}

bool StaticFoldSource::treeSitterActive() const
{
    return treeSitterActive_;
}

} // namespace vedit::folding
